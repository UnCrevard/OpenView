#ifndef OV_COMMON_H
#define OV_COMMON_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

static inline void ov_wcscpy(wchar_t* dst, size_t cap, const wchar_t* src) {
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = L'\0';
        return;
    }
    wcsncpy(dst, src, cap - 1);
    dst[cap - 1] = L'\0';
}

#define MAX_CANDLES         2048
#define MAX_WATCHLIST       64
#define MAX_WL_SECTIONS     16
#define MAX_DRAWINGS        256
#define MAX_ALERTS          64
#define MAX_ALERT_CONDS     8
#define MAX_ALERT_LOGS      128
#define RING_CAPACITY       1024

#define WM_APP_FEED_TICK      (WM_APP + 101)
#define WM_APP_SOURCE_STATE   (WM_APP + 102)
#define WM_APP_ALERT_FIRED    (WM_APP + 103)
#define WM_APP_SNAPSHOT_READY (WM_APP + 104)

typedef enum {
    SRC_BINANCE  = 0,
    SRC_COINBASE = 1,
    SRC_KRAKEN   = 2,
    SRC_COUNT    = 3
} FeedSourceId;

typedef enum {
    TF_1M  = 60,
    TF_5M  = 300,
    TF_15M = 900,
    TF_1H  = 3600,
    TF_4H  = 14400,
    TF_1D  = 86400,
    TF_1W  = 604800,
    TF_1MO = 2592000,
    TF_1Y  = 31536000
} TimeframeSec;

typedef struct {
    int64_t      time_utc;
    double       open;
    double       high;
    double       low;
    double       close;
    double       volume;
    FeedSourceId source;
} CandleBar;

typedef struct {
    uint32_t id;
    wchar_t  name[48];
    bool     collapsed;
} WatchlistSection;

typedef struct {
    uint32_t id;
    uint32_t section_id;
    wchar_t  symbol[24];
    wchar_t  name[48];
    wchar_t  display_name[48];
    double   price;
    double   open_24h;
    double   high_24h;
    double   low_24h;
    double   volume_24h;
    double   change_pct;
    double   source_prices[SRC_COUNT];
    int64_t  last_update_ms;
    bool     removed;
} WatchlistItem;

typedef struct {
    wchar_t      symbol[24];
    FeedSourceId source;
    double       price;
    double       volume;
    int64_t      timestamp_ms;
} FeedTick;

typedef enum {
    STATE_OFFLINE = 0,
    STATE_SYNCING,
    STATE_LIVE,
    STATE_FALLBACK
} SourceConnState;

typedef struct {
    FeedSourceId    id;
    wchar_t         label[24];
    wchar_t         endpoint[64];
    SourceConnState state;
    uint32_t        latency_ms;
    uint64_t        ticks_received;
} SourceStatusInfo;

static inline const wchar_t* ov_tf_label(TimeframeSec tf) {
    switch (tf) {
        case TF_1M:  return L"1m";
        case TF_5M:  return L"5m";
        case TF_15M: return L"15m";
        case TF_1H:  return L"1h";
        case TF_4H:  return L"4h";
        case TF_1D:  return L"1d";
        case TF_1W:  return L"1S";
        case TF_1MO: return L"1M";
        case TF_1Y:  return L"1A";
        default:     return L"15m";
    }
}

static inline const wchar_t* ov_tf_code(TimeframeSec tf) {
    switch (tf) {
        case TF_1M:  return L"1m";
        case TF_5M:  return L"5m";
        case TF_15M: return L"15m";
        case TF_1H:  return L"1h";
        case TF_4H:  return L"4h";
        case TF_1D:  return L"1d";
        case TF_1W:  return L"1w";
        case TF_1MO: return L"1M";
        case TF_1Y:  return L"1y";
        default:     return L"15m";
    }
}

#endif /* OV_COMMON_H */
