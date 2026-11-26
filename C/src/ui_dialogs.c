#include "ui_dialogs.h"
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

typedef HRESULT (WINAPI *PFN_SetWindowTheme)(HWND, LPCWSTR, LPCWSTR);
typedef int     (WINAPI *PFN_SetPreferredAppMode)(int);
typedef void    (WINAPI *PFN_FlushMenuThemes)(void);
typedef BOOL    (WINAPI *PFN_AllowDarkModeForWindow)(HWND, BOOL);

typedef struct {
    bool   dark_theme;
    HBRUSH br_bg;
    HBRUSH br_edit;
} DlgThemeCtx;

static void dlg_theme_init(DlgThemeCtx* tc, HWND dlg, bool dark) {
    tc->dark_theme = dark;
    if (dark) {
        tc->br_bg   = CreateSolidBrush(RGB(30, 34, 45));  /* #1E222D */
        tc->br_edit = CreateSolidBrush(RGB(19, 23, 34));  /* #131722 */
    } else {
        tc->br_bg   = CreateSolidBrush(RGB(255, 255, 255)); /* #FFFFFF */
        tc->br_edit = CreateSolidBrush(RGB(248, 250, 252)); /* #F8FAFC */
    }
    BOOL d = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(dlg, DWMWA_USE_IMMERSIVE_DARK_MODE, &d, sizeof(d));
    DWORD corner = 2; /* DWMWCP_ROUND */
    DwmSetWindowAttribute(dlg, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
    if (hUx) {
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
        if (fp_allow) {
            PFN_AllowDarkModeForWindow pfnAllow = NULL;
            memcpy(&pfnAllow, &fp_allow, sizeof(pfnAllow));
            pfnAllow(dlg, d);
        }
        FreeLibrary(hUx);
    }
}

static void dlg_theme_apply_children(HWND dlg, const DlgThemeCtx* tc, HFONT font) {
    HMODULE hUx = LoadLibraryW(L"uxtheme.dll");
    PFN_SetWindowTheme pfnTheme = NULL;
    PFN_AllowDarkModeForWindow pfnAllow = NULL;
    if (hUx) {
        FARPROC fp = GetProcAddress(hUx, "SetWindowTheme");
        if (fp) memcpy(&pfnTheme, &fp, sizeof(pfnTheme));
        FARPROC fp_allow = GetProcAddress(hUx, (LPCSTR)(ULONG_PTR)133);
        if (fp_allow) memcpy(&pfnAllow, &fp_allow, sizeof(pfnAllow));
    }

    HWND child = GetWindow(dlg, GW_CHILD);
    while (child) {
        SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
        if (pfnAllow) {
            pfnAllow(child, tc->dark_theme ? TRUE : FALSE);
        }
        if (pfnTheme) {
            wchar_t cls[32] = L"";
            GetClassNameW(child, cls, 32);
            if (_wcsicmp(cls, L"ComboBox") == 0) {
                pfnTheme(child, tc->dark_theme ? L"DarkMode_CFD" : L"Explorer", NULL);
            } else {
                pfnTheme(child, tc->dark_theme ? L"DarkMode_Explorer" : L"Explorer", NULL);
            }
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }

    if (hUx) FreeLibrary(hUx);
}

static void dlg_theme_destroy(DlgThemeCtx* tc) {
    if (tc->br_bg)   { DeleteObject(tc->br_bg); tc->br_bg = NULL; }
    if (tc->br_edit) { DeleteObject(tc->br_edit); tc->br_edit = NULL; }
}

static void draw_modern_win_button(const DRAWITEMSTRUCT* dis, bool dark) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool pressed  = (dis->itemState & ODS_SELECTED) != 0;
    bool focused  = (dis->itemState & ODS_FOCUS) != 0 && (dis->itemState & ODS_NOFOCUSRECT) == 0;
    bool disabled = (dis->itemState & ODS_DISABLED) != 0;

    COLORREF bg_parent = dark ? RGB(30, 34, 45) : RGB(255, 255, 255);
    HBRUSH br_parent = CreateSolidBrush(bg_parent);
    FillRect(hdc, &rc, br_parent);
    DeleteObject(br_parent);

    COLORREF fill, border, text_col;
    if (dis->CtlID == IDOK) {
        fill     = pressed ? RGB(26, 76, 214) : RGB(41, 98, 255);
        border   = fill;
        text_col = RGB(255, 255, 255);
    } else if (dis->CtlID == 300) {
        fill     = pressed
            ? (dark ? RGB(78, 30, 38) : RGB(254, 226, 226))
            : (dark ? RGB(52, 26, 33) : RGB(254, 242, 242));
        border   = RGB(242, 54, 69);
        text_col = RGB(242, 54, 69);
    } else {
        fill     = pressed
            ? (dark ? RGB(54, 61, 77) : RGB(226, 232, 240))
            : (dark ? RGB(43, 49, 64) : RGB(241, 245, 249));
        border   = focused
            ? RGB(41, 98, 255)
            : (dark ? RGB(68, 76, 94) : RGB(203, 213, 225));
        text_col = dark ? RGB(226, 232, 240) : RGB(15, 23, 42);
    }
    if (disabled) {
        text_col = dark ? RGB(100, 116, 139) : RGB(148, 163, 184);
    }

    HBRUSH br_fill = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, focused && dis->CtlID == IDOK ? 2 : 1, border);
    HGDIOBJ old_br = SelectObject(hdc, br_fill);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 10, 10);
    SelectObject(hdc, old_br);
    SelectObject(hdc, old_pen);
    DeleteObject(br_fill);
    DeleteObject(pen);

    if (focused && dis->CtlID == IDOK) {
        RECT frc = { rc.left + 3, rc.top + 3, rc.right - 3, rc.bottom - 3 };
        HPEN fpen = CreatePen(PS_SOLID, 1, RGB(147, 197, 253));
        HGDIOBJ nbr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        HGDIOBJ fp = SelectObject(hdc, fpen);
        RoundRect(hdc, frc.left, frc.top, frc.right, frc.bottom, 6, 6);
        SelectObject(hdc, nbr);
        SelectObject(hdc, fp);
        DeleteObject(fpen);
    }

    wchar_t txt[64] = L"";
    GetWindowTextW(dis->hwndItem, txt, 64);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, text_col);
    RECT trc = rc;
    if (pressed) OffsetRect(&trc, 0, 1);
    DrawTextW(hdc, txt, -1, &trc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static bool dlg_theme_handle_msg(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam,
    const DlgThemeCtx* tc,
    LRESULT* out_res
) {
    if (!tc) return false;
    if (msg == WM_ERASEBKGND) {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, tc->br_bg);
        *out_res = 1;
        return true;
    }
    if (msg == WM_DRAWITEM) {
        const DRAWITEMSTRUCT* dis = (const DRAWITEMSTRUCT*)lParam;
        if (dis && dis->CtlType == ODT_BUTTON && !(dis->CtlID >= 400 && dis->CtlID <= 407)) {
            draw_modern_win_button(dis, tc->dark_theme);
            *out_res = TRUE;
            return true;
        }
        return false;
    }
    if (msg == WM_CTLCOLORDLG || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORBTN) {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, tc->dark_theme ? RGB(209, 212, 220) : RGB(15, 23, 42));
        SetBkColor(hdc,   tc->dark_theme ? RGB(30, 34, 45)   : RGB(255, 255, 255));
        *out_res = (LRESULT)tc->br_bg;
        return true;
    }
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, tc->dark_theme ? RGB(255, 255, 255) : RGB(15, 23, 42));
        SetBkColor(hdc,   tc->dark_theme ? RGB(19, 23, 34)    : RGB(248, 250, 252));
        *out_res = (LRESULT)tc->br_edit;
        return true;
    }
    return false;
}

static HFONT create_ui_font(int pt, bool bold) {
    return CreateFontW(
        -pt, 0, 0, 0,
        bold ? FW_SEMIBOLD : FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI"
    );
}

static void run_modal_loop(HWND parent, HWND dlg) {
    EnableWindow(parent, FALSE);
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    MSG msg;
    while (IsWindow(dlg) && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            HWND foc = GetFocus();
            wchar_t cls[32] = L"";
            if (foc) GetClassNameW(foc, cls, 32);
            if (_wcsicmp(cls, L"Button") == 0) {
                LONG style = GetWindowLongW(foc, GWL_STYLE);
                if ((style & BS_TYPEMASK) == BS_OWNERDRAW) {
                    int cid = GetDlgCtrlID(foc);
                    SendMessageW(dlg, WM_COMMAND, MAKEWPARAM((WORD)cid, BN_CLICKED), (LPARAM)foc);
                    continue;
                }
            }
            SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessageW(dlg, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            continue;
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
}

/* =========================================================================
 * 1. Indicators Dialog
 * ========================================================================= */
typedef struct {
    IndicatorSettings* cfg;
    bool        dark_theme;
    DlgThemeCtx theme;
    bool        accepted;
    HFONT       font;
    HWND  chk_ema_f, ed_ema_f;
    HWND  chk_ema_s, ed_ema_s;
    HWND  chk_sma,   ed_sma;
    HWND  chk_bb,    ed_bb;
    HWND  chk_rsi,   ed_rsi;
    HWND  chk_macd,  ed_macd_f, ed_macd_s, ed_macd_sig;
} IndDlgState;

static LRESULT CALLBACK IndDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    IndDlgState* st = (IndDlgState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    LRESULT tres = 0;
    if (st && dlg_theme_handle_msg(hwnd, msg, wParam, lParam, &st->theme, &tres)) {
        return tres;
    }

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            st = (IndDlgState*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
            dlg_theme_init(&st->theme, hwnd, st->dark_theme);
            st->font = create_ui_font(14, false);

            int y = 16;
            wchar_t num[32];

            st->chk_ema_f = CreateWindowW(L"BUTTON", L"EMA Rapide", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 18, y, 160, 22, hwnd, (HMENU)101, NULL, NULL);
            swprintf(num, 32, L"%d", st->cfg->ema_fast_period);
            st->ed_ema_f  = CreateWindowW(L"EDIT", num, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 210, y, 70, 22, hwnd, (HMENU)102, NULL, NULL);
            if (st->cfg->enable_ema_fast) SendMessageW(st->chk_ema_f, BM_SETCHECK, BST_CHECKED, 0);
            y += 32;

            st->chk_ema_s = CreateWindowW(L"BUTTON", L"EMA Lente", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 18, y, 160, 22, hwnd, (HMENU)103, NULL, NULL);
            swprintf(num, 32, L"%d", st->cfg->ema_slow_period);
            st->ed_ema_s  = CreateWindowW(L"EDIT", num, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 210, y, 70, 22, hwnd, (HMENU)104, NULL, NULL);
            if (st->cfg->enable_ema_slow) SendMessageW(st->chk_ema_s, BM_SETCHECK, BST_CHECKED, 0);
            y += 32;

            st->chk_sma   = CreateWindowW(L"BUTTON", L"SMA", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 18, y, 160, 22, hwnd, (HMENU)105, NULL, NULL);
            swprintf(num, 32, L"%d", st->cfg->sma_period);
            st->ed_sma    = CreateWindowW(L"EDIT", num, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 210, y, 70, 22, hwnd, (HMENU)106, NULL, NULL);
            if (st->cfg->enable_sma) SendMessageW(st->chk_sma, BM_SETCHECK, BST_CHECKED, 0);
            y += 32;

            st->chk_bb    = CreateWindowW(L"BUTTON", L"Bandes de Bollinger", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 18, y, 160, 22, hwnd, (HMENU)107, NULL, NULL);
            swprintf(num, 32, L"%d", st->cfg->bb_period);
            st->ed_bb     = CreateWindowW(L"EDIT", num, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 210, y, 70, 22, hwnd, (HMENU)108, NULL, NULL);
            if (st->cfg->enable_bb) SendMessageW(st->chk_bb, BM_SETCHECK, BST_CHECKED, 0);
            y += 32;

            st->chk_rsi   = CreateWindowW(L"BUTTON", L"RSI (Wilder)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 18, y, 160, 22, hwnd, (HMENU)109, NULL, NULL);
            swprintf(num, 32, L"%d", st->cfg->rsi_period);
            st->ed_rsi    = CreateWindowW(L"EDIT", num, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 210, y, 70, 22, hwnd, (HMENU)110, NULL, NULL);
            if (st->cfg->enable_rsi) SendMessageW(st->chk_rsi, BM_SETCHECK, BST_CHECKED, 0);
            y += 32;

            st->chk_macd  = CreateWindowW(L"BUTTON", L"MACD (F/L/S)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 18, y, 140, 22, hwnd, (HMENU)111, NULL, NULL);
            swprintf(num, 32, L"%d", st->cfg->macd_fast);
            st->ed_macd_f = CreateWindowW(L"EDIT", num, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 166, y, 36, 22, hwnd, (HMENU)112, NULL, NULL);
            swprintf(num, 32, L"%d", st->cfg->macd_slow);
            st->ed_macd_s = CreateWindowW(L"EDIT", num, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 206, y, 36, 22, hwnd, (HMENU)113, NULL, NULL);
            swprintf(num, 32, L"%d", st->cfg->macd_signal);
            st->ed_macd_sig = CreateWindowW(L"EDIT", num, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, 246, y, 34, 22, hwnd, (HMENU)114, NULL, NULL);
            if (st->cfg->enable_macd) SendMessageW(st->chk_macd, BM_SETCHECK, BST_CHECKED, 0);
            y += 42;

            CreateWindowW(L"BUTTON", L"Appliquer", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 110, y, 82, 28, hwnd, (HMENU)IDOK, NULL, NULL);
            CreateWindowW(L"BUTTON", L"Fermer", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 198, y, 82, 28, hwnd, (HMENU)IDCANCEL, NULL, NULL);

            dlg_theme_apply_children(hwnd, &st->theme, st->font);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK && st) {
                wchar_t buf[32];
                st->cfg->enable_ema_fast = (SendMessageW(st->chk_ema_f, BM_GETCHECK, 0, 0) == BST_CHECKED);
                st->cfg->enable_ema_slow = (SendMessageW(st->chk_ema_s, BM_GETCHECK, 0, 0) == BST_CHECKED);
                st->cfg->enable_sma      = (SendMessageW(st->chk_sma, BM_GETCHECK, 0, 0) == BST_CHECKED);
                st->cfg->enable_bb       = (SendMessageW(st->chk_bb, BM_GETCHECK, 0, 0) == BST_CHECKED);
                st->cfg->enable_rsi      = (SendMessageW(st->chk_rsi, BM_GETCHECK, 0, 0) == BST_CHECKED);
                st->cfg->enable_macd     = (SendMessageW(st->chk_macd, BM_GETCHECK, 0, 0) == BST_CHECKED);

                GetWindowTextW(st->ed_ema_f, buf, 32); st->cfg->ema_fast_period = _wtoi(buf);
                GetWindowTextW(st->ed_ema_s, buf, 32); st->cfg->ema_slow_period = _wtoi(buf);
                GetWindowTextW(st->ed_sma,   buf, 32); st->cfg->sma_period      = _wtoi(buf);
                GetWindowTextW(st->ed_bb,    buf, 32); st->cfg->bb_period       = _wtoi(buf);
                GetWindowTextW(st->ed_rsi,   buf, 32); st->cfg->rsi_period      = _wtoi(buf);
                GetWindowTextW(st->ed_macd_f, buf, 32); st->cfg->macd_fast      = _wtoi(buf);
                GetWindowTextW(st->ed_macd_s, buf, 32); st->cfg->macd_slow      = _wtoi(buf);
                GetWindowTextW(st->ed_macd_sig, buf, 32); st->cfg->macd_signal  = _wtoi(buf);

                if (st->cfg->ema_fast_period < 2) st->cfg->ema_fast_period = 2;
                if (st->cfg->ema_slow_period < 2) st->cfg->ema_slow_period = 2;
                if (st->cfg->sma_period < 2)      st->cfg->sma_period = 2;
                if (st->cfg->bb_period < 2)       st->cfg->bb_period = 2;
                if (st->cfg->rsi_period < 2)      st->cfg->rsi_period = 2;
                if (st->cfg->macd_fast < 2)       st->cfg->macd_fast = 2;
                if (st->cfg->macd_slow < 3)       st->cfg->macd_slow = 3;
                if (st->cfg->macd_signal < 2)     st->cfg->macd_signal = 2;

                st->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            } else if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (st) {
                if (st->font) DeleteObject(st->font);
                dlg_theme_destroy(&st->theme);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ui_show_indicators_dialog(HWND parent, IndicatorSettings* cfg, bool dark_theme) {
    static bool reg = false;
    const wchar_t* cls = L"OpenViewIndDlgClass";
    if (!reg) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc   = IndDlgProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.lpszClassName = cls;
        RegisterClassW(&wc);
        reg = true;
    }

    IndDlgState st = { .cfg = cfg, .dark_theme = dark_theme, .accepted = false };
    RECT pr;
    GetWindowRect(parent, &pr);
    int w = 315, h = 305;
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - h) / 2;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, cls, L"Indicateurs Techniques",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h,
        parent, NULL, GetModuleHandleW(NULL), &st
    );
    if (!dlg) return false;
    run_modal_loop(parent, dlg);
    return st.accepted;
}

/* =========================================================================
 * 2. Drawing Properties Dialog
 * ========================================================================= */
#define OV_CC_RGBINIT  0x00000001U
#define OV_CC_FULLOPEN 0x00000002U
#define OV_CC_ANYCOLOR 0x00000100U

typedef struct {
    DWORD     lStructSize;
    HWND      hwndOwner;
    HWND      hInstance;
    COLORREF  rgbResult;
    COLORREF* lpCustColors;
    DWORD     Flags;
    LPARAM    lCustData;
    void*     lpfnHook;
    LPCWSTR   lpTemplateName;
} OV_CHOOSECOLORW;

typedef BOOL (WINAPI *PFN_ChooseColorW)(OV_CHOOSECOLORW*);

typedef struct {
    ChartDrawing* drawing;
    bool          dark_theme;
    DlgThemeCtx   theme;
    int           result; /* 0=cancel, 1=save, 2=delete */
    HFONT         font;
    uint32_t      selected_color;
    HWND          ed_p[3];
    HWND          btn_swatch[8];
    HWND          cmb_style;
    HWND          cmb_width;
    HWND          ed_label;
    HWND          cmb_align_h;
    HWND          cmb_align_v;
    HWND          chk_locked;
} DrwDlgState;

static const uint32_t kDrwColors[7] = {
    0xFF2962FFU, 0xFFF59E0BU, 0xFF089981U, 0xFFF23645U,
    0xFFA855F7U, 0xFF38BDF8U, 0xFFE2E8F0U
};

static COLORREF argb_to_colorref(uint32_t argb) {
    return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

static uint32_t colorref_to_argb(COLORREF cr) {
    return 0xFF000000U | ((uint32_t)GetRValue(cr) << 16) | ((uint32_t)GetGValue(cr) << 8) | (uint32_t)GetBValue(cr);
}

static void draw_color_swatch(const DRAWITEMSTRUCT* dis, const DrwDlgState* st) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;
    bool dark = st->dark_theme;
    COLORREF bg = dark ? RGB(30, 34, 45) : RGB(255, 255, 255);
    HBRUSH br_bg = CreateSolidBrush(bg);
    FillRect(hdc, &rc, br_bg);
    DeleteObject(br_bg);

    int idx = (int)(dis->CtlID - 400);
    bool is_custom = (idx < 0 || idx >= 7);
    uint32_t col_argb = is_custom ? st->selected_color : kDrwColors[idx];
    bool is_selected = false;
    if (!is_custom) {
        is_selected = (st->selected_color == col_argb);
    } else {
        bool in_presets = false;
        for (int i = 0; i < 7; i++) {
            if (st->selected_color == kDrwColors[i]) { in_presets = true; break; }
        }
        is_selected = !in_presets;
    }

    if (is_selected) {
        HPEN rpen = CreatePen(PS_SOLID, 2, RGB(41, 98, 255));
        HGDIOBJ nbr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        HGDIOBJ op = SelectObject(hdc, rpen);
        Ellipse(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, nbr);
        SelectObject(hdc, op);
        DeleteObject(rpen);
    }

    RECT irc = { rc.left + 3, rc.top + 3, rc.right - 3, rc.bottom - 3 };
    HBRUSH sw_br = CreateSolidBrush(argb_to_colorref(col_argb));
    COLORREF bcol = dark ? RGB(71, 85, 105) : RGB(148, 163, 184);
    HPEN bpen = CreatePen(PS_SOLID, 1, bcol);
    HGDIOBJ ob = SelectObject(hdc, sw_br);
    HGDIOBJ op = SelectObject(hdc, bpen);
    Ellipse(hdc, irc.left, irc.top, irc.right, irc.bottom);
    SelectObject(hdc, ob);
    SelectObject(hdc, op);
    DeleteObject(sw_br);
    DeleteObject(bpen);

    if (is_custom) {
        int r = (col_argb >> 16) & 0xFF;
        int g = (col_argb >> 8) & 0xFF;
        int b = col_argb & 0xFF;
        int lum = (r * 299 + g * 587 + b * 114) / 1000;
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, lum > 150 ? RGB(15, 23, 42) : RGB(255, 255, 255));
        DrawTextW(hdc, L"+", -1, &irc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
}

static bool pick_custom_color_dialog(HWND owner, uint32_t* io_argb) {
    static COLORREF cust[16] = {
        RGB(41, 98, 255), RGB(245, 158, 11), RGB(8, 153, 129), RGB(242, 54, 69),
        RGB(168, 85, 247), RGB(56, 189, 248), RGB(226, 232, 240), RGB(249, 115, 22),
        RGB(236, 72, 153), RGB(20, 184, 166), RGB(132, 204, 22), RGB(234, 179, 8),
        RGB(99, 102, 241), RGB(100, 116, 139), RGB(255, 255, 255), RGB(15, 23, 42)
    };
    HMODULE hComDlg = LoadLibraryW(L"comdlg32.dll");
    if (!hComDlg) return false;
    FARPROC proc = GetProcAddress(hComDlg, "ChooseColorW");
    if (!proc) {
        FreeLibrary(hComDlg);
        return false;
    }
    PFN_ChooseColorW pfn = NULL;
    memcpy(&pfn, &proc, sizeof(pfn));

    OV_CHOOSECOLORW cc;
    ZeroMemory(&cc, sizeof(cc));
    cc.lStructSize  = sizeof(cc);
    cc.hwndOwner    = owner;
    cc.rgbResult    = argb_to_colorref(*io_argb);
    cc.lpCustColors = cust;
    cc.Flags        = OV_CC_RGBINIT | OV_CC_FULLOPEN | OV_CC_ANYCOLOR;
    BOOL ok = pfn(&cc);
    FreeLibrary(hComDlg);
    if (ok) {
        *io_argb = colorref_to_argb(cc.rgbResult);
        return true;
    }
    return false;
}

static LRESULT CALLBACK DrwDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DrwDlgState* st = (DrwDlgState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (st && msg == WM_DRAWITEM) {
        const DRAWITEMSTRUCT* dis = (const DRAWITEMSTRUCT*)lParam;
        if (dis && dis->CtlID >= 400 && dis->CtlID <= 407) {
            draw_color_swatch(dis, st);
            return TRUE;
        }
    }
    LRESULT tres = 0;
    if (st && dlg_theme_handle_msg(hwnd, msg, wParam, lParam, &st->theme, &tres)) {
        return tres;
    }

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            st = (DrwDlgState*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
            dlg_theme_init(&st->theme, hwnd, st->dark_theme);
            st->font = create_ui_font(14, false);
            st->selected_color = st->drawing->argb_color ? st->drawing->argb_color : 0xFF2962FFU;

            int y = 14;
            wchar_t buf[64];
            for (size_t i = 0; i < st->drawing->pt_count && i < 3; i++) {
                wchar_t lbl[48];
                swprintf(lbl, 48, (st->drawing->kind == TOOL_HLINE) ? L"Prix ($) :" : L"Point %zu Prix ($) :", i + 1);
                CreateWindowW(L"STATIC", lbl, WS_CHILD | WS_VISIBLE, 16, y + 3, 125, 20, hwnd, NULL, NULL, NULL);
                swprintf(buf, 64, L"%.4f", st->drawing->pts[i].price);
                st->ed_p[i] = CreateWindowW(L"EDIT", buf, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, 146, y, 202, 22, hwnd, NULL, NULL, NULL);
                y += 30;
            }

            CreateWindowW(L"STATIC", L"Couleur :", WS_CHILD | WS_VISIBLE, 16, y + 3, 125, 20, hwnd, NULL, NULL, NULL);
            for (int c = 0; c < 8; c++) {
                st->btn_swatch[c] = CreateWindowW(
                    L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    146 + c * 26, y - 1, 24, 24, hwnd, (HMENU)(UINT_PTR)(400 + c), NULL, NULL
                );
            }
            y += 32;

            CreateWindowW(L"STATIC", L"Type de trait :", WS_CHILD | WS_VISIBLE, 16, y + 3, 125, 20, hwnd, NULL, NULL, NULL);
            st->cmb_style = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 146, y, 202, 120, hwnd, NULL, NULL, NULL);
            SendMessageW(st->cmb_style, CB_ADDSTRING, 0, (LPARAM)L"Plein (Solide)");
            SendMessageW(st->cmb_style, CB_ADDSTRING, 0, (LPARAM)L"Tirets (Dashed)");
            SendMessageW(st->cmb_style, CB_ADDSTRING, 0, (LPARAM)L"Pointillés (Dotted)");
            SendMessageW(st->cmb_style, CB_SETCURSEL, (WPARAM)st->drawing->line_style, 0);
            y += 32;

            CreateWindowW(L"STATIC", L"Épaisseur :", WS_CHILD | WS_VISIBLE, 16, y + 3, 125, 20, hwnd, NULL, NULL, NULL);
            st->cmb_width = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 146, y, 202, 120, hwnd, NULL, NULL, NULL);
            const wchar_t* widths[5] = { L"1 px", L"2 px", L"3 px", L"4 px", L"5 px" };
            int sel_w = (int)(st->drawing->stroke_width + 0.5f) - 1;
            if (sel_w < 0) sel_w = 0;
            if (sel_w > 4) sel_w = 4;
            for (int w = 0; w < 5; w++) SendMessageW(st->cmb_width, CB_ADDSTRING, 0, (LPARAM)widths[w]);
            SendMessageW(st->cmb_width, CB_SETCURSEL, (WPARAM)sel_w, 0);
            y += 32;

            CreateWindowW(L"STATIC", L"Description :", WS_CHILD | WS_VISIBLE, 16, y + 3, 125, 20, hwnd, NULL, NULL, NULL);
            st->ed_label = CreateWindowW(L"EDIT", st->drawing->label, WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP, 146, y, 202, 22, hwnd, NULL, NULL, NULL);
            y += 32;

            CreateWindowW(L"STATIC", L"Align. horizontal :", WS_CHILD | WS_VISIBLE, 16, y + 3, 125, 20, hwnd, NULL, NULL, NULL);
            st->cmb_align_h = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 146, y, 202, 120, hwnd, NULL, NULL, NULL);
            SendMessageW(st->cmb_align_h, CB_ADDSTRING, 0, (LPARAM)L"Gauche");
            SendMessageW(st->cmb_align_h, CB_ADDSTRING, 0, (LPARAM)L"Centre");
            SendMessageW(st->cmb_align_h, CB_ADDSTRING, 0, (LPARAM)L"Droite");
            int ah = (int)st->drawing->label_align_h;
            if (ah < 0 || ah > 2) ah = 0;
            SendMessageW(st->cmb_align_h, CB_SETCURSEL, (WPARAM)ah, 0);
            y += 32;

            CreateWindowW(L"STATIC", L"Pos. verticale :", WS_CHILD | WS_VISIBLE, 16, y + 3, 125, 20, hwnd, NULL, NULL, NULL);
            st->cmb_align_v = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 146, y, 202, 120, hwnd, NULL, NULL, NULL);
            SendMessageW(st->cmb_align_v, CB_ADDSTRING, 0, (LPARAM)L"Au-dessus");
            SendMessageW(st->cmb_align_v, CB_ADDSTRING, 0, (LPARAM)L"Milieu");
            SendMessageW(st->cmb_align_v, CB_ADDSTRING, 0, (LPARAM)L"En dessous");
            int av = (int)st->drawing->label_align_v;
            if (av < 0 || av > 2) av = 0;
            SendMessageW(st->cmb_align_v, CB_SETCURSEL, (WPARAM)av, 0);
            y += 32;

            st->chk_locked = CreateWindowW(L"BUTTON", L"Verrouiller la position", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 16, y, 240, 22, hwnd, NULL, NULL, NULL);
            if (st->drawing->locked) SendMessageW(st->chk_locked, BM_SETCHECK, BST_CHECKED, 0);
            y += 34;

            CreateWindowW(L"BUTTON", L"Supprimer", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 16, y, 92, 28, hwnd, (HMENU)300, NULL, NULL);
            CreateWindowW(L"BUTTON", L"Enregistrer", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 168, y, 94, 28, hwnd, (HMENU)IDOK, NULL, NULL);
            CreateWindowW(L"BUTTON", L"Annuler", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 268, y, 80, 28, hwnd, (HMENU)IDCANCEL, NULL, NULL);

            dlg_theme_apply_children(hwnd, &st->theme, st->font);
            return 0;
        }
        case WM_COMMAND:
            if (!st) break;
            if (LOWORD(wParam) >= 400 && LOWORD(wParam) <= 406) {
                st->selected_color = kDrwColors[LOWORD(wParam) - 400];
                for (int c = 0; c < 8; c++) InvalidateRect(st->btn_swatch[c], NULL, FALSE);
                return 0;
            }
            if (LOWORD(wParam) == 407) {
                if (pick_custom_color_dialog(hwnd, &st->selected_color)) {
                    for (int c = 0; c < 8; c++) InvalidateRect(st->btn_swatch[c], NULL, FALSE);
                }
                return 0;
            }
            if (LOWORD(wParam) == 300) {
                st->result = 2;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == IDOK) {
                wchar_t buf[64];
                for (size_t i = 0; i < st->drawing->pt_count && i < 3; i++) {
                    GetWindowTextW(st->ed_p[i], buf, 64);
                    double v = wcstod(buf, NULL);
                    if (v > 0.0) st->drawing->pts[i].price = v;
                }
                st->drawing->argb_color = st->selected_color;

                int ss = (int)SendMessageW(st->cmb_style, CB_GETCURSEL, 0, 0);
                if (ss >= 0 && ss <= 2) st->drawing->line_style = (DrawingLineStyle)ss;

                int ws = (int)SendMessageW(st->cmb_width, CB_GETCURSEL, 0, 0);
                if (ws >= 0 && ws < 5) st->drawing->stroke_width = (float)(ws + 1);

                GetWindowTextW(st->ed_label, st->drawing->label, 64);

                int ah = (int)SendMessageW(st->cmb_align_h, CB_GETCURSEL, 0, 0);
                if (ah >= 0 && ah <= 2) st->drawing->label_align_h = (DrawingLabelAlignH)ah;

                int av = (int)SendMessageW(st->cmb_align_v, CB_GETCURSEL, 0, 0);
                if (av >= 0 && av <= 2) st->drawing->label_align_v = (DrawingLabelAlignV)av;

                st->drawing->locked = (SendMessageW(st->chk_locked, BM_GETCHECK, 0, 0) == BST_CHECKED);
                st->result = 1;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                st->result = 0;
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (st) {
                if (st->font) DeleteObject(st->font);
                dlg_theme_destroy(&st->theme);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int ui_show_drawing_dialog(HWND parent, ChartDrawing* drawing, bool dark_theme) {
    static bool reg = false;
    const wchar_t* cls = L"OpenViewDrwDlgClass";
    if (!reg) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc   = DrwDlgProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.lpszClassName = cls;
        RegisterClassW(&wc);
        reg = true;
    }

    DrwDlgState st = { .drawing = drawing, .dark_theme = dark_theme, .result = 0 };
    RECT pr;
    GetWindowRect(parent, &pr);
    int w = 376;
    int h = 318 + (int)(drawing->pt_count * 30);
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - h) / 2;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, cls, L"Propriétés du dessin",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h,
        parent, NULL, GetModuleHandleW(NULL), &st
    );
    if (!dlg) return 0;
    run_modal_loop(parent, dlg);
    return st.result;
}

/* =========================================================================
 * 3. Simple Text Prompt Dialog
 * ========================================================================= */
typedef struct {
    const wchar_t* label;
    wchar_t*    out_buf;
    size_t      cap;
    bool        dark_theme;
    DlgThemeCtx theme;
    bool        accepted;
    HFONT       font;
    HWND        ed;
} PromptDlgState;

static LRESULT CALLBACK PromptDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PromptDlgState* st = (PromptDlgState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    LRESULT tres = 0;
    if (st && dlg_theme_handle_msg(hwnd, msg, wParam, lParam, &st->theme, &tres)) {
        return tres;
    }

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            st = (PromptDlgState*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
            dlg_theme_init(&st->theme, hwnd, st->dark_theme);
            st->font = create_ui_font(14, false);

            CreateWindowW(L"STATIC", st->label, WS_CHILD | WS_VISIBLE, 16, 14, 270, 20, hwnd, NULL, NULL, NULL);
            st->ed = CreateWindowW(L"EDIT", st->out_buf, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 16, 38, 270, 24, hwnd, NULL, NULL, NULL);
            CreateWindowW(L"BUTTON", L"Valider", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 120, 76, 80, 28, hwnd, (HMENU)IDOK, NULL, NULL);
            CreateWindowW(L"BUTTON", L"Annuler", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 206, 76, 80, 28, hwnd, (HMENU)IDCANCEL, NULL, NULL);

            dlg_theme_apply_children(hwnd, &st->theme, st->font);
            SetFocus(st->ed);
            SendMessageW(st->ed, EM_SETSEL, 0, -1);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK && st) {
                GetWindowTextW(st->ed, st->out_buf, (int)st->cap);
                st->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (st) {
                if (st->font) DeleteObject(st->font);
                dlg_theme_destroy(&st->theme);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ui_show_text_prompt(
    HWND parent,
    const wchar_t* title,
    const wchar_t* prompt_label,
    wchar_t* out_buf,
    size_t cap,
    bool allow_empty,
    bool dark_theme
) {
    static bool reg = false;
    const wchar_t* cls = L"OpenViewPromptDlgClass";
    if (!reg) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc   = PromptDlgProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.lpszClassName = cls;
        RegisterClassW(&wc);
        reg = true;
    }

    PromptDlgState st = {
        .label = prompt_label,
        .out_buf = out_buf,
        .cap = cap,
        .dark_theme = dark_theme,
        .accepted = false
    };
    RECT pr;
    GetWindowRect(parent, &pr);
    int w = 316, h = 148;
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - h) / 2;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, cls, title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h,
        parent, NULL, GetModuleHandleW(NULL), &st
    );
    if (!dlg) return false;
    run_modal_loop(parent, dlg);
    return st.accepted && (allow_empty || out_buf[0] != L'\0');
}

/* =========================================================================
 * 4. Add Multi-Condition Alert Dialog
 * ========================================================================= */
typedef struct {
    const wchar_t* default_symbol;
    AlertRule*     out_rule;
    bool           dark_theme;
    DlgThemeCtx    theme;
    bool           accepted;
    HFONT          font;
    HWND ed_name, cmb_logic;
    HWND cmb_m1, cmb_op1, ed_v1;
    HWND cmb_m2, cmb_op2, ed_v2;
    HWND chk_cond2, chk_sound;
} AlertDlgState;

static void fill_metric_combo(HWND cmb, int sel) {
    const wchar_t* items[10] = {
        L"Prix", L"EMA Rapide", L"EMA Lente", L"SMA",
        L"RSI", L"MACD", L"MACD Signal", L"Bollinger Sup.", L"Bollinger Inf.", L"Spread (bps)"
    };
    for (int i = 0; i < 10; i++) SendMessageW(cmb, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageW(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
}

static void fill_op_combo(HWND cmb, int sel) {
    const wchar_t* items[6] = {
        L">", L"<", L">=", L"<=", L"Croise au-dessus", L"Croise en-dessous"
    };
    for (int i = 0; i < 6; i++) SendMessageW(cmb, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageW(cmb, CB_SETCURSEL, (WPARAM)sel, 0);
}

static LRESULT CALLBACK AlertDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    AlertDlgState* st = (AlertDlgState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    LRESULT tres = 0;
    if (st && dlg_theme_handle_msg(hwnd, msg, wParam, lParam, &st->theme, &tres)) {
        return tres;
    }

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            st = (AlertDlgState*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
            dlg_theme_init(&st->theme, hwnd, st->dark_theme);
            st->font = create_ui_font(14, false);

            int y = 14;
            CreateWindowW(L"STATIC", L"Nom de l'alerte :", WS_CHILD | WS_VISIBLE, 16, y + 3, 120, 20, hwnd, NULL, NULL, NULL);
            st->ed_name = CreateWindowW(L"EDIT", L"Alerte Technique", WS_CHILD | WS_VISIBLE | WS_BORDER, 140, y, 220, 22, hwnd, NULL, NULL, NULL);
            y += 32;

            CreateWindowW(L"STATIC", L"Logique :", WS_CHILD | WS_VISIBLE, 16, y + 3, 120, 20, hwnd, NULL, NULL, NULL);
            st->cmb_logic = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 140, y, 220, 100, hwnd, NULL, NULL, NULL);
            SendMessageW(st->cmb_logic, CB_ADDSTRING, 0, (LPARAM)L"ET (AND — toutes)");
            SendMessageW(st->cmb_logic, CB_ADDSTRING, 0, (LPARAM)L"OU (OR — au moins une)");
            SendMessageW(st->cmb_logic, CB_SETCURSEL, 0, 0);
            y += 34;

            CreateWindowW(L"STATIC", L"Condition 1 :", WS_CHILD | WS_VISIBLE, 16, y, 120, 20, hwnd, NULL, NULL, NULL);
            y += 22;
            st->cmb_m1  = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 16, y, 120, 160, hwnd, NULL, NULL, NULL);
            st->cmb_op1 = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 142, y, 130, 130, hwnd, NULL, NULL, NULL);
            st->ed_v1   = CreateWindowW(L"EDIT", L"70", WS_CHILD | WS_VISIBLE | WS_BORDER, 278, y, 82, 22, hwnd, NULL, NULL, NULL);
            fill_metric_combo(st->cmb_m1, METRIC_RSI);
            fill_op_combo(st->cmb_op1, OP_GT);
            y += 34;

            st->chk_cond2 = CreateWindowW(L"BUTTON", L"Condition 2 (optionnelle) :", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, y, 220, 20, hwnd, NULL, NULL, NULL);
            SendMessageW(st->chk_cond2, BM_SETCHECK, BST_CHECKED, 0);
            y += 24;
            st->cmb_m2  = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 16, y, 120, 160, hwnd, NULL, NULL, NULL);
            st->cmb_op2 = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 142, y, 130, 130, hwnd, NULL, NULL, NULL);
            st->ed_v2   = CreateWindowW(L"EDIT", L"95000", WS_CHILD | WS_VISIBLE | WS_BORDER, 278, y, 82, 22, hwnd, NULL, NULL, NULL);
            fill_metric_combo(st->cmb_m2, METRIC_PRICE);
            fill_op_combo(st->cmb_op2, OP_GT);
            y += 34;

            st->chk_sound = CreateWindowW(L"BUTTON", L"Alerte sonore Win32 (MessageBeep)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 16, y, 280, 22, hwnd, NULL, NULL, NULL);
            SendMessageW(st->chk_sound, BM_SETCHECK, BST_CHECKED, 0);
            y += 34;

            CreateWindowW(L"BUTTON", L"Créer l'alerte", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 160, y, 106, 28, hwnd, (HMENU)IDOK, NULL, NULL);
            CreateWindowW(L"BUTTON", L"Annuler", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 274, y, 86, 28, hwnd, (HMENU)IDCANCEL, NULL, NULL);

            dlg_theme_apply_children(hwnd, &st->theme, st->font);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK && st) {
                ZeroMemory(st->out_rule, sizeof(*st->out_rule));
                GetWindowTextW(st->ed_name, st->out_rule->name, 64);
                if (!st->out_rule->name[0]) ov_wcscpy(st->out_rule->name, 64, L"Alerte Technique");
                ov_wcscpy(st->out_rule->symbol, 24, st->default_symbol);
                st->out_rule->logic = (SendMessageW(st->cmb_logic, CB_GETCURSEL, 0, 0) == 1) ? LOGIC_OR : LOGIC_AND;
                st->out_rule->enabled = true;
                st->out_rule->sound = (SendMessageW(st->chk_sound, BM_GETCHECK, 0, 0) == BST_CHECKED);
                st->out_rule->cooldown_ms = 60000;

                wchar_t vbuf[32];
                GetWindowTextW(st->ed_v1, vbuf, 32);
                st->out_rule->conditions[0] = (AlertCondition){
                    .left_metric     = (AlertMetric)SendMessageW(st->cmb_m1, CB_GETCURSEL, 0, 0),
                    .op              = (AlertOp)SendMessageW(st->cmb_op1, CB_GETCURSEL, 0, 0),
                    .right_is_metric = false,
                    .right_value     = wcstod(vbuf, NULL)
                };
                st->out_rule->condition_count = 1;

                if (SendMessageW(st->chk_cond2, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    GetWindowTextW(st->ed_v2, vbuf, 32);
                    st->out_rule->conditions[1] = (AlertCondition){
                        .left_metric     = (AlertMetric)SendMessageW(st->cmb_m2, CB_GETCURSEL, 0, 0),
                        .op              = (AlertOp)SendMessageW(st->cmb_op2, CB_GETCURSEL, 0, 0),
                        .right_is_metric = false,
                        .right_value     = wcstod(vbuf, NULL)
                    };
                    st->out_rule->condition_count = 2;
                }
                st->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (st) {
                if (st->font) DeleteObject(st->font);
                dlg_theme_destroy(&st->theme);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ui_show_add_alert_dialog(
    HWND parent,
    const wchar_t* default_symbol,
    AlertRule* out_rule,
    bool dark_theme
) {
    static bool reg = false;
    const wchar_t* cls = L"OpenViewAlertDlgClass";
    if (!reg) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc   = AlertDlgProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.lpszClassName = cls;
        RegisterClassW(&wc);
        reg = true;
    }

    AlertDlgState st = {
        .default_symbol = default_symbol,
        .out_rule = out_rule,
        .dark_theme = dark_theme,
        .accepted = false
    };
    RECT pr;
    GetWindowRect(parent, &pr);
    int w = 392, h = 320;
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - h) / 2;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, cls, L"Nouvelle Alerte Multi-Conditions",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h,
        parent, NULL, GetModuleHandleW(NULL), &st
    );
    if (!dlg) return false;
    run_modal_loop(parent, dlg);
    return st.accepted;
}

/* =========================================================================
 * 5. Move Token To Section Dialog
 * ========================================================================= */
typedef struct {
    const wchar_t*          item_label;
    const WatchlistSection* sections;
    size_t                  section_count;
    uint32_t                current_section_id;
    uint32_t*               out_section_id;
    bool                    dark_theme;
    DlgThemeCtx             theme;
    bool                    accepted;
    HFONT                   font;
    HWND                    cmb_sec;
} MoveSecDlgState;

static LRESULT CALLBACK MoveSecDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MoveSecDlgState* st = (MoveSecDlgState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    LRESULT tres = 0;
    if (st && dlg_theme_handle_msg(hwnd, msg, wParam, lParam, &st->theme, &tres)) {
        return tres;
    }

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
            st = (MoveSecDlgState*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)st);
            dlg_theme_init(&st->theme, hwnd, st->dark_theme);
            st->font = create_ui_font(14, false);

            wchar_t hdr[96];
            swprintf(hdr, 96, L"Déplacer « %ls » vers :", st->item_label);
            CreateWindowW(L"STATIC", hdr, WS_CHILD | WS_VISIBLE, 16, 14, 270, 20, hwnd, NULL, NULL, NULL);
            st->cmb_sec = CreateWindowW(L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 16, 38, 270, 160, hwnd, NULL, NULL, NULL);
            int sel = 0;
            for (size_t i = 0; i < st->section_count; i++) {
                SendMessageW(st->cmb_sec, CB_ADDSTRING, 0, (LPARAM)st->sections[i].name);
                if (st->sections[i].id == st->current_section_id) sel = (int)i;
            }
            SendMessageW(st->cmb_sec, CB_SETCURSEL, (WPARAM)sel, 0);

            CreateWindowW(L"BUTTON", L"Déplacer", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 120, 76, 80, 28, hwnd, (HMENU)IDOK, NULL, NULL);
            CreateWindowW(L"BUTTON", L"Annuler", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 206, 76, 80, 28, hwnd, (HMENU)IDCANCEL, NULL, NULL);

            dlg_theme_apply_children(hwnd, &st->theme, st->font);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK && st) {
                int idx = (int)SendMessageW(st->cmb_sec, CB_GETCURSEL, 0, 0);
                if (idx >= 0 && (size_t)idx < st->section_count) {
                    *st->out_section_id = st->sections[idx].id;
                    st->accepted = true;
                }
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (st) {
                if (st->font) DeleteObject(st->font);
                dlg_theme_destroy(&st->theme);
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ui_show_move_section_dialog(
    HWND parent,
    const wchar_t* item_label,
    const WatchlistSection* sections,
    size_t section_count,
    uint32_t current_section_id,
    uint32_t* out_section_id,
    bool dark_theme
) {
    if (section_count == 0) return false;
    static bool reg = false;
    const wchar_t* cls = L"OpenViewMoveSecDlgClass";
    if (!reg) {
        WNDCLASSW wc = {0};
        wc.lpfnWndProc   = MoveSecDlgProc;
        wc.hInstance     = GetModuleHandleW(NULL);
        wc.lpszClassName = cls;
        RegisterClassW(&wc);
        reg = true;
    }

    MoveSecDlgState st = {
        .item_label = item_label,
        .sections = sections,
        .section_count = section_count,
        .current_section_id = current_section_id,
        .out_section_id = out_section_id,
        .dark_theme = dark_theme,
        .accepted = false
    };
    RECT pr;
    GetWindowRect(parent, &pr);
    int w = 316, h = 148;
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - h) / 2;

    HWND dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME, cls, L"Déplacer vers une section",
        WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, w, h,
        parent, NULL, GetModuleHandleW(NULL), &st
    );
    if (!dlg) return false;
    run_modal_loop(parent, dlg);
    return st.accepted;
}
