#include "export.h"
#include <stdio.h>
#include <string.h>

static void write_str(HANDLE h, const char* s) {
    DWORD written = 0;
    WriteFile(h, s, (DWORD)strlen(s), &written, NULL);
}

static void wcs_to_utf8(const wchar_t* w, char* out, int out_cap) {
    WideCharToMultiByte(CP_UTF8, 0, w ? w : L"", -1, out, out_cap, NULL, NULL);
}

static void write_json_string(HANDLE h, const wchar_t* w) {
    char utf8[256];
    wcs_to_utf8(w, utf8, sizeof(utf8));
    write_str(h, "\"");
    for (char* p = utf8; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (*p == '"' || *p == '\\') {
            char esc[3] = { '\\', *p, '\0' };
            write_str(h, esc);
        } else if (c < 0x20) {
            char esc[7];
            snprintf(esc, sizeof(esc), "\\u%04X", c);
            write_str(h, esc);
        } else {
            DWORD written = 0;
            WriteFile(h, p, 1, &written, NULL);
        }
    }
    write_str(h, "\"");
}

bool export_watchlist_state_json_w(
    const wchar_t* file_path,
    const WatchlistSection* sections,
    size_t section_count,
    const WatchlistItem* watchlist,
    size_t watchlist_count,
    const DrawingStore* drawings
) {
    HANDLE h = CreateFileW(
        file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (h == INVALID_HANDLE_VALUE) return false;

    char buf[512];
    write_str(h, "{\n  \"sections\": [\n");
    for (size_t i = 0; i < section_count; i++) {
        write_str(h, "    {\"id\": ");
        snprintf(buf, sizeof(buf), "%u, \"name\": ", (unsigned)sections[i].id);
        write_str(h, buf);
        write_json_string(h, sections[i].name);
        snprintf(
            buf, sizeof(buf),
            ", \"collapsed\": %s}%s\n",
            sections[i].collapsed ? "true" : "false",
            (i + 1 < section_count) ? "," : ""
        );
        write_str(h, buf);
    }

    write_str(h, "  ],\n  \"tokens\": [\n");
    bool first_token = true;
    for (size_t i = 0; i < watchlist_count; i++) {
        if (watchlist[i].removed) continue;
        if (!first_token) write_str(h, ",\n");
        first_token = false;

        write_str(h, "    {\"id\": ");
        snprintf(
            buf, sizeof(buf),
            "%u, \"sectionId\": %u, \"symbol\": ",
            (unsigned)watchlist[i].id,
            (unsigned)watchlist[i].section_id
        );
        write_str(h, buf);
        write_json_string(h, watchlist[i].symbol);
        write_str(h, ", \"name\": ");
        write_json_string(h, watchlist[i].name);
        write_str(h, ", \"displayName\": ");
        write_json_string(h, watchlist[i].display_name);
        write_str(h, ", \"providerId\": ");
        write_json_string(h, watchlist[i].provider_id);

        write_str(h, ", \"drawings\": [");
        bool first_drawing = true;
        if (drawings) {
            bool first_matching_token = true;
            for (size_t j = 0; j < i; j++) {
                if (!watchlist[j].removed &&
                    wcscmp(watchlist[j].symbol, watchlist[i].symbol) == 0) {
                    first_matching_token = false;
                    break;
                }
            }
            for (size_t d = 0; d < drawings->count; d++) {
                const ChartDrawing* dr = &drawings->items[d];
                bool belongs_to_entry = dr->entry_id == watchlist[i].id;
                bool belongs_to_primary_symbol =
                    dr->entry_id == 0 &&
                    first_matching_token &&
                    wcscmp(dr->symbol, watchlist[i].symbol) == 0;
                if (!belongs_to_entry && !belongs_to_primary_symbol) continue;
                if (!first_drawing) write_str(h, ", ");
                first_drawing = false;

                snprintf(
                    buf, sizeof(buf),
                    "{\"id\":%u,\"kind\":%d,\"color\":%u,\"width\":%.2f,\"style\":%d,"
                    "\"locked\":%s,\"label\":",
                    (unsigned)dr->id, (int)dr->kind, (unsigned)dr->argb_color,
                    dr->stroke_width, (int)dr->line_style,
                    dr->locked ? "true" : "false"
                );
                write_str(h, buf);
                write_json_string(h, dr->label);
                write_str(h, ",\"pts\":[");
                for (size_t p = 0; p < dr->pt_count; p++) {
                    snprintf(
                        buf, sizeof(buf),
                        "{\"t\":%lld,\"p\":%.8f}%s",
                        (long long)dr->pts[p].time_utc,
                        dr->pts[p].price,
                        (p + 1 < dr->pt_count) ? "," : ""
                    );
                    write_str(h, buf);
                }
                write_str(h, "]");
                write_str(h, "}");
            }
        }
        write_str(h, "]}\n");
    }
    write_str(h, "  ]\n}\n");

    CloseHandle(h);
    return true;
}

static const char* source_name_utf8(FeedSourceId s) {
    switch (s) {
        case SRC_BINANCE:  return "binance";
        case SRC_COINBASE: return "coinbase";
        case SRC_KRAKEN:   return "kraken";
        case SRC_MEXC:     return "mexc";
        case SRC_COINGECKO: return "coingecko";
        default:           return "binance";
    }
}

bool export_candles_csv_w(
    const wchar_t* file_path,
    const wchar_t* symbol,
    TimeframeSec timeframe,
    FeedSourceId source,
    const CandleBar* bars,
    size_t count,
    const IndicatorSettings* cfg,
    const IndicatorBuffers* ind
) {
    HANDLE h = CreateFileW(
        file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (h == INVALID_HANDLE_VALUE) return false;

    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    DWORD written = 0;
    WriteFile(h, bom, 3, &written, NULL);

    char header[512];
    snprintf(
        header, sizeof(header),
        "timestamp_utc,symbol,timeframe,source,open,high,low,close,volume"
        "%s%s%s%s%s%s\r\n",
        cfg->enable_ema_fast ? ",ema_fast" : "",
        cfg->enable_ema_slow ? ",ema_slow" : "",
        cfg->enable_sma      ? ",sma" : "",
        cfg->enable_bb       ? ",bb_upper,bb_mid,bb_lower" : "",
        cfg->enable_rsi      ? ",rsi" : "",
        cfg->enable_macd     ? ",macd,macd_signal,macd_hist" : ""
    );
    write_str(h, header);

    char sym_utf8[64], tf_utf8[16];
    wcs_to_utf8(symbol, sym_utf8, sizeof(sym_utf8));
    wcs_to_utf8(ov_tf_code(timeframe), tf_utf8, sizeof(tf_utf8));
    const char* src_utf8 = source_name_utf8(source);

    char line[768];
    for (size_t i = 0; i < count; i++) {
        const CandleBar* b = &bars[i];
        int pos = snprintf(
            line, sizeof(line),
            "%lld,%s,%s,%s,%.6f,%.6f,%.6f,%.6f,%.4f",
            (long long)b->time_utc, sym_utf8, tf_utf8, src_utf8,
            b->open, b->high, b->low, b->close, b->volume
        );
        if (cfg->enable_ema_fast && pos > 0) {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, ",%.6f",
                            ind->valid_ema_fast[i] ? ind->ema_fast[i] : 0.0);
        }
        if (cfg->enable_ema_slow && pos > 0) {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, ",%.6f",
                            ind->valid_ema_slow[i] ? ind->ema_slow[i] : 0.0);
        }
        if (cfg->enable_sma && pos > 0) {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, ",%.6f",
                            ind->valid_sma[i] ? ind->sma[i] : 0.0);
        }
        if (cfg->enable_bb && pos > 0) {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, ",%.6f,%.6f,%.6f",
                            ind->valid_bb[i] ? ind->bb_upper[i] : 0.0,
                            ind->valid_bb[i] ? ind->bb_mid[i] : 0.0,
                            ind->valid_bb[i] ? ind->bb_lower[i] : 0.0);
        }
        if (cfg->enable_rsi && pos > 0) {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, ",%.2f",
                            ind->valid_rsi[i] ? ind->rsi[i] : 0.0);
        }
        if (cfg->enable_macd && pos > 0) {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, ",%.6f,%.6f,%.6f",
                            ind->valid_macd[i] ? ind->macd_line[i] : 0.0,
                            ind->valid_macd[i] ? ind->macd_signal[i] : 0.0,
                            ind->valid_macd[i] ? ind->macd_hist[i] : 0.0);
        }
        if (pos > 0 && (size_t)pos + 3 < sizeof(line)) {
            strcat(line + pos, "\r\n");
        }
        write_str(h, line);
    }

    CloseHandle(h);
    return true;
}

bool export_workspace_json_w(
    const wchar_t* file_path,
    uint32_t entry_id,
    bool is_primary_entry,
    const wchar_t* symbol,
    TimeframeSec timeframe,
    FeedSourceId source,
    const CandleBar* bars,
    size_t count,
    const DrawingStore* drawings,
    const AlertEngine* alerts
) {
    HANDLE h = CreateFileW(
        file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (h == INVALID_HANDLE_VALUE) return false;

    char sym_utf8[64], tf_utf8[16];
    wcs_to_utf8(symbol, sym_utf8, sizeof(sym_utf8));
    wcs_to_utf8(ov_tf_code(timeframe), tf_utf8, sizeof(tf_utf8));

    char buf[512];
    snprintf(
        buf, sizeof(buf),
        "{\n  \"symbol\": \"%s\",\n  \"timeframe\": \"%s\",\n  \"primarySource\": \"%s\",\n  \"candleCount\": %zu,\n  \"candles\": [\n",
        sym_utf8, tf_utf8, source_name_utf8(source), count
    );
    write_str(h, buf);

    for (size_t i = 0; i < count; i++) {
        const CandleBar* b = &bars[i];
        snprintf(
            buf, sizeof(buf),
            "    {\"time\": %lld, \"open\": %.6f, \"high\": %.6f, \"low\": %.6f, \"close\": %.6f, \"volume\": %.4f}%s\n",
            (long long)b->time_utc,
            b->open, b->high, b->low, b->close, b->volume,
            (i + 1 < count) ? "," : ""
        );
        write_str(h, buf);
    }

    write_str(h, "  ],\n  \"drawings\": [\n");
    bool first_drw = true;
    for (size_t d = 0; d < drawings->count; d++) {
        const ChartDrawing* dr = &drawings->items[d];
        if (!drawings_matches_scope(dr, entry_id, symbol, is_primary_entry)) continue;
        if (!first_drw) write_str(h, ",\n");
        first_drw = false;
        char lbl_utf8[128];
        wcs_to_utf8(dr->label, lbl_utf8, sizeof(lbl_utf8));
        snprintf(
            buf, sizeof(buf),
            "    {\"id\": %u, \"entryId\": %u, \"kind\": %d, \"color\": \"#%06X\", \"lineWidth\": %.1f, \"lineStyle\": %d, \"locked\": %s, \"p0_time\": %lld, \"p0_price\": %.6f, \"label\": \"%s\"}",
            dr->id, dr->entry_id, (int)dr->kind, (unsigned)(dr->argb_color & 0xFFFFFFU),
            dr->stroke_width, (int)dr->line_style, dr->locked ? "true" : "false",
            (long long)dr->pts[0].time_utc, dr->pts[0].price, lbl_utf8
        );
        write_str(h, buf);
    }
    write_str(h, "\n  ],\n  \"alertRulesCount\": ");
    snprintf(buf, sizeof(buf), "%zu\n}\n", alerts ? alerts->rule_count : 0);
    write_str(h, buf);

    CloseHandle(h);
    return true;
}
