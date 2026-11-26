#include "feed_manager.h"
#include <winhttp.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET
#define WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET 114
#endif

typedef HINTERNET (WINAPI *PFN_WinHttpWebSocketCompleteUpgrade)(HINTERNET, DWORD_PTR);
typedef DWORD     (WINAPI *PFN_WinHttpWebSocketSend)(HINTERNET, int, PVOID, DWORD);
typedef DWORD     (WINAPI *PFN_WinHttpWebSocketReceive)(HINTERNET, PVOID, DWORD, DWORD*, int*);
typedef DWORD     (WINAPI *PFN_WinHttpWebSocketClose)(HINTERNET, USHORT, PVOID, DWORD);

static int64_t now_utc_ms(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000ULL);
}

static void unix_sec_to_systemtime(int64_t ts_sec, SYSTEMTIME* st) {
    ULARGE_INTEGER uli;
    uli.QuadPart = (uint64_t)(ts_sec >= 0 ? ts_sec : 0) * 10000000ULL + 116444736000000000ULL;
    FILETIME ft;
    ft.dwLowDateTime  = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    FileTimeToSystemTime(&ft, st);
}

static int64_t systemtime_to_unix_sec(const SYSTEMTIME* st) {
    FILETIME ft;
    if (!SystemTimeToFileTime(st, &ft)) return 0;
    ULARGE_INTEGER uli;
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return (int64_t)((uli.QuadPart - 116444736000000000ULL) / 10000000ULL);
}

int64_t feed_bucket_start_sec(int64_t ts_sec, TimeframeSec tf) {
    if (tf == TF_1W) {
        int64_t mon_off = 345600LL; /* 1970-01-05 00:00:00 UTC (Monday) */
        return ((ts_sec - mon_off) / 604800LL) * 604800LL + mon_off;
    }
    if (tf == TF_1MO) {
        SYSTEMTIME st;
        unix_sec_to_systemtime(ts_sec, &st);
        st.wDay = 1;
        st.wHour = st.wMinute = st.wSecond = st.wMilliseconds = 0;
        return systemtime_to_unix_sec(&st);
    }
    if (tf == TF_1Y) {
        SYSTEMTIME st;
        unix_sec_to_systemtime(ts_sec, &st);
        st.wMonth = 1;
        st.wDay   = 1;
        st.wHour = st.wMinute = st.wSecond = st.wMilliseconds = 0;
        return systemtime_to_unix_sec(&st);
    }
    int64_t step = (int64_t)tf;
    return (ts_sec / step) * step;
}

static void ring_push(TickRingBuffer* rb, const FeedTick* tick) {
    AcquireSRWLockExclusive(&rb->lock);
    LONG next = (rb->head + 1) % RING_CAPACITY;
    if (next == rb->tail) {
        rb->tail = (rb->tail + 1) % RING_CAPACITY;
    }
    rb->items[rb->head] = *tick;
    rb->head = next;
    ReleaseSRWLockExclusive(&rb->lock);
    WakeConditionVariable(&rb->not_empty);
}

static bool ring_pop(TickRingBuffer* rb, FeedTick* out_tick, volatile LONG* running) {
    AcquireSRWLockExclusive(&rb->lock);
    while (rb->head == rb->tail && InterlockedCompareExchange(running, 0, 0) == 1) {
        SleepConditionVariableSRW(&rb->not_empty, &rb->lock, 100, 0);
    }
    if (rb->head == rb->tail) {
        ReleaseSRWLockExclusive(&rb->lock);
        return false;
    }
    *out_tick = rb->items[rb->tail];
    rb->tail = (rb->tail + 1) % RING_CAPACITY;
    ReleaseSRWLockExclusive(&rb->lock);
    return true;
}

static char* winhttp_get_text(const wchar_t* host, const wchar_t* path, uint32_t* out_ms) {
    int64_t t0 = now_utc_ms();
    HINTERNET hSession = WinHttpOpen(
        L"OpenViewTerminal/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) return NULL;
    WinHttpSetTimeouts(hSession, 3000, 3000, 3500, 4000);

    HINTERNET hConn = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) {
        WinHttpCloseHandle(hSession);
        return NULL;
    }

    HINTERNET hReq = WinHttpOpenRequest(
        hConn, L"GET", path, NULL, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE
    );
    if (!hReq) {
        WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSession);
        return NULL;
    }

    char* result = NULL;
    if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hReq, NULL)) {
        DWORD status = 0, status_sz = sizeof(status);
        WinHttpQueryHeaders(
            hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_sz, WINHTTP_NO_HEADER_INDEX
        );
        if (status >= 200 && status < 300) {
            size_t cap = 16384, len = 0;
            result = (char*)malloc(cap);
            if (result) {
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                    if (len + avail + 1 > cap) {
                        size_t next_cap = cap * 2 + avail;
                        if (next_cap > 2 * 1024 * 1024) break;
                        char* grown = (char*)realloc(result, next_cap);
                        if (!grown) break;
                        result = grown;
                        cap = next_cap;
                    }
                    DWORD got = 0;
                    if (!WinHttpReadData(hReq, result + len, avail, &got) || got == 0) break;
                    len += got;
                }
                result[len] = '\0';
            }
        }
    }

    if (out_ms) {
        int64_t dt = now_utc_ms() - t0;
        *out_ms = (dt > 0) ? (uint32_t)dt : 1;
    }

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSession);
    return result;
}

static size_t aggregate_bars_in_place(CandleBar* bars, size_t n, TimeframeSec tf, FeedSourceId src) {
    if (n == 0) return 0;
    size_t out_n = 0;
    for (size_t i = 0; i < n; i++) {
        int64_t bucket = feed_bucket_start_sec(bars[i].time_utc, tf);
        if (out_n > 0 && bars[out_n - 1].time_utc == bucket) {
            CandleBar* cur = &bars[out_n - 1];
            if (bars[i].high > cur->high) cur->high = bars[i].high;
            if (bars[i].low < cur->low)   cur->low  = bars[i].low;
            cur->close   = bars[i].close;
            cur->volume += bars[i].volume;
        } else {
            bars[out_n] = bars[i];
            bars[out_n].time_utc = bucket;
            bars[out_n].source   = src;
            out_n++;
        }
    }
    return out_n;
}

static void split_symbol(const wchar_t* sym, char* base_out, size_t bcap, char* quote_out, size_t qcap) {
    char utf8[48];
    WideCharToMultiByte(CP_UTF8, 0, sym, -1, utf8, sizeof(utf8), NULL, NULL);
    const char* slash = strchr(utf8, '/');
    if (slash) {
        size_t blen = (size_t)(slash - utf8);
        if (blen >= bcap) blen = bcap - 1;
        memcpy(base_out, utf8, blen);
        base_out[blen] = '\0';
        snprintf(quote_out, qcap, "%s", slash + 1);
    } else {
        snprintf(base_out, bcap, "%s", utf8);
        snprintf(quote_out, qcap, "USDT");
    }
}

static size_t fetch_binance_candles(
    const wchar_t* symbol,
    TimeframeSec tf,
    CandleBar* out,
    size_t max_out,
    uint32_t* latency_ms
) {
    char base[16], quote[16];
    split_symbol(symbol, base, sizeof(base), quote, sizeof(quote));
    if (strcmp(quote, "USD") == 0) strcpy(quote, "USDT");

    const wchar_t* iv = L"15m";
    switch (tf) {
        case TF_1M:  iv = L"1m"; break;
        case TF_5M:  iv = L"5m"; break;
        case TF_15M: iv = L"15m"; break;
        case TF_1H:  iv = L"1h"; break;
        case TF_4H:  iv = L"4h"; break;
        case TF_1D:  iv = L"1d"; break;
        case TF_1W:  iv = L"1w"; break;
        case TF_1MO: iv = L"1M"; break;
        case TF_1Y:  iv = L"1M"; break;
    }

    wchar_t wbase[16], wquote[16], path[192];
    MultiByteToWideChar(CP_UTF8, 0, base, -1, wbase, 16);
    MultiByteToWideChar(CP_UTF8, 0, quote, -1, wquote, 16);
    swprintf(path, 192, L"/api/v3/klines?symbol=%ls%ls&interval=%ls&limit=180", wbase, wquote, iv);

    char* body = winhttp_get_text(L"api.binance.com", path, latency_ms);
    if (!body) return 0;

    size_t count = 0;
    const char* p = body;
    while (count < max_out && (p = strchr(p, '[')) != NULL) {
        p++;
        if (*p == '[' || *p == ']') continue;
        if (*p < '0' || *p > '9') continue;

        long long open_ms = strtoll(p, NULL, 10);
        const char* q1 = strchr(p, '"');
        if (!q1) continue;
        double o = strtod(q1 + 1, NULL);
        const char* q2 = strchr(q1 + 1, '"');
        const char* q3 = q2 ? strchr(q2 + 1, '"') : NULL;
        double h = q3 ? strtod(q3 + 1, NULL) : o;
        const char* q4 = q3 ? strchr(q3 + 1, '"') : NULL;
        const char* q5 = q4 ? strchr(q4 + 1, '"') : NULL;
        double l = q5 ? strtod(q5 + 1, NULL) : o;
        const char* q6 = q5 ? strchr(q5 + 1, '"') : NULL;
        const char* q7 = q6 ? strchr(q6 + 1, '"') : NULL;
        double c = q7 ? strtod(q7 + 1, NULL) : o;
        const char* q8 = q7 ? strchr(q7 + 1, '"') : NULL;
        const char* q9 = q8 ? strchr(q8 + 1, '"') : NULL;
        double v = q9 ? strtod(q9 + 1, NULL) : 0.0;

        if (open_ms > 0 && c > 0.0) {
            out[count++] = (CandleBar){
                .time_utc = (int64_t)(open_ms / 1000LL),
                .open     = o,
                .high     = h,
                .low      = l,
                .close    = c,
                .volume   = v,
                .source   = SRC_BINANCE
            };
        }
        const char* end_row = strchr(p, ']');
        if (!end_row) break;
        p = end_row + 1;
    }

    free(body);
    if (tf == TF_1Y && count > 0) {
        count = aggregate_bars_in_place(out, count, TF_1Y, SRC_BINANCE);
    }
    return count;
}

static size_t fetch_coinbase_candles(
    const wchar_t* symbol,
    TimeframeSec tf,
    CandleBar* out,
    size_t max_out,
    uint32_t* latency_ms
) {
    char base[16], quote[16];
    split_symbol(symbol, base, sizeof(base), quote, sizeof(quote));
    int gran = (int)tf;
    if (gran > 86400) gran = 86400;

    wchar_t wbase[16], path[192];
    MultiByteToWideChar(CP_UTF8, 0, base, -1, wbase, 16);
    swprintf(path, 192, L"/products/%ls-USD/candles?granularity=%d", wbase, gran);

    char* body = winhttp_get_text(L"api.exchange.coinbase.com", path, latency_ms);
    if (!body) return 0;

    CandleBar rev[320];
    size_t rcnt = 0;
    const char* p = body;
    while (rcnt < 300 && (p = strchr(p, '[')) != NULL) {
        p++;
        if (*p == '[') continue;
        if (*p < '0' || *p > '9') continue;
        long long ts = 0;
        double lo = 0, hi = 0, op = 0, cl = 0, vol = 0;
        if (sscanf(p, "%lld,%lf,%lf,%lf,%lf,%lf", &ts, &lo, &hi, &op, &cl, &vol) == 6 && cl > 0.0) {
            rev[rcnt++] = (CandleBar){
                .time_utc = (int64_t)ts,
                .open     = op,
                .high     = hi,
                .low      = lo,
                .close    = cl,
                .volume   = vol,
                .source   = SRC_COINBASE
            };
        }
        const char* end_row = strchr(p, ']');
        if (!end_row) break;
        p = end_row + 1;
    }
    free(body);

    size_t count = 0;
    for (size_t i = rcnt; i > 0 && count < max_out; i--) {
        out[count++] = rev[i - 1];
    }
    if ((tf == TF_1W || tf == TF_1MO || tf == TF_1Y) && count > 0) {
        count = aggregate_bars_in_place(out, count, tf, SRC_COINBASE);
    }
    return count;
}

static size_t fetch_kraken_candles(
    const wchar_t* symbol,
    TimeframeSec tf,
    CandleBar* out,
    size_t max_out,
    uint32_t* latency_ms
) {
    char base[16], quote[16];
    split_symbol(symbol, base, sizeof(base), quote, sizeof(quote));
    if (strcmp(base, "BTC") == 0) strcpy(base, "XBT");

    int iv = (int)tf / 60;
    if (tf == TF_1W || tf == TF_1MO || tf == TF_1Y) iv = 10080;

    wchar_t wbase[16], path[192];
    MultiByteToWideChar(CP_UTF8, 0, base, -1, wbase, 16);
    swprintf(path, 192, L"/0/public/OHLC?pair=%lsUSD&interval=%d", wbase, iv);

    char* body = winhttp_get_text(L"api.kraken.com", path, latency_ms);
    if (!body) return 0;

    const char* arr_start = strstr(body, ":[[");
    if (!arr_start) {
        free(body);
        return 0;
    }

    size_t count = 0;
    const char* p = arr_start + 2;
    while (count < max_out && (p = strchr(p, '[')) != NULL) {
        p++;
        if (*p < '0' || *p > '9') continue;
        long long ts = strtoll(p, NULL, 10);
        double o = 0, h = 0, l = 0, c = 0, vwap = 0, vol = 0;
        const char* comma = strchr(p, ',');
        if (comma && sscanf(comma + 1, "\"%lf\",\"%lf\",\"%lf\",\"%lf\",\"%lf\",\"%lf\"",
                            &o, &h, &l, &c, &vwap, &vol) == 6 && c > 0.0) {
            out[count++] = (CandleBar){
                .time_utc = (int64_t)ts,
                .open     = o,
                .high     = h,
                .low      = l,
                .close    = c,
                .volume   = vol,
                .source   = SRC_KRAKEN
            };
        }
        const char* end_row = strchr(p, ']');
        if (!end_row) break;
        p = end_row + 1;
    }
    free(body);

    if ((tf == TF_1MO || tf == TF_1Y) && count > 0) {
        count = aggregate_bars_in_place(out, count, tf, SRC_KRAKEN);
    } else if (count > 180) {
        size_t skip = count - 180;
        memmove(out, out + skip, 180 * sizeof(CandleBar));
        count = 180;
    }
    return count;
}

static void build_seed_candles_locked(
    AsyncFeedManager* mgr,
    const wchar_t* symbol,
    TimeframeSec tf,
    FeedSourceId s
) {
    double base_price = 100.0;
    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        if (wcscmp(mgr->watchlist[w].symbol, symbol) == 0) {
            base_price = mgr->watchlist[w].price;
            break;
        }
    }

    int64_t now_s = now_utc_ms() / 1000;
    int64_t aligned_now = feed_bucket_start_sec(now_s, tf);
    SYSTEMTIME now_st;
    unix_sec_to_systemtime(aligned_now, &now_st);

    size_t count = 180;
    if (tf == TF_1Y)       count = 15;
    else if (tf == TF_1MO) count = 72;
    else if (tf == TF_1W)  count = 120;

    uint32_t sym_hash = 0;
    for (const wchar_t* p = symbol; *p; p++) sym_hash += (uint32_t)*p;

    double src_off = (s == SRC_BINANCE) ? 0.0 : (s == SRC_COINBASE ? 0.00035 : -0.00028);
    double cursor = base_price * ((tf == TF_1Y) ? 0.25 : (tf == TF_1MO ? 0.55 : 0.945));

    for (size_t i = 0; i < count; i++) {
        int steps_back = (int)(count - 1 - i);
        int64_t ts = 0;
        if (tf == TF_1Y) {
            SYSTEMTIME st = now_st;
            st.wYear = (WORD)(now_st.wYear - steps_back);
            st.wMonth = 1;
            st.wDay = 1;
            ts = systemtime_to_unix_sec(&st);
        } else if (tf == TF_1MO) {
            int total_m = now_st.wYear * 12 + (now_st.wMonth - 1) - steps_back;
            SYSTEMTIME st = now_st;
            st.wYear  = (WORD)(total_m / 12);
            st.wMonth = (WORD)((total_m % 12) + 1);
            st.wDay   = 1;
            ts = systemtime_to_unix_sec(&st);
        } else {
            ts = aligned_now - (int64_t)steps_back * (int64_t)tf;
        }

        double wave = sin((double)(i + sym_hash) * 0.17) * 0.0042 +
                      cos((double)(i * 0.43 + sym_hash) * 0.23) * 0.0026 + 0.00032;
        double open  = cursor * (1.0 + src_off);
        double close = open * (1.0 + wave);
        double wu = fabs(sin((double)i * 1.3 + sym_hash)) * open * 0.0032;
        double wd = fabs(cos((double)i * 1.1 + sym_hash)) * open * 0.0032;
        double hi = (open > close ? open : close) + wu;
        double lo = (open < close ? open : close) - wd;
        double vb = base_price > 1000.0 ? 65.0 : 1800.0;

        mgr->candles[s][i] = (CandleBar){
            .time_utc = ts,
            .open     = open,
            .high     = hi,
            .low      = lo,
            .close    = close,
            .volume   = vb * (0.6 + fabs(sin((double)i * 0.7)) * 1.4),
            .source   = s
        };
        cursor = close / (1.0 + src_off);
    }

    double last_c = mgr->candles[s][count - 1].close;
    double scale = (last_c > 0.0) ? (base_price * (1.0 + src_off)) / last_c : 1.0;
    for (size_t i = 0; i < count; i++) {
        mgr->candles[s][i].open  *= scale;
        mgr->candles[s][i].high  *= scale;
        mgr->candles[s][i].low   *= scale;
        mgr->candles[s][i].close *= scale;
    }
    mgr->candle_counts[s] = count;
}

static void seed_initial_watchlist(AsyncFeedManager* mgr) {
    static const struct {
        const wchar_t* symbol;
        const wchar_t* name;
        double base_price;
    } kSeeds[] = {
        { L"BTC/USDT",  L"Bitcoin",   96420.0 },
        { L"ETH/USDT",  L"Ethereum",   3425.0 },
        { L"SOL/USDT",  L"Solana",      214.8 },
        { L"AVAX/USDT", L"Avalanche",    41.9 },
        { L"LINK/USDT", L"Chainlink",    19.45 },
        { L"ARB/USDT",  L"Arbitrum",      1.14 },
        { L"XRP/USDT",  L"Ripple",        1.48 },
        { L"DOGE/USDT", L"Dogecoin",     0.392 }
    };

    mgr->section_count = 1;
    mgr->sections[0].id = mgr->next_section_id++;
    ov_wcscpy(mgr->sections[0].name, 48, L"Favoris");
    mgr->sections[0].collapsed = false;

    mgr->watchlist_count = sizeof(kSeeds) / sizeof(kSeeds[0]);
    int64_t now_ms = now_utc_ms();

    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        WatchlistItem* item = &mgr->watchlist[w];
        item->id         = mgr->next_wl_id++;
        item->section_id = mgr->sections[0].id;
        ov_wcscpy(item->symbol, 24, kSeeds[w].symbol);
        ov_wcscpy(item->name, 48, kSeeds[w].name);
        item->display_name[0] = L'\0';
        item->price      = kSeeds[w].base_price;
        item->open_24h   = kSeeds[w].base_price * 0.985;
        item->high_24h   = kSeeds[w].base_price * 1.022;
        item->low_24h    = kSeeds[w].base_price * 0.976;
        item->volume_24h = (kSeeds[w].base_price > 1000.0) ? 18420.0 : 492000.0;
        item->change_pct = 1.52;
        for (int s = 0; s < SRC_COUNT; s++) {
            item->source_prices[s] = kSeeds[w].base_price * (1.0 + (s - 1) * 0.00025);
        }
        item->last_update_ms = now_ms;
        item->removed = false;
    }

    if (mgr->watchlist_count > 0) {
        mgr->active_entry_id = mgr->watchlist[0].id;
    }

    for (int s = 0; s < SRC_COUNT; s++) {
        build_seed_candles_locked(mgr, mgr->active_symbol, mgr->timeframe, (FeedSourceId)s);
    }
}

static void sync_exchange_snapshots(AsyncFeedManager* mgr) {
    wchar_t sym[24];
    TimeframeSec tf;
    AcquireSRWLockShared(&mgr->data_lock);
    ov_wcscpy(sym, 24, mgr->active_symbol);
    tf = mgr->timeframe;
    ReleaseSRWLockShared(&mgr->data_lock);

    size_t min_bars = (tf == TF_1Y) ? 2 : (tf == TF_1MO ? 6 : 20);
    CandleBar* tmp = (CandleBar*)malloc(sizeof(CandleBar) * 320);
    if (!tmp) return;

    for (int s = 0; s < SRC_COUNT; s++) {
        uint32_t lat = 0;
        size_t n = 0;
        if (s == SRC_BINANCE)       n = fetch_binance_candles(sym, tf, tmp, 320, &lat);
        else if (s == SRC_COINBASE) n = fetch_coinbase_candles(sym, tf, tmp, 320, &lat);
        else                        n = fetch_kraken_candles(sym, tf, tmp, 320, &lat);

        AcquireSRWLockExclusive(&mgr->data_lock);
        if (wcscmp(mgr->active_symbol, sym) == 0 && mgr->timeframe == tf && n >= min_bars) {
            if (n > MAX_CANDLES) n = MAX_CANDLES;
            memcpy(mgr->candles[s], tmp, n * sizeof(CandleBar));
            mgr->candle_counts[s] = n;
            mgr->sources[s].state = STATE_LIVE;
            mgr->sources[s].latency_ms = lat;

            double last_px = tmp[n - 1].close;
            for (size_t w = 0; w < mgr->watchlist_count; w++) {
                if (mgr->watchlist[w].removed) continue;
                if (wcscmp(mgr->watchlist[w].symbol, sym) == 0) {
                    mgr->watchlist[w].source_prices[s] = last_px;
                    if (s == SRC_BINANCE) {
                        mgr->watchlist[w].price = last_px;
                        if (mgr->watchlist[w].open_24h > 0.0) {
                            mgr->watchlist[w].change_pct =
                                ((last_px - mgr->watchlist[w].open_24h) / mgr->watchlist[w].open_24h) * 100.0;
                        }
                    }
                }
            }
        }
        ReleaseSRWLockExclusive(&mgr->data_lock);
    }

    free(tmp);
    if (mgr->ui_hwnd) {
        PostMessageW(mgr->ui_hwnd, WM_APP_SNAPSHOT_READY, 0, 0);
    }
}

static DWORD WINAPI rest_sync_proc(LPVOID param) {
    AsyncFeedManager* mgr = (AsyncFeedManager*)param;
    LONG last_seq = -1;
    int64_t next_periodic = 0;

    while (InterlockedCompareExchange(&mgr->running, 0, 0) == 1) {
        LONG cur_seq = InterlockedCompareExchange(&mgr->resync_seq, 0, 0);
        int64_t now = now_utc_ms();
        if (cur_seq != last_seq || now >= next_periodic) {
            last_seq = cur_seq;
            sync_exchange_snapshots(mgr);
            next_periodic = now_utc_ms() + 18000;
        }
        Sleep(120);
    }
    return 0;
}

typedef struct {
    AsyncFeedManager* mgr;
    FeedSourceId      source_id;
    const char*       host;
    const char*       port;
    const char*       ws_path;
} SourceWorkerCtx;

static DWORD WINAPI source_worker_proc(LPVOID param) {
    SourceWorkerCtx* ctx = (SourceWorkerCtx*)param;
    AsyncFeedManager* mgr = ctx->mgr;
    FeedSourceId sid = ctx->source_id;
    uint32_t seed = (uint32_t)(GetTickCount64() ^ ((uint64_t)sid * 2654435761U));

    HMODULE hWinHttp = GetModuleHandleW(L"winhttp.dll");
    PFN_WinHttpWebSocketCompleteUpgrade pfnUpgrade = NULL;
    PFN_WinHttpWebSocketSend            pfnSend    = NULL;
    PFN_WinHttpWebSocketReceive         pfnRecv    = NULL;
    PFN_WinHttpWebSocketClose           pfnClose   = NULL;

    if (hWinHttp) {
        FARPROC f1 = GetProcAddress(hWinHttp, "WinHttpWebSocketCompleteUpgrade");
        FARPROC f2 = GetProcAddress(hWinHttp, "WinHttpWebSocketSend");
        FARPROC f3 = GetProcAddress(hWinHttp, "WinHttpWebSocketReceive");
        FARPROC f4 = GetProcAddress(hWinHttp, "WinHttpWebSocketClose");
        if (f1 && f2 && f3 && f4) {
            memcpy(&pfnUpgrade, &f1, sizeof(f1));
            memcpy(&pfnSend,    &f2, sizeof(f2));
            memcpy(&pfnRecv,    &f3, sizeof(f3));
            memcpy(&pfnClose,   &f4, sizeof(f4));
        }
    }

    while (InterlockedCompareExchange(&mgr->running, 0, 0) == 1) {
        bool connected_ws = false;

        if (pfnUpgrade && pfnRecv) {
            wchar_t whost[64], wpath[128];
            MultiByteToWideChar(CP_UTF8, 0, ctx->host, -1, whost, 64);

            wchar_t sym[24];
            AcquireSRWLockShared(&mgr->data_lock);
            ov_wcscpy(sym, 24, mgr->active_symbol);
            ReleaseSRWLockShared(&mgr->data_lock);

            char base[16], quote[16];
            split_symbol(sym, base, sizeof(base), quote, sizeof(quote));
            for (char* c = base; *c; c++) {
                if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
            }
            for (char* c = quote; *c; c++) {
                if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
            }

            if (sid == SRC_BINANCE) {
                wchar_t wb[16], wq[16];
                MultiByteToWideChar(CP_UTF8, 0, base, -1, wb, 16);
                MultiByteToWideChar(CP_UTF8, 0, quote, -1, wq, 16);
                swprintf(wpath, 128, L"/ws/%ls%ls@trade", wb, wq);
            } else {
                MultiByteToWideChar(CP_UTF8, 0, ctx->ws_path, -1, wpath, 128);
            }

            INTERNET_PORT port = (INTERNET_PORT)atoi(ctx->port);
            HINTERNET hSess = WinHttpOpen(
                L"OpenViewTerminal/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0
            );
            HINTERNET hConn = hSess ? WinHttpConnect(hSess, whost, port, 0) : NULL;
            HINTERNET hReq  = hConn ? WinHttpOpenRequest(
                hConn, L"GET", wpath, NULL, WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE
            ) : NULL;

            if (hReq && WinHttpSetOption(hReq, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) &&
                WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) &&
                WinHttpReceiveResponse(hReq, NULL)) {
                HINTERNET hWs = pfnUpgrade(hReq, 0);
                WinHttpCloseHandle(hReq);
                hReq = NULL;

                if (hWs) {
                    connected_ws = true;
                    AcquireSRWLockExclusive(&mgr->data_lock);
                    mgr->sources[sid].state = STATE_LIVE;
                    ReleaseSRWLockExclusive(&mgr->data_lock);

                    /* Subscribe for Coinbase / Kraken */
                    if (sid == SRC_COINBASE || sid == SRC_KRAKEN) {
                        char ub[16], sub_msg[256];
                        split_symbol(sym, ub, sizeof(ub), quote, sizeof(quote));
                        if (sid == SRC_COINBASE) {
                            snprintf(sub_msg, sizeof(sub_msg),
                                     "{\"type\":\"subscribe\",\"product_ids\":[\"%s-USD\"],\"channels\":[\"ticker\"]}",
                                     ub);
                        } else {
                            snprintf(sub_msg, sizeof(sub_msg),
                                     "{\"method\":\"subscribe\",\"params\":{\"channel\":\"ticker\",\"symbol\":[\"%s/USD\"]}}",
                                     ub);
                        }
                        pfnSend(hWs, 0, sub_msg, (DWORD)strlen(sub_msg));
                    }

                    char frame_buf[4096];
                    wchar_t cur_sym[24];
                    ov_wcscpy(cur_sym, 24, sym);

                    while (InterlockedCompareExchange(&mgr->running, 0, 0) == 1) {
                        AcquireSRWLockShared(&mgr->data_lock);
                        bool sym_changed = (wcscmp(mgr->active_symbol, cur_sym) != 0);
                        ReleaseSRWLockShared(&mgr->data_lock);
                        if (sym_changed) break;

                        DWORD got = 0;
                        int btype = 0;
                        DWORD err = pfnRecv(hWs, frame_buf, sizeof(frame_buf) - 1, &got, &btype);
                        if (err != ERROR_SUCCESS || btype == 4) break;
                        frame_buf[got] = '\0';

                        const char* pf = strstr(frame_buf, "\"p\":\"");
                        if (!pf) pf = strstr(frame_buf, "\"price\":\"");
                        if (pf) {
                            const char* vs = strchr(pf + 3, '"');
                            if (vs) {
                                double px = strtod(vs + 1, NULL);
                                if (px > 0.0) {
                                    FeedTick tk = {0};
                                    ov_wcscpy(tk.symbol, 24, cur_sym);
                                    tk.source       = sid;
                                    tk.price        = px;
                                    tk.volume       = 0.25;
                                    tk.timestamp_ms = now_utc_ms();
                                    ring_push(&mgr->ring, &tk);
                                }
                            }
                        } else {
                            const char* lf = strstr(frame_buf, "\"last\":");
                            if (lf) {
                                double px = strtod(lf + 7, NULL);
                                if (px > 0.0) {
                                    FeedTick tk = {0};
                                    ov_wcscpy(tk.symbol, 24, cur_sym);
                                    tk.source       = sid;
                                    tk.price        = px;
                                    tk.volume       = 0.25;
                                    tk.timestamp_ms = now_utc_ms();
                                    ring_push(&mgr->ring, &tk);
                                }
                            }
                        }
                    }

                    pfnClose(hWs, 1000, NULL, 0);
                    WinHttpCloseHandle(hWs);
                }
            }
            if (hReq)  WinHttpCloseHandle(hReq);
            if (hConn) WinHttpCloseHandle(hConn);
            if (hSess) WinHttpCloseHandle(hSess);
        }

        if (!connected_ws) {
            AcquireSRWLockExclusive(&mgr->data_lock);
            mgr->sources[sid].state = STATE_FALLBACK;
            ReleaseSRWLockExclusive(&mgr->data_lock);

            for (int tick_i = 0; tick_i < 16 && InterlockedCompareExchange(&mgr->running, 0, 0) == 1; tick_i++) {
                Sleep(450 + (sid * 90));
                seed = seed * 1664525U + 1013904223U;
                double r = ((double)(seed & 0xFFFF) / 65535.0) - 0.5;

                AcquireSRWLockShared(&mgr->data_lock);
                wchar_t sym[24];
                ov_wcscpy(sym, 24, mgr->active_symbol);
                double ref_px = 100.0;
                for (size_t w = 0; w < mgr->watchlist_count; w++) {
                    if (wcscmp(mgr->watchlist[w].symbol, sym) == 0) {
                        ref_px = mgr->watchlist[w].source_prices[sid];
                        if (ref_px <= 0.0) ref_px = mgr->watchlist[w].price;
                        break;
                    }
                }
                ReleaseSRWLockShared(&mgr->data_lock);

                FeedTick tk = {0};
                ov_wcscpy(tk.symbol, 24, sym);
                tk.source       = sid;
                tk.price        = ref_px * (1.0 + r * 0.0011);
                tk.volume       = 0.4 + fabs(r) * 4.5;
                tk.timestamp_ms = now_utc_ms();
                ring_push(&mgr->ring, &tk);
            }
        }
    }

    free(ctx);
    return 0;
}

static DWORD WINAPI sync_aggregator_proc(LPVOID param) {
    AsyncFeedManager* mgr = (AsyncFeedManager*)param;
    FeedTick tk;

    while (ring_pop(&mgr->ring, &tk, &mgr->running)) {
        AcquireSRWLockExclusive(&mgr->data_lock);

        mgr->sources[tk.source].ticks_received++;
        if (mgr->sources[tk.source].latency_ms == 0) {
            mgr->sources[tk.source].latency_ms = (uint32_t)(12 + (tk.timestamp_ms % 24));
        }

        for (size_t w = 0; w < mgr->watchlist_count; w++) {
            WatchlistItem* item = &mgr->watchlist[w];
            if (item->removed) continue;
            if (wcscmp(item->symbol, tk.symbol) == 0) {
                item->source_prices[tk.source] = tk.price;
                if (tk.source == mgr->primary_source || mgr->sources[mgr->primary_source].state != STATE_LIVE) {
                    item->price = tk.price;
                    if (tk.price > item->high_24h) item->high_24h = tk.price;
                    if (item->low_24h <= 0.0 || tk.price < item->low_24h) item->low_24h = tk.price;
                    item->volume_24h += tk.volume;
                    if (item->open_24h > 0.0) {
                        item->change_pct = ((item->price - item->open_24h) / item->open_24h) * 100.0;
                    }
                }
                item->last_update_ms = tk.timestamp_ms;
            }
        }

        if (wcscmp(tk.symbol, mgr->active_symbol) == 0) {
            int64_t bar_time = feed_bucket_start_sec(tk.timestamp_ms / 1000, mgr->timeframe);
            size_t* cnt      = &mgr->candle_counts[tk.source];
            CandleBar* arr   = mgr->candles[tk.source];

            if (*cnt > 0 && arr[*cnt - 1].time_utc == bar_time) {
                CandleBar* cur = &arr[*cnt - 1];
                if (tk.price > cur->high) cur->high = tk.price;
                if (tk.price < cur->low)  cur->low  = tk.price;
                cur->close   = tk.price;
                cur->volume += tk.volume;
            } else {
                if (*cnt >= MAX_CANDLES) {
                    memmove(&arr[0], &arr[1], (MAX_CANDLES - 1) * sizeof(CandleBar));
                    *cnt = MAX_CANDLES - 1;
                }
                double prev_close = (*cnt > 0) ? arr[*cnt - 1].close : tk.price;
                arr[*cnt] = (CandleBar){
                    .time_utc = bar_time,
                    .open     = prev_close,
                    .high     = tk.price > prev_close ? tk.price : prev_close,
                    .low      = tk.price < prev_close ? tk.price : prev_close,
                    .close    = tk.price,
                    .volume   = tk.volume,
                    .source   = tk.source
                };
                (*cnt)++;
            }
        }

        ReleaseSRWLockExclusive(&mgr->data_lock);

        if (mgr->ui_hwnd) {
            PostMessageW(mgr->ui_hwnd, WM_APP_FEED_TICK, (WPARAM)tk.source, 0);
        }
    }

    return 0;
}

bool feed_manager_init(AsyncFeedManager* mgr, HWND ui_hwnd) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }

    ZeroMemory(mgr, sizeof(*mgr));
    mgr->ui_hwnd         = ui_hwnd;
    mgr->running         = 1;
    mgr->primary_source  = SRC_BINANCE;
    mgr->timeframe       = TF_15M;
    mgr->next_section_id = 1;
    mgr->next_wl_id      = 1;
    ov_wcscpy(mgr->active_symbol, 24, L"BTC/USDT");

    InitializeSRWLock(&mgr->ring.lock);
    InitializeConditionVariable(&mgr->ring.not_empty);
    InitializeSRWLock(&mgr->data_lock);

    ov_wcscpy(mgr->sources[SRC_BINANCE].label, 24, L"Binance");
    ov_wcscpy(mgr->sources[SRC_BINANCE].endpoint, 64, L"wss://stream.binance.com:9443");
    ov_wcscpy(mgr->sources[SRC_COINBASE].label, 24, L"Coinbase");
    ov_wcscpy(mgr->sources[SRC_COINBASE].endpoint, 64, L"wss://ws-feed.exchange.coinbase.com");
    ov_wcscpy(mgr->sources[SRC_KRAKEN].label, 24, L"Kraken");
    ov_wcscpy(mgr->sources[SRC_KRAKEN].endpoint, 64, L"wss://ws.kraken.com/v2");

    seed_initial_watchlist(mgr);

    mgr->sync_thread = CreateThread(NULL, 0, sync_aggregator_proc, mgr, 0, NULL);
    mgr->rest_thread = CreateThread(NULL, 0, rest_sync_proc, mgr, 0, NULL);

    static const struct {
        FeedSourceId id;
        const char*  host;
        const char*  port;
        const char*  path;
    } kEndpoints[SRC_COUNT] = {
        { SRC_BINANCE,  "stream.binance.com",            "9443", "/ws/btcusdt@trade" },
        { SRC_COINBASE, "ws-feed.exchange.coinbase.com", "443",  "/" },
        { SRC_KRAKEN,   "ws.kraken.com",                 "443",  "/v2" }
    };

    for (int i = 0; i < SRC_COUNT; i++) {
        SourceWorkerCtx* ctx = (SourceWorkerCtx*)calloc(1, sizeof(SourceWorkerCtx));
        if (!ctx) continue;
        ctx->mgr       = mgr;
        ctx->source_id = kEndpoints[i].id;
        ctx->host      = kEndpoints[i].host;
        ctx->port      = kEndpoints[i].port;
        ctx->ws_path   = kEndpoints[i].path;
        mgr->worker_threads[i] = CreateThread(NULL, 0, source_worker_proc, ctx, 0, NULL);
    }

    return true;
}

void feed_manager_shutdown(AsyncFeedManager* mgr) {
    InterlockedExchange(&mgr->running, 0);
    WakeAllConditionVariable(&mgr->ring.not_empty);

    for (int i = 0; i < SRC_COUNT; i++) {
        if (mgr->worker_threads[i]) {
            WaitForSingleObject(mgr->worker_threads[i], 2000);
            CloseHandle(mgr->worker_threads[i]);
            mgr->worker_threads[i] = NULL;
        }
    }
    if (mgr->rest_thread) {
        WaitForSingleObject(mgr->rest_thread, 2000);
        CloseHandle(mgr->rest_thread);
        mgr->rest_thread = NULL;
    }
    if (mgr->sync_thread) {
        WaitForSingleObject(mgr->sync_thread, 2000);
        CloseHandle(mgr->sync_thread);
        mgr->sync_thread = NULL;
    }
    WSACleanup();
}

void feed_manager_set_active_symbol(AsyncFeedManager* mgr, const wchar_t* symbol) {
    AcquireSRWLockExclusive(&mgr->data_lock);
    ov_wcscpy(mgr->active_symbol, 24, symbol);
    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        if (!mgr->watchlist[w].removed && wcscmp(mgr->watchlist[w].symbol, symbol) == 0) {
            mgr->active_entry_id = mgr->watchlist[w].id;
            break;
        }
    }
    for (int s = 0; s < SRC_COUNT; s++) {
        build_seed_candles_locked(mgr, mgr->active_symbol, mgr->timeframe, (FeedSourceId)s);
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
    InterlockedIncrement(&mgr->resync_seq);
}

void feed_manager_set_active_entry(AsyncFeedManager* mgr, uint32_t entry_id) {
    bool sym_changed = false;
    AcquireSRWLockExclusive(&mgr->data_lock);
    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        WatchlistItem* it = &mgr->watchlist[w];
        if (!it->removed && it->id == entry_id) {
            mgr->active_entry_id = entry_id;
            if (wcscmp(mgr->active_symbol, it->symbol) != 0) {
                ov_wcscpy(mgr->active_symbol, 24, it->symbol);
                for (int s = 0; s < SRC_COUNT; s++) {
                    build_seed_candles_locked(mgr, mgr->active_symbol, mgr->timeframe, (FeedSourceId)s);
                }
                sym_changed = true;
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
    if (sym_changed) {
        InterlockedIncrement(&mgr->resync_seq);
    }
}

void feed_manager_set_timeframe(AsyncFeedManager* mgr, TimeframeSec tf) {
    AcquireSRWLockExclusive(&mgr->data_lock);
    mgr->timeframe = tf;
    for (int s = 0; s < SRC_COUNT; s++) {
        build_seed_candles_locked(mgr, mgr->active_symbol, mgr->timeframe, (FeedSourceId)s);
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
    InterlockedIncrement(&mgr->resync_seq);
}

void feed_manager_set_primary_source(AsyncFeedManager* mgr, FeedSourceId src) {
    AcquireSRWLockExclusive(&mgr->data_lock);
    mgr->primary_source = src;
    ReleaseSRWLockExclusive(&mgr->data_lock);
}

void feed_manager_toggle_overlay(AsyncFeedManager* mgr, FeedSourceId src) {
    AcquireSRWLockExclusive(&mgr->data_lock);
    mgr->overlay_sources[src] = !mgr->overlay_sources[src];
    ReleaseSRWLockExclusive(&mgr->data_lock);
}

static uint32_t resolve_section_id_locked(const AsyncFeedManager* mgr, uint32_t section_id) {
    if (section_id != 0) {
        for (size_t i = 0; i < mgr->section_count; i++) {
            if (mgr->sections[i].id == section_id) return section_id;
        }
    }
    return (mgr->section_count > 0) ? mgr->sections[0].id : 1;
}

static WatchlistItem* alloc_watchlist_slot_locked(AsyncFeedManager* mgr) {
    if (mgr->watchlist_count < MAX_WATCHLIST) {
        return &mgr->watchlist[mgr->watchlist_count++];
    }
    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        if (mgr->watchlist[w].removed) {
            return &mgr->watchlist[w];
        }
    }
    return NULL;
}

bool feed_manager_add_token(AsyncFeedManager* mgr, const wchar_t* symbol, const wchar_t* name, uint32_t section_id) {
    if (!symbol || !symbol[0]) return false;
    wchar_t clean[24];
    size_t out_i = 0;
    for (size_t i = 0; symbol[i] && out_i < 22; i++) {
        wchar_t ch = symbol[i];
        if (ch == L' ') continue;
        if (ch >= L'a' && ch <= L'z') ch = (wchar_t)(ch - 32);
        clean[out_i++] = ch;
    }
    clean[out_i] = L'\0';
    if (!wcschr(clean, L'/') && out_i + 5 < 23) {
        wcscat(clean, L"/USDT");
    }

    AcquireSRWLockExclusive(&mgr->data_lock);
    uint32_t target_sec = resolve_section_id_locked(mgr, section_id);

    size_t existing_count = 0;
    const WatchlistItem* sample = NULL;
    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        if (!mgr->watchlist[w].removed && wcscmp(mgr->watchlist[w].symbol, clean) == 0) {
            if (!sample) sample = &mgr->watchlist[w];
            existing_count++;
        }
    }

    WatchlistItem* it = alloc_watchlist_slot_locked(mgr);
    if (!it) {
        ReleaseSRWLockExclusive(&mgr->data_lock);
        return false;
    }

    if (existing_count > 0 && sample) {
        /* Already in list -> CLONE IT with distinct ID & display name */
        WatchlistItem clone = *sample;
        clone.id         = mgr->next_wl_id++;
        clone.section_id = target_sec;
        clone.removed    = false;
        clone.last_update_ms = now_utc_ms();

        wchar_t base[24];
        ov_wcscpy(base, 24, clean);
        wchar_t* sl = wcschr(base, L'/');
        if (sl) *sl = L'\0';
        swprintf(clone.display_name, 48, L"%ls (copie %zu)", base, existing_count + 1);

        *it = clone;
        mgr->active_entry_id = it->id;
        bool sym_changed = (wcscmp(mgr->active_symbol, clean) != 0);
        if (sym_changed) {
            ov_wcscpy(mgr->active_symbol, 24, clean);
            for (int s = 0; s < SRC_COUNT; s++) {
                build_seed_candles_locked(mgr, clean, mgr->timeframe, (FeedSourceId)s);
            }
        }
        ReleaseSRWLockExclusive(&mgr->data_lock);
        if (sym_changed) InterlockedIncrement(&mgr->resync_seq);
        return true;
    }

    ZeroMemory(it, sizeof(*it));
    it->id         = mgr->next_wl_id++;
    it->section_id = target_sec;
    ov_wcscpy(it->symbol, 24, clean);
    if (name && name[0]) {
        ov_wcscpy(it->name, 48, name);
    } else {
        ov_wcscpy(it->name, 48, clean);
        wchar_t* sl = wcschr(it->name, L'/');
        if (sl) *sl = L'\0';
    }
    it->display_name[0] = L'\0';
    it->price      = 25.0;
    it->open_24h   = 24.75;
    it->high_24h   = 25.75;
    it->low_24h    = 24.25;
    it->volume_24h = 125000.0;
    it->change_pct = 1.01;
    it->source_prices[SRC_BINANCE]  = 25.0;
    it->source_prices[SRC_COINBASE] = 25.005;
    it->source_prices[SRC_KRAKEN]   = 24.995;
    it->last_update_ms = now_utc_ms();

    mgr->active_entry_id = it->id;
    ov_wcscpy(mgr->active_symbol, 24, clean);
    for (int s = 0; s < SRC_COUNT; s++) {
        build_seed_candles_locked(mgr, clean, mgr->timeframe, (FeedSourceId)s);
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
    InterlockedIncrement(&mgr->resync_seq);
    return true;
}

bool feed_manager_clone_entry(AsyncFeedManager* mgr, uint32_t entry_id) {
    AcquireSRWLockExclusive(&mgr->data_lock);
    const WatchlistItem* src = NULL;
    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        if (!mgr->watchlist[w].removed && mgr->watchlist[w].id == entry_id) {
            src = &mgr->watchlist[w];
            break;
        }
    }
    if (!src) {
        ReleaseSRWLockExclusive(&mgr->data_lock);
        return false;
    }

    size_t same_sym = 0;
    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        if (!mgr->watchlist[w].removed && wcscmp(mgr->watchlist[w].symbol, src->symbol) == 0) {
            same_sym++;
        }
    }

    WatchlistItem src_copy = *src;
    WatchlistItem* dst = alloc_watchlist_slot_locked(mgr);
    if (!dst) {
        ReleaseSRWLockExclusive(&mgr->data_lock);
        return false;
    }

    *dst = src_copy;
    dst->id      = mgr->next_wl_id++;
    dst->removed = false;
    dst->last_update_ms = now_utc_ms();

    wchar_t base[24];
    ov_wcscpy(base, 24, src_copy.symbol);
    wchar_t* sl = wcschr(base, L'/');
    if (sl) *sl = L'\0';
    swprintf(dst->display_name, 48, L"%ls (copie %zu)", base, same_sym + 1);

    mgr->active_entry_id = dst->id;
    bool sym_changed = (wcscmp(mgr->active_symbol, dst->symbol) != 0);
    if (sym_changed) {
        ov_wcscpy(mgr->active_symbol, 24, dst->symbol);
        for (int s = 0; s < SRC_COUNT; s++) {
            build_seed_candles_locked(mgr, dst->symbol, mgr->timeframe, (FeedSourceId)s);
        }
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
    if (sym_changed) InterlockedIncrement(&mgr->resync_seq);
    return true;
}

void feed_manager_rename_entry(AsyncFeedManager* mgr, uint32_t entry_id, const wchar_t* display_name) {
    AcquireSRWLockExclusive(&mgr->data_lock);
    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        if (!mgr->watchlist[w].removed && mgr->watchlist[w].id == entry_id) {
            if (!display_name) {
                mgr->watchlist[w].display_name[0] = L'\0';
            } else {
                const wchar_t* p = display_name;
                while (*p == L' ' || *p == L'\t') p++;
                ov_wcscpy(mgr->watchlist[w].display_name, 48, p);
                size_t len = wcslen(mgr->watchlist[w].display_name);
                while (len > 0 && (mgr->watchlist[w].display_name[len - 1] == L' ' ||
                                   mgr->watchlist[w].display_name[len - 1] == L'\t')) {
                    mgr->watchlist[w].display_name[--len] = L'\0';
                }
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
}

void feed_manager_move_entry(AsyncFeedManager* mgr, uint32_t entry_id, uint32_t target_section_id) {
    AcquireSRWLockExclusive(&mgr->data_lock);
    WatchlistSection* target_sec = NULL;
    for (size_t i = 0; i < mgr->section_count; i++) {
        if (mgr->sections[i].id == target_section_id) {
            target_sec = &mgr->sections[i];
            break;
        }
    }
    if (target_sec) {
        for (size_t w = 0; w < mgr->watchlist_count; w++) {
            if (!mgr->watchlist[w].removed && mgr->watchlist[w].id == entry_id) {
                mgr->watchlist[w].section_id = target_section_id;
                target_sec->collapsed = false;
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
}

void feed_manager_reorder_entry(
    AsyncFeedManager* mgr,
    uint32_t entry_id,
    uint32_t target_section_id,
    uint32_t target_entry_id,
    bool insert_after
) {
    AcquireSRWLockExclusive(&mgr->data_lock);

    WatchlistSection* target_sec = NULL;
    for (size_t i = 0; i < mgr->section_count; i++) {
        if (mgr->sections[i].id == target_section_id) {
            target_sec = &mgr->sections[i];
            break;
        }
    }

    size_t src_idx = mgr->watchlist_count;
    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        if (!mgr->watchlist[w].removed && mgr->watchlist[w].id == entry_id) {
            src_idx = w;
            break;
        }
    }

    if (target_sec && src_idx < mgr->watchlist_count) {
        WatchlistItem moving = mgr->watchlist[src_idx];
        moving.section_id = target_section_id;
        target_sec->collapsed = false;

        if (src_idx + 1 < mgr->watchlist_count) {
            memmove(
                &mgr->watchlist[src_idx],
                &mgr->watchlist[src_idx + 1],
                (mgr->watchlist_count - src_idx - 1) * sizeof(WatchlistItem)
            );
        }
        size_t reduced = mgr->watchlist_count - 1;

        size_t insert_idx = reduced;
        bool placed = false;
        if (target_entry_id != 0 && target_entry_id != entry_id) {
            for (size_t w = 0; w < reduced; w++) {
                if (!mgr->watchlist[w].removed && mgr->watchlist[w].id == target_entry_id) {
                    insert_idx = insert_after ? (w + 1) : w;
                    placed = true;
                    break;
                }
            }
        }
        if (!placed) {
            for (size_t w = reduced; w > 0; w--) {
                if (!mgr->watchlist[w - 1].removed && mgr->watchlist[w - 1].section_id == target_section_id) {
                    insert_idx = w;
                    break;
                }
            }
        }
        if (insert_idx > reduced) insert_idx = reduced;
        if (insert_idx < reduced) {
            memmove(
                &mgr->watchlist[insert_idx + 1],
                &mgr->watchlist[insert_idx],
                (reduced - insert_idx) * sizeof(WatchlistItem)
            );
        }
        mgr->watchlist[insert_idx] = moving;
    }

    ReleaseSRWLockExclusive(&mgr->data_lock);
}

bool feed_manager_is_primary_entry(AsyncFeedManager* mgr, uint32_t entry_id, const wchar_t* symbol) {
    if (!mgr || !symbol || !symbol[0]) return true;
    AcquireSRWLockShared(&mgr->data_lock);
    uint32_t first_id = 0;
    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        if (!mgr->watchlist[w].removed && wcscmp(mgr->watchlist[w].symbol, symbol) == 0) {
            first_id = mgr->watchlist[w].id;
            break;
        }
    }
    ReleaseSRWLockShared(&mgr->data_lock);
    if (first_id == 0 || entry_id == 0) return true;
    return first_id == entry_id;
}

void feed_manager_remove_entry(AsyncFeedManager* mgr, uint32_t entry_id) {
    bool resync = false;
    AcquireSRWLockExclusive(&mgr->data_lock);
    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        if (!mgr->watchlist[w].removed && mgr->watchlist[w].id == entry_id) {
            mgr->watchlist[w].removed = true;
            break;
        }
    }
    if (mgr->active_entry_id == entry_id) {
        mgr->active_entry_id = 0;
        for (size_t w = 0; w < mgr->watchlist_count; w++) {
            if (!mgr->watchlist[w].removed) {
                mgr->active_entry_id = mgr->watchlist[w].id;
                if (wcscmp(mgr->active_symbol, mgr->watchlist[w].symbol) != 0) {
                    ov_wcscpy(mgr->active_symbol, 24, mgr->watchlist[w].symbol);
                    for (int s = 0; s < SRC_COUNT; s++) {
                        build_seed_candles_locked(mgr, mgr->active_symbol, mgr->timeframe, (FeedSourceId)s);
                    }
                    resync = true;
                }
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
    if (resync) InterlockedIncrement(&mgr->resync_seq);
}

void feed_manager_remove_token(AsyncFeedManager* mgr, const wchar_t* symbol) {
    AcquireSRWLockExclusive(&mgr->data_lock);
    for (size_t w = 0; w < mgr->watchlist_count; w++) {
        if (!mgr->watchlist[w].removed && wcscmp(mgr->watchlist[w].symbol, symbol) == 0) {
            mgr->watchlist[w].removed = true;
            break;
        }
    }
    if (wcscmp(mgr->active_symbol, symbol) == 0) {
        mgr->active_entry_id = 0;
        for (size_t w = 0; w < mgr->watchlist_count; w++) {
            if (!mgr->watchlist[w].removed) {
                mgr->active_entry_id = mgr->watchlist[w].id;
                ov_wcscpy(mgr->active_symbol, 24, mgr->watchlist[w].symbol);
                for (int s = 0; s < SRC_COUNT; s++) {
                    build_seed_candles_locked(mgr, mgr->active_symbol, mgr->timeframe, (FeedSourceId)s);
                }
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
    InterlockedIncrement(&mgr->resync_seq);
}

uint32_t feed_manager_add_section(AsyncFeedManager* mgr, const wchar_t* name) {
    if (!name) return 0;
    const wchar_t* p = name;
    while (*p == L' ' || *p == L'\t') p++;
    if (!*p) return 0;

    uint32_t new_id = 0;
    AcquireSRWLockExclusive(&mgr->data_lock);
    if (mgr->section_count < MAX_WL_SECTIONS) {
        WatchlistSection* sec = &mgr->sections[mgr->section_count++];
        sec->id = mgr->next_section_id++;
        ov_wcscpy(sec->name, 48, p);
        size_t len = wcslen(sec->name);
        while (len > 0 && (sec->name[len - 1] == L' ' || sec->name[len - 1] == L'\t')) {
            sec->name[--len] = L'\0';
        }
        sec->collapsed = false;
        new_id = sec->id;
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
    return new_id;
}

void feed_manager_rename_section(AsyncFeedManager* mgr, uint32_t section_id, const wchar_t* name) {
    if (!name) return;
    const wchar_t* p = name;
    while (*p == L' ' || *p == L'\t') p++;
    if (!*p) return;

    AcquireSRWLockExclusive(&mgr->data_lock);
    for (size_t i = 0; i < mgr->section_count; i++) {
        if (mgr->sections[i].id == section_id) {
            ov_wcscpy(mgr->sections[i].name, 48, p);
            size_t len = wcslen(mgr->sections[i].name);
            while (len > 0 && (mgr->sections[i].name[len - 1] == L' ' || mgr->sections[i].name[len - 1] == L'\t')) {
                mgr->sections[i].name[--len] = L'\0';
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
}

void feed_manager_toggle_section(AsyncFeedManager* mgr, uint32_t section_id) {
    AcquireSRWLockExclusive(&mgr->data_lock);
    for (size_t i = 0; i < mgr->section_count; i++) {
        if (mgr->sections[i].id == section_id) {
            mgr->sections[i].collapsed = !mgr->sections[i].collapsed;
            break;
        }
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
}

void feed_manager_reorder_section(
    AsyncFeedManager* mgr,
    uint32_t section_id,
    uint32_t target_section_id,
    bool insert_after
) {
    if (!mgr || section_id == 0 || target_section_id == 0 || section_id == target_section_id) return;
    AcquireSRWLockExclusive(&mgr->data_lock);
    size_t src_idx = mgr->section_count;
    for (size_t i = 0; i < mgr->section_count; i++) {
        if (mgr->sections[i].id == section_id) {
            src_idx = i;
            break;
        }
    }
    if (src_idx < mgr->section_count) {
        WatchlistSection moving = mgr->sections[src_idx];
        if (src_idx + 1 < mgr->section_count) {
            memmove(
                &mgr->sections[src_idx],
                &mgr->sections[src_idx + 1],
                (mgr->section_count - src_idx - 1) * sizeof(WatchlistSection)
            );
        }
        size_t reduced = mgr->section_count - 1;
        size_t dst_idx = reduced;
        for (size_t i = 0; i < reduced; i++) {
            if (mgr->sections[i].id == target_section_id) {
                dst_idx = insert_after ? (i + 1) : i;
                break;
            }
        }
        if (dst_idx > reduced) dst_idx = reduced;
        if (dst_idx < reduced) {
            memmove(
                &mgr->sections[dst_idx + 1],
                &mgr->sections[dst_idx],
                (reduced - dst_idx) * sizeof(WatchlistSection)
            );
        }
        mgr->sections[dst_idx] = moving;
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
}

void feed_manager_delete_section(AsyncFeedManager* mgr, uint32_t section_id) {
    AcquireSRWLockExclusive(&mgr->data_lock);
    if (mgr->section_count > 1) {
        size_t del_idx = mgr->section_count;
        for (size_t i = 0; i < mgr->section_count; i++) {
            if (mgr->sections[i].id == section_id) {
                del_idx = i;
                break;
            }
        }
        if (del_idx < mgr->section_count) {
            uint32_t fallback_id = (del_idx == 0) ? mgr->sections[1].id : mgr->sections[0].id;
            for (size_t w = 0; w < mgr->watchlist_count; w++) {
                if (!mgr->watchlist[w].removed && mgr->watchlist[w].section_id == section_id) {
                    mgr->watchlist[w].section_id = fallback_id;
                }
            }
            if (del_idx + 1 < mgr->section_count) {
                memmove(
                    &mgr->sections[del_idx],
                    &mgr->sections[del_idx + 1],
                    (mgr->section_count - del_idx - 1) * sizeof(WatchlistSection)
                );
            }
            mgr->section_count--;
        }
    }
    ReleaseSRWLockExclusive(&mgr->data_lock);
}

size_t feed_manager_copy_candles(
    AsyncFeedManager* mgr,
    FeedSourceId src,
    CandleBar* out_bars,
    size_t max_bars
) {
    AcquireSRWLockShared(&mgr->data_lock);
    size_t n = mgr->candle_counts[src];
    if (n > max_bars) n = max_bars;
    memcpy(out_bars, mgr->candles[src], n * sizeof(CandleBar));
    ReleaseSRWLockShared(&mgr->data_lock);
    return n;
}

size_t feed_manager_copy_watchlist(
    AsyncFeedManager* mgr,
    WatchlistItem* out_items,
    size_t max_items
) {
    AcquireSRWLockShared(&mgr->data_lock);
    size_t n = mgr->watchlist_count;
    if (n > max_items) n = max_items;
    memcpy(out_items, mgr->watchlist, n * sizeof(WatchlistItem));
    ReleaseSRWLockShared(&mgr->data_lock);
    return n;
}

size_t feed_manager_copy_sections(
    AsyncFeedManager* mgr,
    WatchlistSection* out_sections,
    size_t max_sections
) {
    AcquireSRWLockShared(&mgr->data_lock);
    size_t n = mgr->section_count;
    if (n > max_sections) n = max_sections;
    memcpy(out_sections, mgr->sections, n * sizeof(WatchlistSection));
    ReleaseSRWLockShared(&mgr->data_lock);
    return n;
}

void feed_manager_copy_sources(
    AsyncFeedManager* mgr,
    SourceStatusInfo out_sources[SRC_COUNT]
) {
    AcquireSRWLockShared(&mgr->data_lock);
    memcpy(out_sources, mgr->sources, sizeof(SourceStatusInfo) * SRC_COUNT);
    ReleaseSRWLockShared(&mgr->data_lock);
}
