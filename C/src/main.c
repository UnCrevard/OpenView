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

typedef enum {
    DRAG_NONE = 0,
    DRAG_PAN,
    DRAG_Y_SCALE,
    DRAG_DRAWING_PT,
    DRAG_WL_ENTRY,
    DRAG_WL_SECTION
} DragMode;

typedef struct {
    AsyncFeedManager   feed;
    D2DRenderer        renderer;
    IndicatorSettings  ind_cfg;
    IndicatorBuffers   ind_buf;
    DrawingStore       drawings;
    AlertEngine        alerts;
    ChartViewportState viewport;
    DragMode           drag_mode;
    float              drag_start_x_dip;
    float              drag_start_y_dip;
    float              drag_start_pan;
    float              drag_start_y_scale;
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
    drawings_save_file_w(&app->drawings, L"openview_drawings.json");
}

static bool is_active_entry_primary(AppState* app) {
    return feed_manager_is_primary_entry(&app->feed, app->feed.active_entry_id, app->feed.active_symbol);
}

static void refresh_analytics(AppState* app, HWND hwnd) {
    CandleBar bars[MAX_CANDLES];
    WatchlistItem wl[MAX_WATCHLIST];
    size_t n  = feed_manager_copy_candles(&app->feed, app->feed.primary_source, bars, MAX_CANDLES);
    size_t wn = feed_manager_copy_watchlist(&app->feed, wl, MAX_WATCHLIST);

    const WatchlistItem* active_tok = NULL;
    for (size_t i = 0; i < wn; i++) {
        if (!wl[i].removed && wcscmp(wl[i].symbol, app->feed.active_symbol) == 0) {
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

static void handle_export_json(AppState* app, HWND hwnd) {
    wchar_t clean[32], def_name[64], path[MAX_PATH];
    sanitize_sym_filename(app->feed.active_symbol, clean, 32);
    swprintf(def_name, 64, L"openview_%ls_%ls.json", clean, ov_tf_code(app->feed.timeframe));
    if (prompt_save_file(hwnd, L"Fichiers JSON (*.json)\0*.json\0Tous (*.*)\0*.*\0", L"json", def_name, path, MAX_PATH)) {
        CandleBar bars[MAX_CANDLES];
        size_t n = feed_manager_copy_candles(&app->feed, app->feed.primary_source, bars, MAX_CANDLES);
        export_workspace_json_w(
            path, app->feed.active_entry_id, is_active_entry_primary(app),
            app->feed.active_symbol, app->feed.timeframe, app->feed.primary_source,
            bars, n, &app->drawings, &app->alerts
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
            feed_manager_set_active_entry(&app->feed, wl[idx].id);
            app->viewport.draft_count = 0;
            app->viewport.selected_drawing_id = 0;
            refresh_analytics(app, hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
        }
    }
}

static void handle_add_token(AppState* app, HWND hwnd) {
    wchar_t raw[32] = L"";
    if (!ui_show_text_prompt(
            hwnd, L"Ajouter un token",
            L"Symbole (ex: AVAX/USDT ou AVAX — cloner si déjà présent) :",
            raw, 32, false, app->viewport.dark_theme)) {
        return;
    }
    wchar_t sym[24] = L"";
    size_t j = 0;
    for (size_t i = 0; raw[i] && j + 1 < 18; i++) {
        if (raw[i] == L' ') continue;
        sym[j++] = (wchar_t)towupper((wint_t)raw[i]);
    }
    sym[j] = L'\0';
    if (!sym[0]) return;
    if (!wcschr(sym, L'/')) {
        wcsncat(sym, L"/USDT", 23 - wcslen(sym));
    }
    wchar_t base[24];
    ov_wcscpy(base, 24, sym);
    wchar_t* slash = wcschr(base, L'/');
    if (slash) *slash = L'\0';

    feed_manager_add_token(&app->feed, sym, base, 0);
    app->viewport.draft_count = 0;
    app->viewport.selected_drawing_id = 0;
    refresh_analytics(app, hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
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
            app->viewport.draft_count = 0;
            app->viewport.pan_bars = 4.0f;
            refresh_analytics(app, hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return true;
        }
    }
    if (pt_in(L->ind_btn, x_dip, y_dip)) {
        if (ui_show_indicators_dialog(hwnd, &app->ind_cfg, app->viewport.dark_theme)) {
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
    if (pt_in(L->export_csv_btn, x_dip, y_dip)) {
        handle_export_csv(app, hwnd);
        return true;
    }
    if (pt_in(L->export_json_btn, x_dip, y_dip)) {
        handle_export_json(app, hwnd);
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
            if (app->viewport.selected_drawing_id != 0) {
                ChartDrawing* dr = drawings_find_by_id(&app->drawings, app->viewport.selected_drawing_id);
                if (dr) {
                    dr->argb_color = L->swatch_colors[c];
                    persist_drawings(app);
                }
            }
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
            if (pt_in(L->wl_sec_del_btns[s], x_dip, y_dip)) {
                feed_manager_delete_section(&app->feed, sid);
                InvalidateRect(hwnd, NULL, FALSE);
                return true;
            }
            if (pt_in(L->wl_sec_rename_btns[s], x_dip, y_dip)) {
                wchar_t sname[48] = L"";
                for (size_t k = 0; k < sn; k++) {
                    if (secs[k].id == sid) { ov_wcscpy(sname, 48, secs[k].name); break; }
                }
                if (ui_show_text_prompt(
                        hwnd, L"Renommer la section", L"Nouveau nom de la section :",
                        sname, 48, false, app->viewport.dark_theme)) {
                    feed_manager_rename_section(&app->feed, sid, sname);
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

            if (pt_in(L->wl_rename_btns[i], x_dip, y_dip)) {
                wchar_t disp[48];
                ov_wcscpy(disp, 48, wl[wi].display_name);
                wchar_t prompt_lbl[80];
                swprintf(prompt_lbl, 80, L"Nom affiché pour %ls (vide = défaut) :", wl[wi].symbol);
                if (ui_show_text_prompt(
                        hwnd, L"Renommer dans la liste", prompt_lbl,
                        disp, 48, true, app->viewport.dark_theme)) {
                    feed_manager_rename_entry(&app->feed, eid, disp);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return true;
            }
            if (pt_in(L->wl_move_btns[i], x_dip, y_dip)) {
                uint32_t target_sec = wl[wi].section_id;
                const wchar_t* lbl = wl[wi].display_name[0] ? wl[wi].display_name : wl[wi].symbol;
                if (ui_show_move_section_dialog(
                        hwnd, lbl, secs, sn, wl[wi].section_id,
                        &target_sec, app->viewport.dark_theme)) {
                    feed_manager_move_entry(&app->feed, eid, target_sec);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return true;
            }
            if (pt_in(L->wl_clone_btns[i], x_dip, y_dip)) {
                feed_manager_clone_entry(&app->feed, eid);
                app->viewport.draft_count = 0;
                app->viewport.selected_drawing_id = 0;
                refresh_analytics(app, hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return true;
            }
            if (pt_in(L->wl_del_btns[i], x_dip, y_dip)) {
                drawings_remove_by_entry(&app->drawings, eid);
                persist_drawings(app);
                feed_manager_remove_entry(&app->feed, eid);
                app->viewport.draft_count = 0;
                app->viewport.selected_drawing_id = 0;
                refresh_analytics(app, hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                return true;
            }
        }

        for (size_t s = 0; s < L->wl_sec_visible_count; s++) {
            if (pt_in(L->wl_sec_headers[s], x_dip, y_dip)) {
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
            if (pt_in(L->wl_rows[i], x_dip, y_dip)) {
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
            app->viewport.drawing_color  = 0xFF2962FFU;
            app->viewport.dark_theme     = true;
            app->viewport.active_tab     = TAB_WATCHLIST;
            app->viewport.active_tool    = TOOL_CURSOR;

            update_titlebar_theme(hwnd, app->viewport.dark_theme);
            d2d_renderer_init(&app->renderer, hwnd);
            indicators_default_settings(&app->ind_cfg);
            drawings_init(&app->drawings);
            drawings_load_file_w(&app->drawings, L"openview_drawings.json");
            alerts_init(&app->alerts);
            feed_manager_init(&app->feed, hwnd);
            refresh_analytics(app, hwnd);
            SetTimer(hwnd, ID_TOAST_TIMER, 500, NULL);
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
                if (app->viewport.has_toast && (int64_t)GetTickCount64() >= app->viewport.toast_until_ms) {
                    app->viewport.has_toast = false;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;

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
                            app->drag_start_pan   = app->viewport.pan_bars;
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
                            uint32_t nid = drawings_add(
                                &app->drawings, app->feed.active_entry_id, app->feed.active_symbol, TOOL_HLINE,
                                &pt, 1, app->viewport.drawing_color, 1.8f, LINE_SOLID, L""
                            );
                            app->viewport.selected_drawing_id = nid;
                            app->viewport.active_tool = TOOL_CURSOR;
                            app->viewport.draft_count = 0;
                            persist_drawings(app);
                        } else if (tool == TOOL_TEXT) {
                            wchar_t note[64] = L"Note";
                            if (ui_show_text_prompt(
                                    hwnd, L"Annotation texte", L"Texte à afficher sur le graphique :",
                                    note, 64, false, app->viewport.dark_theme) && note[0]) {
                                uint32_t nid = drawings_add(
                                    &app->drawings, app->feed.active_entry_id, app->feed.active_symbol, TOOL_TEXT,
                                    &pt, 1, app->viewport.drawing_color, 1.8f, LINE_SOLID, note
                                );
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
                                uint32_t nid = drawings_add(
                                    &app->drawings, app->feed.active_entry_id, app->feed.active_symbol, tool,
                                    app->viewport.draft_pts, 2,
                                    app->viewport.drawing_color, 1.8f, LINE_SOLID, L""
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
                                uint32_t nid = drawings_add(
                                    &app->drawings, app->feed.active_entry_id, app->feed.active_symbol, TOOL_CHANNEL,
                                    app->viewport.draft_pts, 3,
                                    app->viewport.drawing_color, 1.8f, LINE_SOLID, L""
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
                            app->viewport.selected_drawing_id = hit_id;
                            ChartDrawing* dr = drawings_find_by_id(&app->drawings, hit_id);
                            if (dr) {
                                int res = ui_show_drawing_dialog(hwnd, dr, app->viewport.dark_theme);
                                if (res == 2) {
                                    drawings_remove_by_id(&app->drawings, hit_id);
                                    app->viewport.selected_drawing_id = 0;
                                    persist_drawings(app);
                                } else if (res == 1) {
                                    persist_drawings(app);
                                }
                                InvalidateRect(hwnd, NULL, FALSE);
                            }
                            return 0;
                        }
                    }
                    app->viewport.pan_bars       = 4.0f;
                    app->viewport.bar_width      = 10.0f;
                    app->viewport.y_scale_factor = 1.0f;
                    InvalidateRect(hwnd, NULL, FALSE);
                } else if (pt_in(L->price_axis_rect, x_dip, y_dip)) {
                    app->viewport.y_scale_factor = 1.0f;
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
                        }
                    } else {
                        feed_manager_set_active_entry(&app->feed, app->viewport.wl_drag_id);
                        app->viewport.draft_count = 0;
                        app->viewport.selected_drawing_id = 0;
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
                        }
                    } else {
                        feed_manager_toggle_section(&app->feed, app->viewport.wl_drag_id);
                    }
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
                    float np = app->drag_start_pan + (dx / app->viewport.bar_width);
                    float min_pan = (bn > 12) ? (-(float)bn + 10.0f) : 0.0f;
                    if (np < min_pan) np = min_pan;
                    if (np > 40.0f)   np = 40.0f;
                    app->viewport.pan_bars = np;
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
                } else if (app->drag_mode == DRAG_WL_SECTION) {
                    if (!app->viewport.wl_drag_active &&
                        (fabsf(x_dip - app->drag_start_x_dip) > 5.0f ||
                         fabsf(y_dip - app->drag_start_y_dip) > 5.0f)) {
                        app->viewport.wl_drag_active = true;
                    }
                    if (app->viewport.wl_drag_active) {
                        const UILayoutCache* L = &app->renderer.layout;
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
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_MOUSEWHEEL:
            if (app) {
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                app->viewport.bar_width += (delta > 0) ? 1.5f : -1.5f;
                if (app->viewport.bar_width < 3.5f)  app->viewport.bar_width = 3.5f;
                if (app->viewport.bar_width > 48.0f) app->viewport.bar_width = 48.0f;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;

        case WM_KEYDOWN:
            if (app) {
                if (wParam == VK_ESCAPE) {
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
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            if (app) {
                KillTimer(hwnd, ID_TOAST_TIMER);
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
