#include "common.h"
#include "feed_manager.h"
#include "indicators.h"
#include "drawings.h"
#include "alerts.h"
#include "export.h"
#include "d2d_renderer.h"
#include "ui_dialogs.h"
#include <dwmapi.h>
#include <objbase.h>
#include <windowsx.h>
#include <ctype.h>
#include <wctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

typedef int  (WINAPI *PFN_SetPreferredAppMode)(int);
typedef void (WINAPI *PFN_FlushMenuThemes)(void);
typedef BOOL (WINAPI *PFN_AllowDarkModeForWindow)(HWND, BOOL);

typedef struct {
    DWORD dwSize;
    DWORD dwICC;
} OV_INITCOMMONCONTROLSEX;
typedef BOOL (WINAPI *PFN_InitCommonControlsEx)(const OV_INITCOMMONCONTROLSEX*);

static void ov_apply_uxtheme_app_mode(HWND hwnd, bool dark) {
    HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
    if (!hUx) return;
    FARPROC fp_mode  = GetProcAddress(hUx, (LPCSTR)(ULONG_PTR)135);
    FARPROC fp_flush = GetProcAddress(hUx, (LPCSTR)(ULONG_PTR)136);
    FARPROC fp_allow = GetProcAddress(hUx, (LPCSTR)(ULONG_PTR)133);
    if (fp_mode) {
        PFN_SetPreferredAppMode pfnMode = NULL;
        memcpy(&pfnMode, &fp_mode, sizeof(pfnMode));
        pfnMode(dark ? 2 : 3); /* ForceDark : ForceLight */
    }
    if (fp_flush) {
        PFN_FlushMenuThemes pfnFlush = NULL;
        memcpy(&pfnFlush, &fp_flush, sizeof(pfnFlush));
        pfnFlush();
    }
    if (hwnd && fp_allow) {
        PFN_AllowDarkModeForWindow pfnAllow = NULL;
        memcpy(&pfnAllow, &fp_allow, sizeof(pfnAllow));
        pfnAllow(hwnd, dark ? TRUE : FALSE);
    }
    FreeLibrary(hUx);
}

static HANDLE g_actctx = INVALID_HANDLE_VALUE;
static ULONG_PTR g_actctx_cookie = 0;

static void ov_enable_modern_windows_ui(bool dark) {
    wchar_t tmp_dir[MAX_PATH] = L"";
    wchar_t mf_path[MAX_PATH] = L"";
    if (GetTempPathW(MAX_PATH, tmp_dir) > 0) {
        swprintf(mf_path, MAX_PATH, L"%lsopenview_v6.manifest", tmp_dir);
        static const char kManifestXml[] =
            "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
            "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\r\n"
            "  <dependency>\r\n"
            "    <dependentAssembly>\r\n"
            "      <assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" "
            "version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>\r\n"
            "    </dependentAssembly>\r\n"
            "  </dependency>\r\n"
            "</assembly>\r\n";
        HANDLE hm = CreateFileW(mf_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hm != INVALID_HANDLE_VALUE) {
            DWORD wr = 0;
            WriteFile(hm, kManifestXml, (DWORD)(sizeof(kManifestXml) - 1), &wr, NULL);
            CloseHandle(hm);

            ACTCTXW act;
            ZeroMemory(&act, sizeof(act));
            act.cbSize   = sizeof(act);
            act.lpSource = mf_path;
            g_actctx = CreateActCtxW(&act);
            if (g_actctx != INVALID_HANDLE_VALUE) {
                ActivateActCtx(g_actctx, &g_actctx_cookie);
            }
        }
    }

    HMODULE hCc = LoadLibraryW(L"comctl32.dll");
    if (hCc) {
        FARPROC fp = GetProcAddress(hCc, "InitCommonControlsEx");
        if (fp) {
            PFN_InitCommonControlsEx pfn = NULL;
            memcpy(&pfn, &fp, sizeof(pfn));
            OV_INITCOMMONCONTROLSEX icc = { sizeof(icc), 0x000040FFU }; /* ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES */
            pfn(&icc);
        }
        FreeLibrary(hCc);
    }

    ov_apply_uxtheme_app_mode(NULL, dark);
}

#define ID_TOAST_TIMER 1
#define IDM_SYM_BASE   3000
#define IDM_DRAWING_BASE 4000

typedef enum {
    DRAG_NONE = 0,
    DRAG_PAN,
    DRAG_Y_SCALE,
    DRAG_DRAWING_PT,
    DRAG_WL_ENTRY,
    DRAG_WL_SECTION
} DragMode;

typedef struct {
    uint32_t color;
    float width;
    DrawingLineStyle line_style;
    DrawingLabelAlignH align_h;
    DrawingLabelAlignV align_v;
    wchar_t label[64];
} DrawingStyleDefaults;

typedef struct {
    AsyncFeedManager   feed;
    D2DRenderer        renderer;
    IndicatorSettings  ind_cfg;
    IndicatorBuffers   ind_buf;
    DrawingStore       drawings;
    DrawingStyleDefaults drawing_defaults[TOOL_TEXT + 1];
    AlertEngine        alerts;
    ChartViewportState viewport;
    DragMode           drag_mode;
    float              drag_start_x_dip;
    float              drag_start_y_dip;
    float              drag_start_pan;
    float              drag_start_y_scale;
    float              drag_start_y_pan;
    uint32_t           drag_drawing_id;
    int                drag_point_idx;
} AppState;

static float get_dpi_scale(HWND hwnd) {
    UINT dpi = GetDpiForWindow(hwnd);
    return (dpi > 0) ? ((float)dpi / 96.0f) : 1.0f;
}

static bool pt_in(D2D1_RECT_F rc, float x, float y) {
    return (rc.right > rc.left) && (rc.bottom > rc.top) &&
           (x >= rc.left && x <= rc.right && y >= rc.top && y <= rc.bottom);
}

static void update_titlebar_theme(HWND hwnd, bool dark) {
    BOOL d = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &d, sizeof(d));
    DWORD corner = 2; /* DWMWCP_ROUND */
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    ov_apply_uxtheme_app_mode(hwnd, dark);
}

static void persist_drawings(const AppState* app) {
    wchar_t drawings_path[MAX_PATH];
    wchar_t watchlist_path[MAX_PATH];
    if (ov_state_path(drawings_path, MAX_PATH, L"openview_drawings.json")) {
        drawings_save_file_w(&app->drawings, drawings_path);
    }
    WatchlistSection sections[MAX_WL_SECTIONS];
    WatchlistItem watchlist[MAX_WATCHLIST];
    size_t section_count = feed_manager_copy_sections(
        (AsyncFeedManager*)&app->feed, sections, MAX_WL_SECTIONS
    );
    size_t watchlist_count = feed_manager_copy_watchlist(
        (AsyncFeedManager*)&app->feed, watchlist, MAX_WATCHLIST
    );
    if (ov_state_path(watchlist_path, MAX_PATH, L"openview_watchlist.json")) {
        export_watchlist_state_json_w(
            watchlist_path,
            sections,
            section_count,
            watchlist,
            watchlist_count,
            &app->drawings
        );
    }
}

static bool drawing_label_to_hex(const wchar_t* label, char* out, size_t cap) {
    size_t pos = 0;
    for (size_t i = 0; label[i]; i++) {
        int n = snprintf(out + pos, cap - pos, "%04X", (unsigned)(uint16_t)label[i]);
        if (n != 4 || (size_t)n >= cap - pos) return false;
        pos += (size_t)n;
    }
    if (pos >= cap) return false;
    out[pos] = '\0';
    return true;
}

typedef struct {
    TimeframeSec timeframe;
    FeedSourceId primary_source;
    IndicatorSettings indicators;
} AppSettings;

static void persist_settings(
    TimeframeSec timeframe,
    FeedSourceId primary_source,
    const IndicatorSettings* indicators,
    const DrawingStyleDefaults* drawing_defaults
) {
    char json[4096];
    int len = snprintf(
        json, sizeof(json),
        "{\"timeframe\":%d,\"primary_source\":%d,"
        "\"enable_ema_fast\":%d,\"enable_ema_slow\":%d,"
        "\"enable_sma\":%d,\"enable_bb\":%d,\"enable_rsi\":%d,\"enable_macd\":%d,"
        "\"ema_fast_period\":%d,\"ema_slow_period\":%d,\"sma_period\":%d,"
        "\"bb_period\":%d,\"bb_stddev\":%.6f,\"rsi_period\":%d,"
        "\"macd_fast\":%d,\"macd_slow\":%d,\"macd_signal\":%d",
        (int)timeframe, (int)primary_source,
        indicators->enable_ema_fast, indicators->enable_ema_slow,
        indicators->enable_sma, indicators->enable_bb,
        indicators->enable_rsi, indicators->enable_macd,
        indicators->ema_fast_period, indicators->ema_slow_period,
        indicators->sma_period, indicators->bb_period, indicators->bb_stddev,
        indicators->rsi_period, indicators->macd_fast, indicators->macd_slow,
        indicators->macd_signal
    );
    if (len <= 0 || (size_t)len >= sizeof(json)) return;
    size_t pos = (size_t)len;
    for (int kind = TOOL_TRENDLINE; kind <= TOOL_TEXT; kind++) {
        const DrawingStyleDefaults* style = &drawing_defaults[kind];
        char label_hex[260];
        if (!drawing_label_to_hex(style->label, label_hex, sizeof(label_hex))) return;
        int n = snprintf(
            json + pos, sizeof(json) - pos,
            ",\"drawing_%d_color\":%u,\"drawing_%d_width\":%.3f,"
            "\"drawing_%d_line_style\":%d,\"drawing_%d_align_h\":%d,"
            "\"drawing_%d_align_v\":%d,\"drawing_%d_label_utf16_hex\":\"%s\"",
            kind, style->color, kind, (double)style->width,
            kind, (int)style->line_style, kind, (int)style->align_h,
            kind, (int)style->align_v, kind, label_hex
        );
        if (n <= 0 || (size_t)n >= sizeof(json) - pos) return;
        pos += (size_t)n;
    }

    len = snprintf(json + pos, sizeof(json) - pos, "}\n");
    if (len <= 0 || (size_t)len >= sizeof(json) - pos) return;
    pos += (size_t)len;

    wchar_t path[MAX_PATH];
    if (!ov_state_path(path, MAX_PATH, L"openview_settings.json")) return;
    HANDLE h = CreateFileW(
        path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, json, (DWORD)pos, &written, NULL);
    CloseHandle(h);
}

static bool settings_read_long(const char* json, const char* key, long* out) {
    char field[64];
    int len = snprintf(field, sizeof(field), "\"%s\":", key);
    if (len <= 0 || (size_t)len >= sizeof(field)) return false;
    const char* value = strstr(json, field);
    if (!value) return false;
    value += len;
    char* end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value) return false;
    *out = parsed;
    return true;
}

static bool settings_read_double(const char* json, const char* key, double* out) {
    char field[64];
    int len = snprintf(field, sizeof(field), "\"%s\":", key);
    if (len <= 0 || (size_t)len >= sizeof(field)) return false;
    const char* value = strstr(json, field);
    if (!value) return false;
    value += len;
    char* end = NULL;
    double parsed = strtod(value, &end);
    if (end == value || !isfinite(parsed)) return false;
    *out = parsed;
    return true;
}

static bool settings_read_uint32(const char* json, const char* key, uint32_t* out) {
    char field[64];
    int len = snprintf(field, sizeof(field), "\"%s\":", key);
    if (len <= 0 || (size_t)len >= sizeof(field)) return false;
    const char* value = strstr(json, field);
    if (!value) return false;
    value += len;
    char* end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (end == value || parsed > UINT32_MAX) return false;
    *out = (uint32_t)parsed;
    return true;
}

static bool settings_read_drawing_label(
    const char* json,
    const char* key,
    wchar_t* out,
    size_t cap
) {
    char field[80];
    int len = snprintf(field, sizeof(field), "\"%s\":\"", key);
    if (len <= 0 || (size_t)len >= sizeof(field)) return false;
    const char* value = strstr(json, field);
    if (!value) return false;
    value += len;

    size_t count = 0;
    while (*value && *value != '"') {
        if (count + 1 >= cap) return false;
        if (strlen(value) < 4) return false;
        for (size_t i = 0; i < 4; i++) {
            if (!isxdigit((unsigned char)value[i])) return false;
        }
        char hex[5];
        memcpy(hex, value, 4);
        hex[4] = '\0';
        char* end = NULL;
        unsigned long code_unit = strtoul(hex, &end, 16);
        if (end != hex + 4 || code_unit > 0xFFFFUL) return false;
        out[count++] = (wchar_t)code_unit;
        value += 4;
    }
    if (*value != '"') return false;
    out[count] = L'\0';
    return true;
}

static void load_drawing_defaults(AppState* app) {
    wchar_t path[MAX_PATH];
    if (!ov_state_path(path, MAX_PATH, L"openview_settings.json")) return;
    HANDLE h = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (h == INVALID_HANDLE_VALUE) return;

    char json[4096];
    DWORD got = 0;
    BOOL ok = ReadFile(h, json, sizeof(json) - 1, &got, NULL);
    CloseHandle(h);
    if (!ok || got == 0) return;
    json[got] = '\0';

    for (int kind = TOOL_TRENDLINE; kind <= TOOL_TEXT; kind++) {
        DrawingStyleDefaults* style = &app->drawing_defaults[kind];
        char key[64];
        long value;
        uint32_t color;
        double width;

        snprintf(key, sizeof(key), "drawing_%d_color", kind);
        if (settings_read_uint32(json, key, &color)) style->color = color;

        snprintf(key, sizeof(key), "drawing_%d_width", kind);
        if (settings_read_double(json, key, &width) && width >= 0.2 && width <= 20.0) {
            style->width = (float)width;
        }

        snprintf(key, sizeof(key), "drawing_%d_line_style", kind);
        if (settings_read_long(json, key, &value) && value >= LINE_SOLID && value <= LINE_DOTTED) {
            style->line_style = (DrawingLineStyle)value;
        }

        snprintf(key, sizeof(key), "drawing_%d_align_h", kind);
        if (settings_read_long(json, key, &value) && value >= ALIGN_H_LEFT && value <= ALIGN_H_RIGHT) {
            style->align_h = (DrawingLabelAlignH)value;
        }

        snprintf(key, sizeof(key), "drawing_%d_align_v", kind);
        if (settings_read_long(json, key, &value) && value >= ALIGN_V_TOP && value <= ALIGN_V_BOTTOM) {
            style->align_v = (DrawingLabelAlignV)value;
        }

        snprintf(key, sizeof(key), "drawing_%d_label_utf16_hex", kind);
        settings_read_drawing_label(json, key, style->label, sizeof(style->label) / sizeof(style->label[0]));
    }
}

static AppSettings load_settings(void) {
    AppSettings settings = {0};
    settings.timeframe = TF_15M;
    settings.primary_source = SRC_BINANCE;
    indicators_default_settings(&settings.indicators);
    wchar_t path[MAX_PATH];
    if (!ov_state_path(path, MAX_PATH, L"openview_settings.json")) return settings;
    HANDLE h = CreateFileW(
        path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (h == INVALID_HANDLE_VALUE) return settings;

    char buf[4096];
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, sizeof(buf) - 1, &got, NULL);
    CloseHandle(h);
    if (!ok || got == 0) return settings;
    buf[got] = '\0';

    const char* value = strstr(buf, "\"timeframe\":");
    if (value) {
        long tf = strtol(value + strlen("\"timeframe\":"), NULL, 10);
        switch (tf) {
            case TF_1M: case TF_5M: case TF_15M: case TF_1H: case TF_4H:
            case TF_1D: case TF_1W: case TF_1MO: case TF_1Y:
                settings.timeframe = (TimeframeSec)tf;
                break;
            default:
                break;
        }
    }

    value = strstr(buf, "\"primary_source\":");
    if (value) {
        long source = strtol(value + strlen("\"primary_source\":"), NULL, 10);
        if (source >= SRC_BINANCE && source < SRC_COUNT) {
            settings.primary_source = (FeedSourceId)source;
        }
    }

    long number;
#define LOAD_INDICATOR_BOOL(field) \
    do { \
        if (settings_read_long(buf, #field, &number)) settings.indicators.field = number != 0; \
    } while (0)
#define LOAD_INDICATOR_PERIOD(field) \
    do { \
        if (settings_read_long(buf, #field, &number) && number >= 2 && number <= 10000) \
            settings.indicators.field = (int)number; \
    } while (0)
    LOAD_INDICATOR_BOOL(enable_ema_fast);
    LOAD_INDICATOR_BOOL(enable_ema_slow);
    LOAD_INDICATOR_BOOL(enable_sma);
    LOAD_INDICATOR_BOOL(enable_bb);
    LOAD_INDICATOR_BOOL(enable_rsi);
    LOAD_INDICATOR_BOOL(enable_macd);
    LOAD_INDICATOR_PERIOD(ema_fast_period);
    LOAD_INDICATOR_PERIOD(ema_slow_period);
    LOAD_INDICATOR_PERIOD(sma_period);
    LOAD_INDICATOR_PERIOD(bb_period);
    LOAD_INDICATOR_PERIOD(rsi_period);
    LOAD_INDICATOR_PERIOD(macd_fast);
    LOAD_INDICATOR_PERIOD(macd_slow);
    LOAD_INDICATOR_PERIOD(macd_signal);
#undef LOAD_INDICATOR_BOOL
#undef LOAD_INDICATOR_PERIOD
    double bb_stddev;
    if (settings_read_double(buf, "bb_stddev", &bb_stddev) &&
        bb_stddev > 0.0 && bb_stddev <= 100.0) {
        settings.indicators.bb_stddev = bb_stddev;
    }
    return settings;
}

static uint32_t add_drawing_with_defaults(
    AppState* app,
    DrawingToolKind kind,
    const AnchorPoint* points,
    size_t point_count,
    const wchar_t* label
) {
    DrawingStyleDefaults* style = &app->drawing_defaults[kind];
    uint32_t id = drawings_add(
        &app->drawings,
        app->feed.active_entry_id,
        app->feed.active_symbol,
        kind,
        points,
        point_count,
        style->color,
        style->width,
        style->line_style,
        label ? label : style->label
    );
    ChartDrawing* drawing = drawings_find_by_id(&app->drawings, id);
    if (drawing) {
        drawing->label_align_h = style->align_h;
        drawing->label_align_v = style->align_v;
        drawing->locked = true;
    }
    return id;
}

static bool is_active_entry_primary(AppState* app) {
    return feed_manager_is_primary_entry(&app->feed, app->feed.active_entry_id, app->feed.active_symbol);
}

static void select_watchlist_entry(AppState* app, uint32_t entry_id) {
    feed_manager_set_active_entry(&app->feed, entry_id);
    FeedSourceId source = app->feed.active_provider_id[0]
        ? SRC_COINGECKO
        : (app->feed.primary_source == SRC_COINGECKO ? SRC_BINANCE : app->feed.primary_source);
    if (source != app->feed.primary_source) {
        feed_manager_set_primary_source(&app->feed, source);
        app->viewport.last_primary_tick_ms = 0;
    }
}

static void refresh_analytics(AppState* app, HWND hwnd) {
    CandleBar bars[MAX_CANDLES];
    WatchlistItem wl[MAX_WATCHLIST];
    size_t n  = feed_manager_copy_candles(&app->feed, app->feed.primary_source, bars, MAX_CANDLES);
    size_t wn = feed_manager_copy_watchlist(&app->feed, wl, MAX_WATCHLIST);

    const WatchlistItem* active_tok = NULL;
    for (size_t i = 0; i < wn; i++) {
        if (!wl[i].removed &&
            (wl[i].id == app->feed.active_entry_id ||
             (app->feed.active_entry_id == 0 && wcscmp(wl[i].symbol, app->feed.active_symbol) == 0))) {
            active_tok = &wl[i];
            break;
        }
    }

    indicators_compute_all(bars, n, &app->ind_cfg, &app->ind_buf);
    alerts_evaluate(
        &app->alerts,
        app->feed.active_symbol,
        bars,
        n,
        &app->ind_buf,
        active_tok,
        (int64_t)GetTickCount64(),
        hwnd
    );
}

#define OV_OFN_OVERWRITEPROMPT 0x00000002U
#define OV_OFN_PATHMUSTEXIST   0x00000800U

typedef struct {
    DWORD     lStructSize;
    HWND      hwndOwner;
    HINSTANCE hInstance;
    LPCWSTR   lpstrFilter;
    LPWSTR    lpstrCustomFilter;
    DWORD     nMaxCustFilter;
    DWORD     nFilterIndex;
    LPWSTR    lpstrFile;
    DWORD     nMaxFile;
    LPWSTR    lpstrFileTitle;
    DWORD     nMaxFileTitle;
    LPCWSTR   lpstrInitialDir;
    LPCWSTR   lpstrTitle;
    DWORD     Flags;
    WORD      nFileOffset;
    WORD      nFileExtension;
    LPCWSTR   lpstrDefExt;
    LPARAM    lCustData;
    void*     lpfnHook;
    LPCWSTR   lpTemplateName;
    void*     pvReserved;
    DWORD     dwReserved;
    DWORD     FlagsEx;
} OV_OPENFILENAMEW;

typedef BOOL (WINAPI *PFN_GetSaveFileNameW)(OV_OPENFILENAMEW*);

static bool prompt_save_file(
    HWND hwnd,
    const wchar_t* filter,
    const wchar_t* def_ext,
    const wchar_t* default_name,
    wchar_t* out_path,
    size_t cap
) {
    ov_wcscpy(out_path, cap, default_name);
    HMODULE hComDlg = LoadLibraryW(L"comdlg32.dll");
    if (!hComDlg) return true;
    FARPROC proc = GetProcAddress(hComDlg, "GetSaveFileNameW");
    if (!proc) {
        FreeLibrary(hComDlg);
        return true;
    }
    PFN_GetSaveFileNameW pfn = NULL;
    memcpy(&pfn, &proc, sizeof(pfn));

    OV_OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = out_path;
    ofn.nMaxFile    = (DWORD)cap;
    ofn.lpstrDefExt = def_ext;
    ofn.Flags       = OV_OFN_OVERWRITEPROMPT | OV_OFN_PATHMUSTEXIST;
    BOOL ok = pfn(&ofn);
    FreeLibrary(hComDlg);
    return ok == TRUE;
}

static void sanitize_sym_filename(const wchar_t* sym, wchar_t* out, size_t cap) {
    size_t j = 0;
    for (size_t i = 0; sym[i] && j + 1 < cap; i++) {
        out[j++] = (sym[i] == L'/') ? L'-' : sym[i];
    }
    out[j] = L'\0';
}

static void handle_export_csv(AppState* app, HWND hwnd) {
    wchar_t clean[32], def_name[64], path[MAX_PATH];
    sanitize_sym_filename(app->feed.active_symbol, clean, 32);
    swprintf(def_name, 64, L"openview_%ls_%ls.csv", clean, ov_tf_code(app->feed.timeframe));
    if (prompt_save_file(hwnd, L"Fichiers CSV (*.csv)\0*.csv\0Tous (*.*)\0*.*\0", L"csv", def_name, path, MAX_PATH)) {
        CandleBar bars[MAX_CANDLES];
        size_t n = feed_manager_copy_candles(&app->feed, app->feed.primary_source, bars, MAX_CANDLES);
        export_candles_csv_w(
            path, app->feed.active_symbol, app->feed.timeframe, app->feed.primary_source,
            bars, n, &app->ind_cfg, &app->ind_buf
        );
    }
}

static void show_symbol_menu(AppState* app, HWND hwnd) {
    WatchlistItem wl[MAX_WATCHLIST];
    size_t wn = feed_manager_copy_watchlist(&app->feed, wl, MAX_WATCHLIST);
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    for (size_t i = 0; i < wn; i++) {
        if (wl[i].removed) continue;
        wchar_t item_txt[104];
        if (wl[i].display_name[0]) {
            swprintf(item_txt, 104, L"%ls (%ls)", wl[i].display_name, wl[i].symbol);
        } else {
            swprintf(item_txt, 104, L"%ls  —  %ls", wl[i].symbol, wl[i].name);
        }
        UINT flags = MF_STRING;
        bool is_act = (app->feed.active_entry_id != 0)
            ? (wl[i].id == app->feed.active_entry_id)
            : (wcscmp(wl[i].symbol, app->feed.active_symbol) == 0);
        if (is_act) flags |= MF_CHECKED;
        AppendMenuW(menu, flags, (UINT_PTR)(IDM_SYM_BASE + i), item_txt);
    }

    float dpi = get_dpi_scale(hwnd);
    POINT pt = {
        (LONG)(app->renderer.layout.symbol_btn.left * dpi),
        (LONG)(app->renderer.layout.symbol_btn.bottom * dpi)
    };
    ClientToScreen(hwnd, &pt);

    ov_apply_uxtheme_app_mode(hwnd, app->viewport.dark_theme);
    UINT cmd = (UINT)TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        pt.x, pt.y, 0, hwnd, NULL
    );
    DestroyMenu(menu);

    if (cmd >= IDM_SYM_BASE && cmd < IDM_SYM_BASE + (UINT)wn) {
        size_t idx = (size_t)(cmd - IDM_SYM_BASE);
        if (!wl[idx].removed) {
            select_watchlist_entry(app, wl[idx].id);
            app->viewport.last_primary_tick_ms = 0;
            app->viewport.draft_count = 0;
            app->viewport.selected_drawing_id = 0;
            app->viewport.y_pan_factor = 0.0f;
            refresh_analytics(app, hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }
}

static void edit_drawing(AppState* app, HWND hwnd, uint32_t drawing_id) {
    ChartDrawing* dr = drawings_find_by_id(&app->drawings, drawing_id);
    if (!dr) return;

    app->viewport.selected_drawing_id = drawing_id;
    int result = ui_show_drawing_dialog(hwnd, dr, app->viewport.dark_theme);
    if (result == 2) {
        drawings_remove_by_id(&app->drawings, drawing_id);
        app->viewport.selected_drawing_id = 0;
        persist_drawings(app);
    } else if (result == 1) {
        if (dr->kind > TOOL_CURSOR && dr->kind <= TOOL_TEXT) {
            DrawingStyleDefaults* style = &app->drawing_defaults[dr->kind];
            style->color = dr->argb_color;
            style->width = dr->stroke_width;
            style->line_style = dr->line_style;
            style->align_h = dr->label_align_h;
            style->align_v = dr->label_align_v;
            ov_wcscpy(style->label, 64, dr->label);
            app->viewport.drawing_color = dr->argb_color;
            persist_settings(
                app->feed.timeframe, app->feed.primary_source, &app->ind_cfg,
                app->drawing_defaults
            );
        }
        persist_drawings(app);
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

static const wchar_t* drawing_kind_label(DrawingToolKind kind) {
    switch (kind) {
        case TOOL_TRENDLINE: return L"Ligne de tendance";
        case TOOL_HLINE:     return L"Ligne horizontale";
        case TOOL_FIBONACCI: return L"Fibonacci";
        case TOOL_CHANNEL:   return L"Canal";
        case TOOL_RECTANGLE: return L"Rectangle";
        case TOOL_TEXT:      return L"Texte";
        default:             return L"Dessin";
    }
}

static void show_drawing_menu(AppState* app, HWND hwnd, LPARAM lParam) {
    HMENU menu = CreatePopupMenu();
    if (!menu) {
        MessageBoxW(hwnd, L"Impossible d'ouvrir la liste des dessins.",
                    L"Gestion des dessins", MB_OK | MB_ICONERROR);
        return;
    }

    uint32_t drawing_ids[MAX_DRAWINGS];
    size_t item_count = 0;
    bool is_primary_entry = is_active_entry_primary(app);
    for (size_t i = 0; i < app->drawings.count; i++) {
        const ChartDrawing* dr = &app->drawings.items[i];
        if (!drawings_matches_scope(
                dr, app->feed.active_entry_id, app->feed.active_symbol, is_primary_entry)) {
            continue;
        }

        wchar_t item_text[144];
        const wchar_t* kind = drawing_kind_label(dr->kind);
        if (dr->label[0]) {
            swprintf(item_text, 144, L"%ls — %ls", kind, dr->label);
        } else if (dr->kind == TOOL_HLINE && dr->pt_count > 0) {
            swprintf(item_text, 144, L"%ls — %.6g", kind, dr->pts[0].price);
        } else {
            swprintf(item_text, 144, L"%ls — Sans nom", kind);
        }

        UINT command = IDM_DRAWING_BASE + (UINT)item_count;
        if (!AppendMenuW(menu, MF_STRING, command, item_text)) {
            DestroyMenu(menu);
            MessageBoxW(hwnd, L"Impossible de charger la liste des dessins.",
                        L"Gestion des dessins", MB_OK | MB_ICONERROR);
            return;
        }
        drawing_ids[item_count++] = dr->id;
    }

    if (item_count == 0 &&
        !AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Aucun dessin pour ce token")) {
        DestroyMenu(menu);
        MessageBoxW(hwnd, L"Impossible d'afficher la liste des dessins.",
                    L"Gestion des dessins", MB_OK | MB_ICONERROR);
        return;
    }

    POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    ClientToScreen(hwnd, &pt);
    ov_apply_uxtheme_app_mode(hwnd, app->viewport.dark_theme);
    SetForegroundWindow(hwnd);
    UINT command = (UINT)TrackPopupMenu(
        menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
        pt.x, pt.y, 0, hwnd, NULL
    );
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (command >= IDM_DRAWING_BASE &&
        command < IDM_DRAWING_BASE + (UINT)item_count) {
        edit_drawing(app, hwnd, drawing_ids[command - IDM_DRAWING_BASE]);
    }
}

typedef struct {
    AppState* app;
    HWND hwnd;
    FeedSourceId source;
} AddTokenActionContext;

static bool add_token_to_watchlist(
    void* context,
    const wchar_t* symbol,
    const wchar_t* name,
    const wchar_t* provider_id
) {
    AddTokenActionContext* action = (AddTokenActionContext*)context;
    wchar_t source_suffix[32], attributed_name[48];
    const wchar_t* source_name = (provider_id && provider_id[0])
        ? L"CoinGecko"
        : action->app->feed.sources[action->source].label;
    if (!source_name[0]) source_name = L"Unknown";
    swprintf(source_suffix, 32, L" (%ls)", source_name);

    size_t suffix_len = wcslen(source_suffix);
    size_t name_len = name ? wcslen(name) : 0;
    if (name_len + suffix_len > 47) name_len = 47 - suffix_len;
    if (name_len > 0) wmemcpy(attributed_name, name, name_len);
    wmemcpy(attributed_name + name_len, source_suffix, suffix_len);
    attributed_name[name_len + suffix_len] = L'\0';

    if (!feed_manager_add_token(&action->app->feed, symbol, attributed_name, provider_id, 0)) {
        return false;
    }
    if (provider_id && provider_id[0]) {
        feed_manager_set_primary_source(&action->app->feed, SRC_COINGECKO);
    } else if (action->app->feed.primary_source == SRC_COINGECKO) {
        feed_manager_set_primary_source(&action->app->feed, action->source);
        persist_settings(
            action->app->feed.timeframe, action->source, &action->app->ind_cfg,
            action->app->drawing_defaults
        );
    }
    persist_drawings(action->app);
    action->app->viewport.draft_count = 0;
    action->app->viewport.selected_drawing_id = 0;
    refresh_analytics(action->app, action->hwnd);
    InvalidateRect(action->hwnd, NULL, FALSE);
    return true;
}

static void handle_add_token(AppState* app, HWND hwnd) {
    MarketOption* markets = (MarketOption*)calloc(MAX_MARKET_OPTIONS, sizeof(*markets));
    if (!markets) {
        MessageBoxW(hwnd, L"Impossible de charger la liste des marchés.",
                    L"Ajouter un token", MB_OK | MB_ICONERROR);
        return;
    }

    FeedSourceId source = app->feed.primary_source == SRC_COINGECKO
        ? SRC_BINANCE
        : app->feed.primary_source;
    size_t market_count = feed_manager_copy_market_cache(
        &app->feed, source, markets, MAX_MARKET_OPTIONS
    );
    if (market_count == 0 && feed_manager_refresh_market_cache(&app->feed, source)) {
        market_count = feed_manager_copy_market_cache(
            &app->feed, source, markets, MAX_MARKET_OPTIONS
        );
    }
    AddTokenActionContext action = { app, hwnd, source };

    ui_show_add_token_dialog(
        hwnd, &app->feed, source, markets, market_count,
        add_token_to_watchlist, &action, app->viewport.dark_theme
    );
    free(markets);
}

static bool handle_toolbar_and_sidebars(AppState* app, HWND hwnd, float x_dip, float y_dip) {
    const UILayoutCache* L = &app->renderer.layout;

    /* 1. Top Toolbar */
    if (pt_in(L->symbol_btn, x_dip, y_dip)) {
        show_symbol_menu(app, hwnd);
        return true;
    }
    for (int i = 0; i < 9; i++) {
        if (pt_in(L->tf_btns[i], x_dip, y_dip)) {
            feed_manager_set_timeframe(&app->feed, L->tf_values[i]);
            app->viewport.last_primary_tick_ms = 0;
            persist_settings(
                L->tf_values[i], app->feed.primary_source, &app->ind_cfg,
                app->drawing_defaults
            );
            app->viewport.draft_count = 0;
            app->viewport.pan_bars = 4.0f;
            app->viewport.y_pan_factor = 0.0f;
            refresh_analytics(app, hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return true;
        }
    }
    if (pt_in(L->ind_btn, x_dip, y_dip)) {
        if (ui_show_indicators_dialog(hwnd, &app->ind_cfg, app->viewport.dark_theme)) {
            persist_settings(
                app->feed.timeframe, app->feed.primary_source, &app->ind_cfg,
                app->drawing_defaults
            );
            refresh_analytics(app, hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return true;
    }
    if (pt_in(L->sources_btn, x_dip, y_dip)) {
        app->viewport.active_tab = TAB_SOURCES;
        InvalidateRect(hwnd, NULL, FALSE);
        return true;
    }
    if (pt_in(L->theme_btn, x_dip, y_dip)) {
        app->viewport.dark_theme = !app->viewport.dark_theme;
        update_titlebar_theme(hwnd, app->viewport.dark_theme);
        InvalidateRect(hwnd, NULL, FALSE);
        return true;
    }

    /* 2. Left Drawing Rail */
    for (int t = 0; t < 7; t++) {
        if (pt_in(L->tool_btns[t], x_dip, y_dip)) {
            app->viewport.active_tool = (DrawingToolKind)t;
            app->viewport.draft_count = 0;
            InvalidateRect(hwnd, NULL, FALSE);
            return true;
        }
    }
    for (int c = 0; c < 7; c++) {
        if (pt_in(L->swatch_btns[c], x_dip, y_dip)) {
            app->viewport.drawing_color = L->swatch_colors[c];
            if (app->viewport.active_tool == TOOL_CURSOR) {
                for (int kind = TOOL_TRENDLINE; kind <= TOOL_TEXT; kind++) {
                    app->drawing_defaults[kind].color = L->swatch_colors[c];
                }
            } else if (app->viewport.active_tool <= TOOL_TEXT) {
                app->drawing_defaults[app->viewport.active_tool].color = L->swatch_colors[c];
            }
            if (app->viewport.selected_drawing_id != 0) {
                ChartDrawing* dr = drawings_find_by_id(&app->drawings, app->viewport.selected_drawing_id);
                if (dr) {
                    dr->argb_color = L->swatch_colors[c];
                    if (dr->kind > TOOL_CURSOR && dr->kind <= TOOL_TEXT) {
                        app->drawing_defaults[dr->kind].color = dr->argb_color;
                    }
                    persist_drawings(app);
                }
            }
            persist_settings(
                app->feed.timeframe, app->feed.primary_source, &app->ind_cfg,
                app->drawing_defaults
            );
            InvalidateRect(hwnd, NULL, FALSE);
            return true;
        }
    }
    if (pt_in(L->undo_btn, x_dip, y_dip)) {
        if (drawings_undo_scope(&app->drawings, app->feed.active_entry_id, app->feed.active_symbol, is_active_entry_primary(app))) {
            app->viewport.selected_drawing_id = 0;
            persist_drawings(app);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return true;
    }
    if (pt_in(L->del_btn, x_dip, y_dip)) {
        if (app->viewport.selected_drawing_id != 0 &&
            drawings_remove_by_id(&app->drawings, app->viewport.selected_drawing_id)) {
            app->viewport.selected_drawing_id = 0;
            persist_drawings(app);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return true;
    }
    if (pt_in(L->clear_btn, x_dip, y_dip)) {
        drawings_clear_scope(&app->drawings, app->feed.active_entry_id, app->feed.active_symbol, is_active_entry_primary(app));
        app->viewport.selected_drawing_id = 0;
        app->viewport.draft_count = 0;
        persist_drawings(app);
        InvalidateRect(hwnd, NULL, FALSE);
        return true;
    }

    /* 3. Right Sidebar Tabs */
    for (int t = 0; t < 3; t++) {
        if (pt_in(L->tab_btns[t], x_dip, y_dip)) {
            app->viewport.active_tab = (RightSidebarTab)t;
            InvalidateRect(hwnd, NULL, FALSE);
            return true;
        }
    }

    if (app->viewport.active_tab == TAB_WATCHLIST) {
        if (pt_in(L->wl_sec_add_btn, x_dip, y_dip)) {
            wchar_t sname[48] = L"";
            if (ui_show_text_prompt(
                    hwnd, L"Nouvelle section", L"Nom de la section (ex: DeFi, Layer 1) :",
                    sname, 48, false, app->viewport.dark_theme)) {
                feed_manager_add_section(&app->feed, sname);
                persist_drawings(app);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return true;
        }
        if (pt_in(L->wl_add_btn, x_dip, y_dip)) {
            handle_add_token(app, hwnd);
            return true;
        }

        WatchlistSection secs[MAX_WL_SECTIONS];
        size_t sn = feed_manager_copy_sections(&app->feed, secs, MAX_WL_SECTIONS);
        for (size_t s = 0; s < L->wl_sec_visible_count; s++) {
            uint32_t sid = L->wl_sec_ids[s];
            if (pt_in(L->wl_list_rect, x_dip, y_dip) &&
                pt_in(L->wl_sec_del_btns[s], x_dip, y_dip)) {
                feed_manager_delete_section(&app->feed, sid);
                persist_drawings(app);
                InvalidateRect(hwnd, NULL, FALSE);
                return true;
            }
            if (pt_in(L->wl_list_rect, x_dip, y_dip) &&
                pt_in(L->wl_sec_rename_btns[s], x_dip, y_dip)) {
                wchar_t sname[48] = L"";
                for (size_t k = 0; k < sn; k++) {
                    if (secs[k].id == sid) { ov_wcscpy(sname, 48, secs[k].name); break; }
                }
                if (ui_show_text_prompt(
                        hwnd, L"Renommer la section", L"Nouveau nom de la section :",
                        sname, 48, false, app->viewport.dark_theme)) {
                    feed_manager_rename_section(&app->feed, sid, sname);
                    persist_drawings(app);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return true;
            }
        }

        WatchlistItem wl[MAX_WATCHLIST];
        size_t wn = feed_manager_copy_watchlist(&app->feed, wl, MAX_WATCHLIST);
        for (size_t i = 0; i < L->wl_visible_count; i++) {
            size_t wi = L->wl_indices[i];
            if (wi >= wn) continue;
            uint32_t eid = L->wl_entry_ids[i];

            if (pt_in(L->wl_list_rect, x_dip, y_dip) &&
                pt_in(L->wl_rename_btns[i], x_dip, y_dip)) {
                wchar_t disp[48];
                ov_wcscpy(disp, 48, wl[wi].display_name);
                wchar_t prompt_lbl[80];
                swprintf(prompt_lbl, 80, L"Nom affiché pour %ls (vide = défaut) :", wl[wi].symbol);
                if (ui_show_text_prompt(
                        hwnd, L"Renommer dans la liste", prompt_lbl,
                        disp, 48, true, app->viewport.dark_theme)) {
                    feed_manager_rename_entry(&app->feed, eid, disp);
                    persist_drawings(app);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return true;
            }
            if (pt_in(L->wl_list_rect, x_dip, y_dip) &&
                pt_in(L->wl_move_btns[i], x_dip, y_dip)) {
                uint32_t target_sec = wl[wi].section_id;
                const wchar_t* lbl = wl[wi].display_name[0] ? wl[wi].display_name : wl[wi].symbol;
                if (ui_show_move_section_dialog(
                        hwnd, lbl, secs, sn, wl[wi].section_id,
                        &target_sec, app->viewport.dark_theme)) {
                    feed_manager_move_entry(&app->feed, eid, target_sec);
                    persist_drawings(app);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return true;
            }
            if (pt_in(L->wl_list_rect, x_dip, y_dip) &&
                pt_in(L->wl_clone_btns[i], x_dip, y_dip)) {
                feed_manager_clone_entry(&app->feed, eid);
                persist_drawings(app);
                app->viewport.draft_count = 0;
                app->viewport.selected_drawing_id = 0;
                refresh_analytics(app, hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return true;
            }
            if (pt_in(L->wl_list_rect, x_dip, y_dip) &&
                pt_in(L->wl_del_btns[i], x_dip, y_dip)) {
                drawings_remove_by_entry(&app->drawings, eid);
                feed_manager_remove_entry(&app->feed, eid);
                persist_drawings(app);
                app->viewport.draft_count = 0;
                app->viewport.selected_drawing_id = 0;
                refresh_analytics(app, hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return true;
            }
        }

        for (size_t s = 0; s < L->wl_sec_visible_count; s++) {
            if (pt_in(L->wl_list_rect, x_dip, y_dip) &&
                pt_in(L->wl_sec_headers[s], x_dip, y_dip)) {
                app->drag_mode                    = DRAG_WL_SECTION;
                app->drag_start_x_dip             = x_dip;
                app->drag_start_y_dip             = y_dip;
                app->viewport.wl_drag_kind        = 2;
                app->viewport.wl_drag_id          = L->wl_sec_ids[s];
                app->viewport.wl_drag_active      = false;
                app->viewport.wl_drop_entry_id    = 0;
                app->viewport.wl_drop_section_id  = L->wl_sec_ids[s];
                app->viewport.wl_drop_after       = false;
                SetCapture(hwnd);
                return true;
            }
        }
        for (size_t i = 0; i < L->wl_visible_count; i++) {
            size_t wi = L->wl_indices[i];
            if (wi >= wn) continue;
            if (pt_in(L->wl_list_rect, x_dip, y_dip) &&
                pt_in(L->wl_rows[i], x_dip, y_dip)) {
                app->drag_mode                    = DRAG_WL_ENTRY;
                app->drag_start_x_dip             = x_dip;
                app->drag_start_y_dip             = y_dip;
                app->viewport.wl_drag_kind        = 1;
                app->viewport.wl_drag_id          = L->wl_entry_ids[i];
                app->viewport.wl_drag_active      = false;
                app->viewport.wl_drop_entry_id    = L->wl_entry_ids[i];
                app->viewport.wl_drop_section_id  = wl[wi].section_id;
                app->viewport.wl_drop_after       = false;
                SetCapture(hwnd);
                return true;
            }
        }
    } else if (app->viewport.active_tab == TAB_ALERTS) {
        if (pt_in(L->alert_add_btn, x_dip, y_dip)) {
            AlertRule nr;
            if (ui_show_add_alert_dialog(hwnd, app->feed.active_symbol, &nr, app->viewport.dark_theme)) {
                alerts_add_rule(&app->alerts, &nr);
                refresh_analytics(app, hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return true;
        }
        if (pt_in(L->alert_clear_btn, x_dip, y_dip)) {
            alerts_clear_logs(&app->alerts);
            InvalidateRect(hwnd, NULL, FALSE);
            return true;
        }
        for (size_t i = 0; i < L->alert_visible_count; i++) {
            if (pt_in(L->alert_toggle_btns[i], x_dip, y_dip)) {
                alerts_toggle_rule(&app->alerts, L->alert_ids[i]);
                InvalidateRect(hwnd, NULL, FALSE);
                return true;
            }
            if (pt_in(L->alert_del_btns[i], x_dip, y_dip)) {
                alerts_delete_rule(&app->alerts, L->alert_ids[i]);
                InvalidateRect(hwnd, NULL, FALSE);
                return true;
            }
        }
    } else {
        for (int s = 0; s < SRC_COUNT; s++) {
            if (pt_in(L->src_primary_btns[s], x_dip, y_dip)) {
                feed_manager_set_primary_source(&app->feed, (FeedSourceId)s);
                app->viewport.last_primary_tick_ms = 0;
                persist_settings(
                    app->feed.timeframe, (FeedSourceId)s, &app->ind_cfg,
                    app->drawing_defaults
                );
                refresh_analytics(app, hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return true;
            }
            if (pt_in(L->src_overlay_btns[s], x_dip, y_dip)) {
                feed_manager_toggle_overlay(&app->feed, (FeedSourceId)s);
                InvalidateRect(hwnd, NULL, FALSE);
                return true;
            }
        }
    }

    return false;
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AppState* app = (AppState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            app = (AppState*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)app);

            app->viewport.bar_width      = 10.0f;
            app->viewport.pan_bars       = 4.0f;
            app->viewport.y_scale_factor = 1.0f;
            app->viewport.y_pan_factor   = 0.0f;
            app->viewport.drawing_color  = 0xFF2962FFU;
            app->viewport.dark_theme     = true;
            app->viewport.active_tab     = TAB_WATCHLIST;
            app->viewport.active_tool    = TOOL_CURSOR;

            for (int kind = TOOL_CURSOR; kind <= TOOL_TEXT; kind++) {
                DrawingStyleDefaults* style = &app->drawing_defaults[kind];
                style->color = app->viewport.drawing_color;
                style->width = 1.8f;
                style->line_style = LINE_SOLID;
                style->align_h =
                    (kind == TOOL_TRENDLINE || kind == TOOL_CHANNEL)
                    ? ALIGN_H_CENTER : ALIGN_H_LEFT;
                style->align_v = ALIGN_V_TOP;
            }

            update_titlebar_theme(hwnd, app->viewport.dark_theme);
            d2d_renderer_init(&app->renderer, hwnd);
            indicators_default_settings(&app->ind_cfg);
            drawings_init(&app->drawings);
            wchar_t drawings_path[MAX_PATH];
            if (ov_state_path(drawings_path, MAX_PATH, L"openview_drawings.json")) {
                drawings_load_file_w(&app->drawings, drawings_path);
            }
            alerts_init(&app->alerts);
            feed_manager_init(&app->feed, hwnd);
            AppSettings saved_settings = load_settings();
            app->ind_cfg = saved_settings.indicators;
            load_drawing_defaults(app);
            if (saved_settings.timeframe != TF_15M) {
                feed_manager_set_timeframe(&app->feed, saved_settings.timeframe);
            }
            FeedSourceId saved_source = saved_settings.primary_source;
            if (app->feed.active_provider_id[0]) {
                saved_source = SRC_COINGECKO;
            } else if (saved_source == SRC_COINGECKO) {
                saved_source = SRC_BINANCE;
            }
            if (saved_source != app->feed.primary_source) {
                feed_manager_set_primary_source(&app->feed, saved_source);
            }
            refresh_analytics(app, hwnd);
            persist_drawings(app);
            SetTimer(hwnd, ID_TOAST_TIMER, 250, NULL);
            return 0;
        }

        case WM_SIZE:
            if (app) {
                d2d_renderer_resize(&app->renderer, LOWORD(lParam), HIWORD(lParam));
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_APP_FEED_TICK:
        case WM_APP_SNAPSHOT_READY:
            if (app) {
                if (msg == WM_APP_FEED_TICK &&
                    wParam < SRC_COUNT &&
                    (FeedSourceId)wParam == app->feed.primary_source) {
                    app->viewport.last_primary_tick_ms = GetTickCount64();
                }
                refresh_analytics(app, hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_APP_ALERT_FIRED:
            if (app && app->alerts.log_count > 0) {
                app->viewport.toast_entry    = app->alerts.logs[0];
                app->viewport.has_toast      = true;
                app->viewport.toast_until_ms = (int64_t)GetTickCount64() + 5000;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_TIMER:
            if (app && wParam == ID_TOAST_TIMER) {
                uint64_t now_ms = GetTickCount64();
                bool needs_redraw = false;
                if (app->viewport.has_toast && (int64_t)now_ms >= app->viewport.toast_until_ms) {
                    app->viewport.has_toast = false;
                    needs_redraw = true;
                }
                if (app->viewport.last_primary_tick_ms > 0 &&
                    now_ms >= app->viewport.last_primary_tick_ms &&
                    now_ms - app->viewport.last_primary_tick_ms < 5500) {
                    needs_redraw = true;
                }
                if (needs_redraw) InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_RBUTTONDOWN:
            if (app) {
                float dpi = get_dpi_scale(hwnd);
                float x_dip = (float)GET_X_LPARAM(lParam) / dpi;
                float y_dip = (float)GET_Y_LPARAM(lParam) / dpi;
                if (pt_in(app->renderer.layout.main_pane_rect, x_dip, y_dip)) {
                    show_drawing_menu(app, hwnd, lParam);
                    return 0;
                }
            }
            break;

        case WM_LBUTTONDOWN:
            if (app) {
                float dpi   = get_dpi_scale(hwnd);
                float x_dip = (float)GET_X_LPARAM(lParam) / dpi;
                float y_dip = (float)GET_Y_LPARAM(lParam) / dpi;
                app->viewport.mouse_x      = (float)GET_X_LPARAM(lParam);
                app->viewport.mouse_y      = (float)GET_Y_LPARAM(lParam);
                app->viewport.mouse_inside = true;

                if (handle_toolbar_and_sidebars(app, hwnd, x_dip, y_dip)) {
                    return 0;
                }

                const UILayoutCache* L = &app->renderer.layout;
                if (pt_in(L->price_axis_rect, x_dip, y_dip)) {
                    app->drag_mode          = DRAG_Y_SCALE;
                    app->drag_start_y_dip   = y_dip;
                    app->drag_start_y_scale = app->viewport.y_scale_factor;
                    SetCapture(hwnd);
                    return 0;
                }

                if (pt_in(L->main_pane_rect, x_dip, y_dip)) {
                    CandleBar bars[MAX_CANDLES];
                    size_t bn = feed_manager_copy_candles(&app->feed, app->feed.primary_source, bars, MAX_CANDLES);
                    if (bn == 0) return 0;

                    if (app->viewport.active_tool == TOOL_CURSOR) {
                        uint32_t hit_id = 0;
                        int pt_idx = -1;
                        if (d2d_hit_test_drawing(
                                L, bars, &app->drawings,
                                app->feed.active_entry_id, app->feed.active_symbol, is_active_entry_primary(app),
                                x_dip, y_dip, &hit_id, &pt_idx)) {
                            app->viewport.selected_drawing_id = hit_id;
                            ChartDrawing* dr = drawings_find_by_id(&app->drawings, hit_id);
                            if (dr && !dr->locked && pt_idx >= 0) {
                                app->drag_mode       = DRAG_DRAWING_PT;
                                app->drag_drawing_id = hit_id;
                                app->drag_point_idx  = pt_idx;
                                SetCapture(hwnd);
                            }
                            InvalidateRect(hwnd, NULL, FALSE);
                        } else {
                            app->viewport.selected_drawing_id = 0;
                            app->drag_mode        = DRAG_PAN;
                            app->drag_start_x_dip = x_dip;
                            app->drag_start_y_dip = y_dip;
                            app->drag_start_pan   = app->viewport.pan_bars;
                            app->drag_start_y_pan = app->viewport.y_pan_factor;
                            SetCapture(hwnd);
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    } else {
                        AnchorPoint pt = {
                            d2d_x_to_time(L, bars, x_dip),
                            d2d_y_to_price(L, y_dip)
                        };
                        DrawingToolKind tool = app->viewport.active_tool;

                        if (tool == TOOL_HLINE) {
                            uint32_t nid = add_drawing_with_defaults(app, TOOL_HLINE, &pt, 1, NULL);
                            app->viewport.selected_drawing_id = nid;
                            app->viewport.active_tool = TOOL_CURSOR;
                            app->viewport.draft_count = 0;
                            persist_drawings(app);
                        } else if (tool == TOOL_TEXT) {
                            wchar_t note[64];
                            ov_wcscpy(
                                note, 64,
                                app->drawing_defaults[TOOL_TEXT].label[0]
                                    ? app->drawing_defaults[TOOL_TEXT].label
                                    : L"Note"
                            );
                            if (ui_show_text_prompt(
                                    hwnd, L"Annotation texte", L"Texte à afficher sur le graphique :",
                                    note, 64, false, app->viewport.dark_theme) && note[0]) {
                                uint32_t nid = add_drawing_with_defaults(app, TOOL_TEXT, &pt, 1, note);
                                app->viewport.selected_drawing_id = nid;
                                persist_drawings(app);
                            }
                            app->viewport.active_tool = TOOL_CURSOR;
                            app->viewport.draft_count = 0;
                        } else if (tool == TOOL_TRENDLINE || tool == TOOL_FIBONACCI || tool == TOOL_RECTANGLE) {
                            if (app->viewport.draft_count == 0) {
                                app->viewport.draft_pts[0] = pt;
                                app->viewport.draft_count  = 1;
                            } else {
                                app->viewport.draft_pts[1] = pt;
                                uint32_t nid = add_drawing_with_defaults(
                                    app, tool, app->viewport.draft_pts, 2, NULL
                                );
                                app->viewport.selected_drawing_id = nid;
                                app->viewport.draft_count = 0;
                                app->viewport.active_tool = TOOL_CURSOR;
                                persist_drawings(app);
                            }
                        } else if (tool == TOOL_CHANNEL) {
                            if (app->viewport.draft_count < 2) {
                                app->viewport.draft_pts[app->viewport.draft_count++] = pt;
                            } else {
                                app->viewport.draft_pts[2] = pt;
                                uint32_t nid = add_drawing_with_defaults(
                                    app, TOOL_CHANNEL, app->viewport.draft_pts, 3, NULL
                                );
                                app->viewport.selected_drawing_id = nid;
                                app->viewport.draft_count = 0;
                                app->viewport.active_tool = TOOL_CURSOR;
                                persist_drawings(app);
                            }
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                }
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            if (app) {
                float dpi   = get_dpi_scale(hwnd);
                float x_dip = (float)GET_X_LPARAM(lParam) / dpi;
                float y_dip = (float)GET_Y_LPARAM(lParam) / dpi;
                const UILayoutCache* L = &app->renderer.layout;

                if (pt_in(L->main_pane_rect, x_dip, y_dip)) {
                    CandleBar bars[MAX_CANDLES];
                    size_t bn = feed_manager_copy_candles(&app->feed, app->feed.primary_source, bars, MAX_CANDLES);
                    if (bn > 0) {
                        uint32_t hit_id = 0;
                        int pt_idx = -1;
                        if (d2d_hit_test_drawing(
                                L, bars, &app->drawings,
                                app->feed.active_entry_id, app->feed.active_symbol, is_active_entry_primary(app),
                                x_dip, y_dip, &hit_id, &pt_idx)) {
                            edit_drawing(app, hwnd, hit_id);
                            return 0;
                        }
                    }
                    app->viewport.pan_bars       = 4.0f;
                    app->viewport.bar_width      = 10.0f;
                    app->viewport.y_scale_factor = 1.0f;
                    app->viewport.y_pan_factor   = 0.0f;
                    InvalidateRect(hwnd, NULL, FALSE);
                } else if (pt_in(L->price_axis_rect, x_dip, y_dip)) {
                    app->viewport.y_scale_factor = 1.0f;
                    app->viewport.y_pan_factor   = 0.0f;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;

        case WM_LBUTTONUP:
            if (app && app->drag_mode != DRAG_NONE) {
                if (app->drag_mode == DRAG_DRAWING_PT) {
                    persist_drawings(app);
                } else if (app->drag_mode == DRAG_WL_ENTRY) {
                    if (app->viewport.wl_drag_active) {
                        if (app->viewport.wl_drop_section_id != 0) {
                            feed_manager_reorder_entry(
                                &app->feed,
                                app->viewport.wl_drag_id,
                                app->viewport.wl_drop_section_id,
                                app->viewport.wl_drop_entry_id,
                                app->viewport.wl_drop_after
                            );
                            persist_drawings(app);
                        }
                    } else {
                        select_watchlist_entry(app, app->viewport.wl_drag_id);
                        app->viewport.last_primary_tick_ms = 0;
                        app->viewport.draft_count = 0;
                        app->viewport.selected_drawing_id = 0;
                        app->viewport.y_pan_factor = 0.0f;
                        refresh_analytics(app, hwnd);
                    }
                    app->viewport.wl_drag_kind       = 0;
                    app->viewport.wl_drag_id         = 0;
                    app->viewport.wl_drag_active     = false;
                    app->viewport.wl_drop_entry_id   = 0;
                    app->viewport.wl_drop_section_id = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                } else if (app->drag_mode == DRAG_WL_SECTION) {
                    if (app->viewport.wl_drag_active) {
                        if (app->viewport.wl_drop_section_id != 0 &&
                            app->viewport.wl_drop_section_id != app->viewport.wl_drag_id) {
                            feed_manager_reorder_section(
                                &app->feed,
                                app->viewport.wl_drag_id,
                                app->viewport.wl_drop_section_id,
                                app->viewport.wl_drop_after
                            );
                            persist_drawings(app);
                        }
                    } else {
                        feed_manager_toggle_section(&app->feed, app->viewport.wl_drag_id);
                    }
                    persist_drawings(app);
                    app->viewport.wl_drag_kind       = 0;
                    app->viewport.wl_drag_id         = 0;
                    app->viewport.wl_drag_active     = false;
                    app->viewport.wl_drop_entry_id   = 0;
                    app->viewport.wl_drop_section_id = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                app->drag_mode = DRAG_NONE;
                ReleaseCapture();
            }
            return 0;

        case WM_MOUSEMOVE:
            if (app) {
                float dpi   = get_dpi_scale(hwnd);
                float x_dip = (float)GET_X_LPARAM(lParam) / dpi;
                float y_dip = (float)GET_Y_LPARAM(lParam) / dpi;
                app->viewport.mouse_x      = (float)GET_X_LPARAM(lParam);
                app->viewport.mouse_y      = (float)GET_Y_LPARAM(lParam);
                app->viewport.mouse_inside = true;

                if (app->drag_mode == DRAG_PAN) {
                    CandleBar bars[MAX_CANDLES];
                    size_t bn = feed_manager_copy_candles(&app->feed, app->feed.primary_source, bars, MAX_CANDLES);
                    float dx = x_dip - app->drag_start_x_dip;
                    float np = app->drag_start_pan - (dx / app->viewport.bar_width);
                    float min_pan = (bn > 12) ? (-(float)bn + 10.0f) : 0.0f;
                    if (np < min_pan) np = min_pan;
                    if (np > 40.0f)   np = 40.0f;
                    app->viewport.pan_bars = np;

                    float main_h = app->renderer.layout.main_pane_rect.bottom -
                                   app->renderer.layout.main_pane_rect.top;
                    if (main_h > 1.0f) {
                        float dy = y_dip - app->drag_start_y_dip;
                        float y_pan = app->drag_start_y_pan + dy / main_h;
                        if (y_pan < -5.0f) y_pan = -5.0f;
                        if (y_pan > 5.0f)  y_pan = 5.0f;
                        app->viewport.y_pan_factor = y_pan;
                    }
                } else if (app->drag_mode == DRAG_Y_SCALE) {
                    float dy = y_dip - app->drag_start_y_dip;
                    float ys = app->drag_start_y_scale * (1.0f + dy * 0.005f);
                    if (ys < 0.25f) ys = 0.25f;
                    if (ys > 5.0f)  ys = 5.0f;
                    app->viewport.y_scale_factor = ys;
                } else if (app->drag_mode == DRAG_DRAWING_PT) {
                    ChartDrawing* dr = drawings_find_by_id(&app->drawings, app->drag_drawing_id);
                    if (dr && !dr->locked && app->drag_point_idx >= 0 && (size_t)app->drag_point_idx < dr->pt_count) {
                        CandleBar bars[MAX_CANDLES];
                        size_t bn = feed_manager_copy_candles(&app->feed, app->feed.primary_source, bars, MAX_CANDLES);
                        if (bn > 0) {
                            dr->pts[app->drag_point_idx].time_utc = d2d_x_to_time(&app->renderer.layout, bars, x_dip);
                            dr->pts[app->drag_point_idx].price    = d2d_y_to_price(&app->renderer.layout, y_dip);
                        }
                    }
                } else if (app->drag_mode == DRAG_WL_ENTRY) {
                    if (!app->viewport.wl_drag_active &&
                        (fabsf(x_dip - app->drag_start_x_dip) > 5.0f ||
                         fabsf(y_dip - app->drag_start_y_dip) > 5.0f)) {
                        app->viewport.wl_drag_active = true;
                    }
                    if (app->viewport.wl_drag_active) {
                        const UILayoutCache* L = &app->renderer.layout;
                        if (!pt_in(L->wl_list_rect, x_dip, y_dip)) {
                            app->viewport.wl_drop_entry_id = 0;
                            app->viewport.wl_drop_section_id = 0;
                        } else {
                            WatchlistItem wl[MAX_WATCHLIST];
                            size_t wn = feed_manager_copy_watchlist(&app->feed, wl, MAX_WATCHLIST);
                            bool matched_row = false;
                            for (size_t i = 0; i < L->wl_visible_count; i++) {
                                if (y_dip >= L->wl_rows[i].top - 2.0f && y_dip <= L->wl_rows[i].bottom + 2.0f) {
                                    size_t wi = L->wl_indices[i];
                                    app->viewport.wl_drop_entry_id = L->wl_entry_ids[i];
                                    app->viewport.wl_drop_after    = (y_dip > (L->wl_rows[i].top + L->wl_rows[i].bottom) * 0.5f);
                                    if (wi < wn) {
                                        app->viewport.wl_drop_section_id = wl[wi].section_id;
                                    }
                                    matched_row = true;
                                    break;
                                }
                            }
                            if (!matched_row) {
                                for (size_t s = 0; s < L->wl_sec_visible_count; s++) {
                                    if (y_dip >= L->wl_sec_blocks[s].top && y_dip <= L->wl_sec_blocks[s].bottom) {
                                        app->viewport.wl_drop_section_id = L->wl_sec_ids[s];
                                        app->viewport.wl_drop_entry_id   = 0;
                                        matched_row = true;
                                        break;
                                    }
                                }
                                if (!matched_row && L->wl_sec_visible_count > 0) {
                                    if (y_dip < L->wl_sec_blocks[0].top) {
                                        app->viewport.wl_drop_section_id = L->wl_sec_ids[0];
                                        app->viewport.wl_drop_entry_id   = 0;
                                    } else {
                                        app->viewport.wl_drop_section_id = L->wl_sec_ids[L->wl_sec_visible_count - 1];
                                        app->viewport.wl_drop_entry_id   = 0;
                                    }
                                }
                            }
                        }
                    }
                } else if (app->drag_mode == DRAG_WL_SECTION) {
                    if (!app->viewport.wl_drag_active &&
                        (fabsf(x_dip - app->drag_start_x_dip) > 5.0f ||
                         fabsf(y_dip - app->drag_start_y_dip) > 5.0f)) {
                        app->viewport.wl_drag_active = true;
                    }
                    if (app->viewport.wl_drag_active) {
                        const UILayoutCache* L = &app->renderer.layout;
                        if (!pt_in(L->wl_list_rect, x_dip, y_dip)) {
                            app->viewport.wl_drop_section_id = 0;
                        } else {
                            bool matched_sec = false;
                            for (size_t s = 0; s < L->wl_sec_visible_count; s++) {
                                if (y_dip >= L->wl_sec_blocks[s].top && y_dip <= L->wl_sec_blocks[s].bottom) {
                                    app->viewport.wl_drop_section_id = L->wl_sec_ids[s];
                                    app->viewport.wl_drop_after = (y_dip > (L->wl_sec_blocks[s].top + L->wl_sec_blocks[s].bottom) * 0.5f);
                                    matched_sec = true;
                                    break;
                                }
                            }
                            if (!matched_sec && L->wl_sec_visible_count > 0) {
                                if (y_dip < L->wl_sec_blocks[0].top) {
                                    app->viewport.wl_drop_section_id = L->wl_sec_ids[0];
                                    app->viewport.wl_drop_after      = false;
                                } else {
                                    app->viewport.wl_drop_section_id = L->wl_sec_ids[L->wl_sec_visible_count - 1];
                                    app->viewport.wl_drop_after      = true;
                                }
                            }
                        }
                    }
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_MOUSEWHEEL:
            if (app) {
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                POINT wheel_pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                ScreenToClient(hwnd, &wheel_pt);
                float dpi = get_dpi_scale(hwnd);
                float x_dip = (float)wheel_pt.x / dpi;
                float y_dip = (float)wheel_pt.y / dpi;
                const UILayoutCache* L = &app->renderer.layout;
                if (app->viewport.active_tab == TAB_WATCHLIST &&
                    pt_in(L->wl_list_rect, x_dip, y_dip)) {
                    app->viewport.wl_wheel_remainder += delta;
                    int notches = app->viewport.wl_wheel_remainder / WHEEL_DELTA;
                    app->viewport.wl_wheel_remainder -= notches * WHEEL_DELTA;
                    if (notches != 0) {
                        float old_scroll = app->viewport.wl_scroll;
                        app->viewport.wl_scroll -= (float)notches * 132.0f;
                        if (app->viewport.wl_scroll < 0.0f) app->viewport.wl_scroll = 0.0f;
                        if (app->viewport.wl_scroll > L->wl_scroll_max) {
                            app->viewport.wl_scroll = L->wl_scroll_max;
                        }
                        if (old_scroll != app->viewport.wl_scroll) {
                            InvalidateRect(hwnd, NULL, FALSE);
                        }
                    }
                    return 0;
                }
                app->viewport.bar_width += (delta > 0) ? 1.5f : -1.5f;
                if (app->viewport.bar_width < 3.5f)  app->viewport.bar_width = 3.5f;
                if (app->viewport.bar_width > 48.0f) app->viewport.bar_width = 48.0f;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_KEYDOWN:
            if (app) {
                if (wParam == VK_F1) {
                    MessageBoxW(
                        hwnd,
                        L"F1 : afficher cette aide\n"
                        L"1-9 : changer d'intervalle (1 min, 5 min, 15 min, 1 h, 4 h, 1 j, 1 sem., 1 mois, 1 an)\n"
                        L"E : exporter les chandelles en CSV\n"
                        L"Suppr. / Retour arrière : supprimer le dessin sélectionné\n"
                        L"Échap : annuler le dessin ou le déplacement en cours",
                        L"Raccourcis clavier", MB_OK | MB_ICONINFORMATION
                    );
                } else if (wParam == VK_ESCAPE) {
                    if (app->drag_mode == DRAG_WL_ENTRY || app->drag_mode == DRAG_WL_SECTION) {
                        app->drag_mode                   = DRAG_NONE;
                        app->viewport.wl_drag_kind       = 0;
                        app->viewport.wl_drag_id         = 0;
                        app->viewport.wl_drag_active     = false;
                        app->viewport.wl_drop_entry_id   = 0;
                        app->viewport.wl_drop_section_id = 0;
                        ReleaseCapture();
                    }
                    app->viewport.draft_count = 0;
                    app->viewport.active_tool = TOOL_CURSOR;
                    app->viewport.selected_drawing_id = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                } else if (wParam == VK_DELETE || wParam == VK_BACK) {
                    if (app->viewport.selected_drawing_id != 0 &&
                        drawings_remove_by_id(&app->drawings, app->viewport.selected_drawing_id)) {
                        app->viewport.selected_drawing_id = 0;
                        persist_drawings(app);
                        InvalidateRect(hwnd, NULL, FALSE);
                    }
                } else if (wParam == 'E') {
                    handle_export_csv(app, hwnd);
                } else if (wParam >= '1' && wParam <= '9') {
                    static const TimeframeSec kTfs[9] = {
                        TF_1M, TF_5M, TF_15M, TF_1H, TF_4H, TF_1D, TF_1W, TF_1MO, TF_1Y
                    };
                    feed_manager_set_timeframe(&app->feed, kTfs[wParam - '1']);
                    app->viewport.last_primary_tick_ms = 0;
                    persist_settings(
                        kTfs[wParam - '1'], app->feed.primary_source, &app->ind_cfg,
                        app->drawing_defaults
                    );
                    refresh_analytics(app, hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            if (app) {
                CandleBar bars[MAX_CANDLES];
                WatchlistSection secs[MAX_WL_SECTIONS];
                WatchlistItem wl[MAX_WATCHLIST];
                SourceStatusInfo srcs[SRC_COUNT];
                size_t bn = feed_manager_copy_candles(&app->feed, app->feed.primary_source, bars, MAX_CANDLES);
                size_t sn = feed_manager_copy_sections(&app->feed, secs, MAX_WL_SECTIONS);
                size_t wn = feed_manager_copy_watchlist(&app->feed, wl, MAX_WATCHLIST);
                feed_manager_copy_sources(&app->feed, srcs);
                d2d_renderer_paint(
                    &app->renderer, hwnd, &app->feed,
                    bars, bn, &app->ind_cfg, &app->ind_buf,
                    secs, sn, wl, wn, srcs, &app->drawings, &app->alerts, &app->viewport
                );
                if (app->viewport.active_tab == TAB_WATCHLIST &&
                    app->viewport.wl_scroll > app->renderer.layout.wl_scroll_max) {
                    app->viewport.wl_scroll = app->renderer.layout.wl_scroll_max;
                    app->viewport.wl_wheel_remainder = 0;
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            if (app) {
                KillTimer(hwnd, ID_TOAST_TIMER);
                persist_settings(
                    app->feed.timeframe, app->feed.primary_source, &app->ind_cfg,
                    app->drawing_defaults
                );
                persist_drawings(app);
                feed_manager_shutdown(&app->feed);
                d2d_renderer_destroy(&app->renderer);
            }
            PostQuitMessage(0);
            return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmdLine, int nCmdShow) {
    (void)hPrev;
    (void)cmdLine;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ov_enable_modern_windows_ui(true);

    AppState* app = (AppState*)calloc(1, sizeof(AppState));
    if (!app) return 1;

    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"OpenViewMainWndClass";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"OpenView — Terminal Analytique Multi-Sources (Win32 / Direct2D)",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1480, 920,
        NULL, NULL, hInst, app
    );

    if (!hwnd) {
        free(app);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    free(app);
    CoUninitialize();
    return (int)msg.wParam;
}
