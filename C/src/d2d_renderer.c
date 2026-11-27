#include "d2d_renderer.h"
#include <math.h>
#include <stdio.h>

static const GUID OV_IID_ID2D1Factory = {
    0x06152247, 0x6f50, 0x465a, { 0x92, 0x45, 0x11, 0x8b, 0xfd, 0x3b, 0x60, 0x07 }
};

static const GUID OV_IID_IDWriteFactory = {
    0xb859ee5a, 0xd838, 0x4b5b, { 0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48 }
};

static const float kFibLevels[7] = { 0.0f, 0.236f, 0.382f, 0.5f, 0.618f, 0.786f, 1.0f };

static const uint32_t kPaletteSwatches[7] = {
    0xFF2962FFU, 0xFFF59E0BU, 0xFF089981U, 0xFFF23645U,
    0xFFA855F7U, 0xFF38BDF8U, 0xFFE2E8F0U
};

static inline D2D1_POINT_2F d2d_pt(float x, float y) {
    D2D1_POINT_2F pt = { x, y };
    return pt;
}

static inline bool pt_in_rect(D2D1_RECT_F rc, float x, float y) {
    return x >= rc.left && x <= rc.right && y >= rc.top && y <= rc.bottom;
}

static void set_brush_argb(ID2D1SolidColorBrush* br, uint32_t argb, float alpha_override) {
    float a = (alpha_override >= 0.0f) ? alpha_override : ((float)((argb >> 24) & 0xFF) / 255.0f);
    D2D1_COLOR_F col = {
        (float)((argb >> 16) & 0xFF) / 255.0f,
        (float)((argb >> 8)  & 0xFF) / 255.0f,
        (float)(argb & 0xFF) / 255.0f,
        a
    };
    ID2D1SolidColorBrush_SetColor(br, &col);
}

static void format_price_w(double p, wchar_t* out, size_t cap) {
    if (p >= 1000.0)    swprintf(out, cap, L"%.2f", p);
    else if (p >= 10.0) swprintf(out, cap, L"%.3f", p);
    else if (p >= 1.0)  swprintf(out, cap, L"%.4f", p);
    else                swprintf(out, cap, L"%.5f", p);
}

static void format_time_w(int64_t ts_sec, TimeframeSec tf, wchar_t* out, size_t cap) {
    ULARGE_INTEGER uli;
    uli.QuadPart = (uint64_t)(ts_sec >= 0 ? ts_sec : 0) * 10000000ULL + 116444736000000000ULL;
    FILETIME ft_utc = { uli.LowPart, uli.HighPart };
    FILETIME ft_loc;
    SYSTEMTIME st;
    if (FileTimeToLocalFileTime(&ft_utc, &ft_loc)) {
        FileTimeToSystemTime(&ft_loc, &st);
    } else {
        FileTimeToSystemTime(&ft_utc, &st);
    }

    static const wchar_t* kMonths[12] = {
        L"jan", L"fév", L"mar", L"avr", L"mai", L"jun",
        L"jul", L"aoû", L"sep", L"oct", L"nov", L"déc"
    };
    const wchar_t* mon = kMonths[(st.wMonth >= 1 && st.wMonth <= 12) ? (st.wMonth - 1) : 0];

    if (tf == TF_1Y) {
        swprintf(out, cap, L"%04u", st.wYear);
    } else if (tf == TF_1MO) {
        swprintf(out, cap, L"%ls %02u", mon, st.wYear % 100);
    } else if (tf == TF_1W) {
        swprintf(out, cap, L"%02u %ls %02u", st.wDay, mon, st.wYear % 100);
    } else if (tf == TF_1D) {
        swprintf(out, cap, L"%02u %ls", st.wDay, mon);
    } else if (tf == TF_1H || tf == TF_4H) {
        swprintf(out, cap, L"%02u/%02u %02u:%02u", st.wDay, st.wMonth, st.wHour, st.wMinute);
    } else {
        swprintf(out, cap, L"%02u:%02u", st.wHour, st.wMinute);
    }
}

static float dist_to_seg(float px, float py, float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float len2 = dx * dx + dy * dy;
    if (len2 <= 0.0001f) return hypotf(px - x1, py - y1);
    float t = ((px - x1) * dx + (py - y1) * dy) / len2;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return hypotf(px - (x1 + t * dx), py - (y1 + t * dy));
}

float d2d_time_to_x(const UILayoutCache* l, const CandleBar* bars, int64_t time_utc) {
    if (l->bar_count == 0 || l->step_px <= 0.0f) {
        return (l->main_pane_rect.left + l->main_pane_rect.right) * 0.5f;
    }
    int64_t tf = (int64_t)l->tf;
    if (tf <= 0) tf = 900;

    float right_edge = l->main_pane_rect.right;
    #define CACHE_IDX_TO_X(idx_f) (right_edge - ((float)l->end_idx - (idx_f) + 1.0f) * l->step_px + l->step_px * 0.5f)

    if (l->bar_count == 1) return CACHE_IDX_TO_X(0.0f);
    if (time_utc <= l->first_time) {
        return CACHE_IDX_TO_X((float)(time_utc - l->first_time) / (float)tf);
    }
    if (time_utc >= l->last_time) {
        return CACHE_IDX_TO_X((float)(l->bar_count - 1) + (float)(time_utc - l->last_time) / (float)tf);
    }

    int lo = 0;
    int hi = (int)l->bar_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (bars[mid].time_utc == time_utc) return CACHE_IDX_TO_X((float)mid);
        if (bars[mid].time_utc < time_utc) lo = mid + 1;
        else hi = mid - 1;
    }
    int i0 = (hi >= 0) ? hi : 0;
    int i1 = (lo < (int)l->bar_count) ? lo : ((int)l->bar_count - 1);
    int64_t t0 = bars[i0].time_utc;
    int64_t t1 = bars[i1].time_utc;
    float frac = (t1 > t0) ? ((float)(time_utc - t0) / (float)(t1 - t0)) : 0.0f;
    return CACHE_IDX_TO_X((float)i0 + frac);
    #undef CACHE_IDX_TO_X
}

int64_t d2d_x_to_time(const UILayoutCache* l, const CandleBar* bars, float x_dip) {
    if (l->bar_count == 0 || l->step_px <= 0.0f) return 0;
    int64_t tf = (int64_t)l->tf;
    if (tf <= 0) tf = 900;

    float idx_f = (float)l->end_idx + 1.0f - (l->main_pane_rect.right - x_dip + l->step_px * 0.5f) / l->step_px;
    int last_i = (int)l->bar_count - 1;
    if (idx_f <= 0.0f) {
        return l->first_time + (int64_t)(idx_f * (float)tf);
    }
    if (idx_f >= (float)last_i) {
        return l->last_time + (int64_t)((idx_f - (float)last_i) * (float)tf);
    }
    int i0 = (int)floorf(idx_f);
    int i1 = (i0 + 1 <= last_i) ? (i0 + 1) : last_i;
    float frac = idx_f - (float)i0;
    return bars[i0].time_utc + (int64_t)(frac * (float)(bars[i1].time_utc - bars[i0].time_utc));
}

float d2d_price_to_y(const UILayoutCache* l, double price) {
    double span = l->max_price - l->min_price;
    if (span <= 0.0) span = 1.0;
    float h = l->main_pane_rect.bottom - l->main_pane_rect.top;
    return l->main_pane_rect.bottom - (float)((price - l->min_price) / span) * h;
}

double d2d_y_to_price(const UILayoutCache* l, float y_dip) {
    double span = l->max_price - l->min_price;
    float h = l->main_pane_rect.bottom - l->main_pane_rect.top;
    if (h <= 1.0f) h = 1.0f;
    return l->min_price + ((double)(l->main_pane_rect.bottom - y_dip) / (double)h) * span;
}

static D2D1_RECT_F calc_text_tool_rect(float sx, float sy, const ChartDrawing* d) {
    const wchar_t* txt = d->label[0] ? d->label : L"Note";
    float bw = (float)wcslen(txt) * 6.8f + 16.0f;
    if (bw < 52.0f) bw = 52.0f;
    if (bw > 240.0f) bw = 240.0f;
    float bh = 22.0f;
    float bx = sx;
    if (d->label_align_h == ALIGN_H_CENTER) bx = sx - bw * 0.5f;
    else if (d->label_align_h == ALIGN_H_RIGHT) bx = sx - bw;
    float by = sy - bh - 4.0f;
    if (d->label_align_v == ALIGN_V_MIDDLE) by = sy - bh * 0.5f;
    else if (d->label_align_v == ALIGN_V_BOTTOM) by = sy + 4.0f;
    return (D2D1_RECT_F){ bx, by, bx + bw, by + bh };
}

bool d2d_hit_test_drawing(
    const UILayoutCache* l,
    const CandleBar* bars,
    const DrawingStore* drawings,
    uint32_t active_entry_id,
    const wchar_t* active_symbol,
    bool is_primary_entry,
    float x_dip,
    float y_dip,
    uint32_t* out_drawing_id,
    int* out_point_idx
) {
    const float tol = 8.0f;
    for (size_t idx = drawings->count; idx > 0; idx--) {
        const ChartDrawing* d = &drawings->items[idx - 1];
        if (!drawings_matches_scope(d, active_entry_id, active_symbol, is_primary_entry)) continue;

        float sx[3], sy[3];
        for (size_t p = 0; p < d->pt_count && p < 3; p++) {
            sx[p] = d2d_time_to_x(l, bars, d->pts[p].time_utc);
            sy[p] = d2d_price_to_y(l, d->pts[p].price);
            if (d->kind != TOOL_HLINE && d->kind != TOOL_TEXT) {
                if (hypotf(x_dip - sx[p], y_dip - sy[p]) <= 10.0f) {
                    *out_drawing_id = d->id;
                    *out_point_idx  = (int)p;
                    return true;
                }
            }
        }

        if (d->kind == TOOL_HLINE && d->pt_count >= 1) {
            if (fabsf(y_dip - sy[0]) <= tol) {
                *out_drawing_id = d->id;
                *out_point_idx  = 0;
                return true;
            }
        } else if (d->kind == TOOL_TRENDLINE && d->pt_count >= 2) {
            if (dist_to_seg(x_dip, y_dip, sx[0], sy[0], sx[1], sy[1]) <= tol) {
                *out_drawing_id = d->id;
                *out_point_idx  = -1;
                return true;
            }
        } else if (d->kind == TOOL_FIBONACCI && d->pt_count >= 2) {
            float lx = (sx[0] < sx[1]) ? sx[0] : sx[1];
            float rx = (sx[0] > sx[1]) ? sx[0] : sx[1];
            if (x_dip >= lx - tol && x_dip <= rx + tol) {
                for (int k = 0; k < 7; k++) {
                    double lvl_p = d->pts[0].price + (d->pts[1].price - d->pts[0].price) * kFibLevels[k];
                    float ly = d2d_price_to_y(l, lvl_p);
                    if (fabsf(y_dip - ly) <= tol) {
                        *out_drawing_id = d->id;
                        *out_point_idx  = -1;
                        return true;
                    }
                }
            }
        } else if (d->kind == TOOL_RECTANGLE && d->pt_count >= 2) {
            float x0 = (sx[0] < sx[1]) ? sx[0] : sx[1];
            float x1 = (sx[0] > sx[1]) ? sx[0] : sx[1];
            float y0 = (sy[0] < sy[1]) ? sy[0] : sy[1];
            float y1 = (sy[0] > sy[1]) ? sy[0] : sy[1];
            if (x_dip >= x0 - tol && x_dip <= x1 + tol && y_dip >= y0 - tol && y_dip <= y1 + tol) {
                if (fabsf(x_dip - x0) <= tol || fabsf(x_dip - x1) <= tol ||
                    fabsf(y_dip - y0) <= tol || fabsf(y_dip - y1) <= tol) {
                    *out_drawing_id = d->id;
                    *out_point_idx  = -1;
                    return true;
                }
            }
        } else if (d->kind == TOOL_CHANNEL && d->pt_count >= 3) {
            float offset_y = sy[2] - sy[0];
            if (dist_to_seg(x_dip, y_dip, sx[0], sy[0], sx[1], sy[1]) <= tol ||
                dist_to_seg(x_dip, y_dip, sx[0], sy[0] + offset_y, sx[1], sy[1] + offset_y) <= tol) {
                *out_drawing_id = d->id;
                *out_point_idx  = -1;
                return true;
            }
        } else if (d->kind == TOOL_TEXT && d->pt_count >= 1) {
            D2D1_RECT_F trc = calc_text_tool_rect(sx[0], sy[0], d);
            if (x_dip >= trc.left - 4.0f && x_dip <= trc.right + 4.0f &&
                y_dip >= trc.top - 4.0f && y_dip <= trc.bottom + 4.0f) {
                *out_drawing_id = d->id;
                *out_point_idx  = 0;
                return true;
            }
        }
    }
    return false;
}

static HRESULT create_device_resources(D2DRenderer* r, HWND hwnd) {
    if (r->target) return S_OK;

    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = {
        (UINT32)(rc.right > rc.left ? rc.right - rc.left : 1),
        (UINT32)(rc.bottom > rc.top ? rc.bottom - rc.top : 1)
    };
    D2D1_RENDER_TARGET_PROPERTIES rt_props = {
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        { DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN },
        0.0f, 0.0f,
        D2D1_RENDER_TARGET_USAGE_NONE,
        D2D1_FEATURE_LEVEL_DEFAULT
    };
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props = {
        hwnd, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY
    };

    HRESULT hr = ID2D1Factory_CreateHwndRenderTarget(
        r->d2d_factory, &rt_props, &hwnd_props, &r->target
    );
    if (FAILED(hr)) return hr;

    D2D1_COLOR_F init_col = { 1.0f, 1.0f, 1.0f, 1.0f };
    return ID2D1HwndRenderTarget_CreateSolidColorBrush(r->target, &init_col, NULL, &r->brush_dyn);
}

static void discard_device_resources(D2DRenderer* r) {
    if (r->brush_dyn) {
        IUnknown_Release((IUnknown*)r->brush_dyn);
        r->brush_dyn = NULL;
    }
    if (r->target) {
        IUnknown_Release((IUnknown*)r->target);
        r->target = NULL;
    }
}

bool d2d_renderer_init(D2DRenderer* r, HWND hwnd) {
    ZeroMemory(r, sizeof(*r));
    D2D1_FACTORY_OPTIONS opts = { D2D1_DEBUG_LEVEL_NONE };
    if (FAILED(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_SINGLE_THREADED,
            &OV_IID_ID2D1Factory,
            &opts,
            (void**)&r->d2d_factory))) {
        return false;
    }

    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            &OV_IID_IDWriteFactory,
            (IUnknown**)&r->dwrite_factory))) {
        return false;
    }

    IDWriteFactory_CreateTextFormat(
        r->dwrite_factory, L"Segoe UI", NULL,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        12.0f, L"fr-FR", &r->font_ui
    );
    IDWriteFactory_CreateTextFormat(
        r->dwrite_factory, L"Segoe UI", NULL,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.5f, L"fr-FR", &r->font_ui_bold
    );
    IDWriteFactory_CreateTextFormat(
        r->dwrite_factory, L"Consolas", NULL,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.0f, L"fr-FR", &r->font_mono
    );
    IDWriteFactory_CreateTextFormat(
        r->dwrite_factory, L"Segoe UI", NULL,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        10.5f, L"fr-FR", &r->font_small
    );

    D2D1_STROKE_STYLE_PROPERTIES dash_props = {
        D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
        D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_DASH, 0.0f
    };
    ID2D1Factory_CreateStrokeStyle(r->d2d_factory, &dash_props, NULL, 0, &r->stroke_dashed);

    D2D1_STROKE_STYLE_PROPERTIES dot_props = {
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_DOT, 0.0f
    };
    ID2D1Factory_CreateStrokeStyle(r->d2d_factory, &dot_props, NULL, 0, &r->stroke_dotted);

    return SUCCEEDED(create_device_resources(r, hwnd));
}

void d2d_renderer_resize(D2DRenderer* r, UINT width, UINT height) {
    if (r->target) {
        D2D1_SIZE_U sz = { width, height };
        ID2D1HwndRenderTarget_Resize(r->target, &sz);
    }
}

void d2d_renderer_destroy(D2DRenderer* r) {
    discard_device_resources(r);
    if (r->stroke_dashed)  ID2D1StrokeStyle_Release(r->stroke_dashed);
    if (r->stroke_dotted)  ID2D1StrokeStyle_Release(r->stroke_dotted);
    if (r->font_ui)        IDWriteTextFormat_Release(r->font_ui);
    if (r->font_ui_bold)   IDWriteTextFormat_Release(r->font_ui_bold);
    if (r->font_mono)      IDWriteTextFormat_Release(r->font_mono);
    if (r->font_small)     IDWriteTextFormat_Release(r->font_small);
    if (r->dwrite_factory) IDWriteFactory_Release(r->dwrite_factory);
    if (r->d2d_factory)    ID2D1Factory_Release(r->d2d_factory);
}

static void draw_txt(
    D2DRenderer* r,
    IDWriteTextFormat* fmt,
    const wchar_t* text,
    D2D1_RECT_F rc,
    uint32_t argb
) {
    set_brush_argb(r->brush_dyn, argb, -1.0f);
    ID2D1HwndRenderTarget_DrawText(
        r->target, text, (UINT32)wcslen(text), fmt, &rc,
        (ID2D1Brush*)r->brush_dyn, D2D1_DRAW_TEXT_OPTIONS_CLIP, DWRITE_MEASURING_MODE_NATURAL
    );
}

static void draw_btn_box(
    D2DRenderer* r,
    D2D1_RECT_F rc,
    uint32_t fill_argb,
    uint32_t border_argb,
    const wchar_t* label,
    uint32_t text_argb,
    IDWriteTextFormat* fmt
) {
    D2D1_ROUNDED_RECT rrc = { rc, 5.0f, 5.0f };
    if (fill_argb != 0) {
        set_brush_argb(r->brush_dyn, fill_argb, -1.0f);
        ID2D1HwndRenderTarget_FillRoundedRectangle(r->target, &rrc, (ID2D1Brush*)r->brush_dyn);
    }
    if (border_argb != 0) {
        set_brush_argb(r->brush_dyn, border_argb, -1.0f);
        ID2D1HwndRenderTarget_DrawRoundedRectangle(r->target, &rrc, (ID2D1Brush*)r->brush_dyn, 1.0f, NULL);
    }
    if (label && label[0]) {
        IDWriteTextFormat_SetTextAlignment(fmt, DWRITE_TEXT_ALIGNMENT_CENTER);
        D2D1_RECT_F trc = { rc.left + 2.0f, rc.top + ((rc.bottom - rc.top) - 15.0f) * 0.5f, rc.right - 2.0f, rc.bottom };
        draw_txt(r, fmt, label, trc, text_argb);
        IDWriteTextFormat_SetTextAlignment(fmt, DWRITE_TEXT_ALIGNMENT_LEADING);
    }
}

static void draw_aligned_drawing_label(
    D2DRenderer* r,
    const wchar_t* label,
    float x_left,
    float x_right,
    float y_top,
    float y_bot,
    DrawingLabelAlignH align_h,
    DrawingLabelAlignV align_v,
    uint32_t color_argb,
    uint32_t bg_argb
) {
    if (!label || !label[0]) return;
    float est_w = (float)wcslen(label) * 6.2f + 10.0f;
    if (est_w < 24.0f) est_w = 24.0f;
    if (est_w > 260.0f) est_w = 260.0f;

    float ty = y_top - 17.0f;
    if (align_v == ALIGN_V_MIDDLE) {
        ty = (y_top + y_bot) * 0.5f - 8.0f;
    } else if (align_v == ALIGN_V_BOTTOM) {
        ty = y_bot + 3.0f;
    }

    D2D1_RECT_F trc;
    D2D1_RECT_F pill;
    DWRITE_TEXT_ALIGNMENT dalign = DWRITE_TEXT_ALIGNMENT_LEADING;
    bool span_box = (x_right - x_left > 1.0f);

    if (align_h == ALIGN_H_CENTER) {
        float cx = span_box ? ((x_left + x_right) * 0.5f) : x_left;
        dalign = DWRITE_TEXT_ALIGNMENT_CENTER;
        trc  = (D2D1_RECT_F){ cx - 150.0f, ty, cx + 150.0f, ty + 17.0f };
        pill = (D2D1_RECT_F){ cx - est_w * 0.5f - 2.0f, ty, cx + est_w * 0.5f + 2.0f, ty + 16.0f };
    } else if (align_h == ALIGN_H_RIGHT) {
        float rx = span_box ? (x_right - 6.0f) : (x_left - 4.0f);
        dalign = DWRITE_TEXT_ALIGNMENT_TRAILING;
        trc  = (D2D1_RECT_F){ rx - 280.0f, ty, rx, ty + 17.0f };
        pill = (D2D1_RECT_F){ rx - est_w - 2.0f, ty, rx + 4.0f, ty + 16.0f };
    } else {
        float lx = span_box ? (x_left + 6.0f) : (x_left + 4.0f);
        dalign = DWRITE_TEXT_ALIGNMENT_LEADING;
        trc  = (D2D1_RECT_F){ lx, ty, lx + 280.0f, ty + 17.0f };
        pill = (D2D1_RECT_F){ lx - 4.0f, ty, lx + est_w + 2.0f, ty + 16.0f };
    }

    if (align_v == ALIGN_V_MIDDLE) {
        D2D1_ROUNDED_RECT prc = { pill, 3.0f, 3.0f };
        set_brush_argb(r->brush_dyn, bg_argb, 0.88f);
        ID2D1HwndRenderTarget_FillRoundedRectangle(r->target, &prc, (ID2D1Brush*)r->brush_dyn);
    }

    IDWriteTextFormat_SetTextAlignment(r->font_small, dalign);
    draw_txt(r, r->font_small, label, trc, color_argb);
    IDWriteTextFormat_SetTextAlignment(r->font_small, DWRITE_TEXT_ALIGNMENT_LEADING);
}

static void draw_tool_glyph(D2DRenderer* r, int idx, D2D1_RECT_F rc, uint32_t col) {
    float cx = (rc.left + rc.right) * 0.5f;
    float cy = (rc.top + rc.bottom) * 0.5f;
    set_brush_argb(r->brush_dyn, col, -1.0f);

    if (idx == TOOL_CURSOR) {
        ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(cx - 6, cy), d2d_pt(cx + 6, cy), (ID2D1Brush*)r->brush_dyn, 1.5f, NULL);
        ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(cx, cy - 6), d2d_pt(cx, cy + 6), (ID2D1Brush*)r->brush_dyn, 1.5f, NULL);
    } else if (idx == TOOL_TRENDLINE) {
        ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(cx - 6, cy + 5), d2d_pt(cx + 6, cy - 5), (ID2D1Brush*)r->brush_dyn, 1.8f, NULL);
    } else if (idx == TOOL_HLINE) {
        ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(cx - 7, cy), d2d_pt(cx + 7, cy), (ID2D1Brush*)r->brush_dyn, 1.8f, NULL);
    } else if (idx == TOOL_FIBONACCI) {
        ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(cx - 7, cy - 5), d2d_pt(cx + 7, cy - 5), (ID2D1Brush*)r->brush_dyn, 1.3f, NULL);
        ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(cx - 7, cy),     d2d_pt(cx + 7, cy),     (ID2D1Brush*)r->brush_dyn, 1.3f, NULL);
        ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(cx - 7, cy + 5), d2d_pt(cx + 7, cy + 5), (ID2D1Brush*)r->brush_dyn, 1.3f, NULL);
    } else if (idx == TOOL_CHANNEL) {
        ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(cx - 7, cy + 2), d2d_pt(cx + 5, cy - 6), (ID2D1Brush*)r->brush_dyn, 1.4f, NULL);
        ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(cx - 5, cy + 6), d2d_pt(cx + 7, cy - 2), (ID2D1Brush*)r->brush_dyn, 1.4f, NULL);
    } else if (idx == TOOL_RECTANGLE) {
        D2D1_RECT_F sq = { cx - 6, cy - 5, cx + 6, cy + 5 };
        ID2D1HwndRenderTarget_DrawRectangle(r->target, &sq, (ID2D1Brush*)r->brush_dyn, 1.5f, NULL);
    } else if (idx == TOOL_TEXT) {
        IDWriteTextFormat_SetTextAlignment(r->font_ui_bold, DWRITE_TEXT_ALIGNMENT_CENTER);
        D2D1_RECT_F tr = { rc.left, rc.top + 6, rc.right, rc.bottom };
        draw_txt(r, r->font_ui_bold, L"T", tr, col);
        IDWriteTextFormat_SetTextAlignment(r->font_ui_bold, DWRITE_TEXT_ALIGNMENT_LEADING);
    }
}

void d2d_renderer_paint(
    D2DRenderer* r,
    HWND hwnd,
    const AsyncFeedManager* feed,
    const CandleBar* primary_bars,
    size_t primary_count,
    const IndicatorSettings* ind_cfg,
    const IndicatorBuffers* ind,
    const WatchlistSection* sections,
    size_t section_count,
    const WatchlistItem* watchlist,
    size_t watchlist_count,
    const SourceStatusInfo sources[SRC_COUNT],
    const DrawingStore* drawings,
    const AlertEngine* alerts,
    const ChartViewportState* vp
) {
    if (FAILED(create_device_resources(r, hwnd))) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    float dpi = (float)GetDpiForWindow(hwnd) / 96.0f;
    if (dpi <= 0.0f) dpi = 1.0f;
    const float w_dip = (float)(rc.right - rc.left) / dpi;
    const float h_dip = (float)(rc.bottom - rc.top) / dpi;

    const bool dark = vp->dark_theme;
    const uint32_t col_bg       = dark ? 0xFF131722U : 0xFFFFFFFFU;
    const uint32_t col_panel    = dark ? 0xFF1E222DU : 0xFFF8FAFCU;
    const uint32_t col_surface  = dark ? 0xFF131722U : 0xFFFFFFFFU;
    const uint32_t col_border   = dark ? 0xFF2A2E39U : 0xFFE2E8F0U;
    const uint32_t col_grid     = dark ? 0xFF1E222DU : 0xFFF1F5F9U;
    const uint32_t col_text     = dark ? 0xFFD1D4DCU : 0xFF0F172AU;
    const uint32_t col_bright   = dark ? 0xFFFFFFFFU : 0xFF0F172AU;
    const uint32_t col_muted    = dark ? 0xFF787B86U : 0xFF64748BU;
    const uint32_t col_accent   = 0xFF2962FFU;
    const uint32_t col_bull     = 0xFF089981U;
    const uint32_t col_bear     = 0xFFF23645U;
    const uint32_t col_ema1     = 0xFFF59E0BU;
    const uint32_t col_ema2     = 0xFF2962FFU;
    const uint32_t col_sma      = 0xFF38BDF8U;
    const uint32_t col_bb       = 0xFFA855F7U;
    const uint32_t col_macd_sig = 0xFFF97316U;

    const float top_h   = 48.0f;
    const float left_w  = 46.0f;
    const float right_w = 320.0f;
    const float price_w = 76.0f;
    const float time_h  = 26.0f;

    UILayoutCache* L = &r->layout;
    ZeroMemory(L, sizeof(*L));

    ID2D1HwndRenderTarget_BeginDraw(r->target);
    D2D1_COLOR_F bg_cf = {
        (float)((col_bg >> 16) & 0xFF) / 255.0f,
        (float)((col_bg >> 8)  & 0xFF) / 255.0f,
        (float)(col_bg & 0xFF) / 255.0f,
        1.0f
    };
    ID2D1HwndRenderTarget_Clear(r->target, &bg_cf);

    /* =====================================================================
     * ZONE 1: TOP TOOLBAR
     * ===================================================================== */
    D2D1_RECT_F top_rc = { 0, 0, w_dip, top_h };
    set_brush_argb(r->brush_dyn, col_panel, 1.0f);
    ID2D1HwndRenderTarget_FillRectangle(r->target, &top_rc, (ID2D1Brush*)r->brush_dyn);
    set_brush_argb(r->brush_dyn, col_border, 1.0f);
    ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(0, top_h), d2d_pt(w_dip, top_h), (ID2D1Brush*)r->brush_dyn, 1.0f, NULL);

    draw_txt(r, r->font_ui_bold, L"OpenView", (D2D1_RECT_F){ 14.0f, 13.0f, 100.0f, top_h }, col_bright);

    /* Symbol selector button */
    L->symbol_btn = (D2D1_RECT_F){ 102.0f, 10.0f, 206.0f, 38.0f };
    wchar_t sym_lbl[32];
    swprintf(sym_lbl, 32, L"%ls ▾", feed->active_symbol);
    draw_btn_box(r, L->symbol_btn, col_surface, col_border, sym_lbl, col_bright, r->font_small);

    /* 9 Timeframes */
    static const TimeframeSec kTfs[9] = {
        TF_1M, TF_5M, TF_15M, TF_1H, TF_4H, TF_1D, TF_1W, TF_1MO, TF_1Y
    };
    float tx = 216.0f;
    for (int i = 0; i < 9; i++) {
        L->tf_values[i] = kTfs[i];
        L->tf_btns[i]   = (D2D1_RECT_F){ tx, 10.0f, tx + 32.0f, 38.0f };
        bool active_tf  = (feed->timeframe == kTfs[i]);
        draw_btn_box(
            r, L->tf_btns[i],
            active_tf ? col_accent : col_surface,
            active_tf ? col_accent : col_border,
            ov_tf_label(kTfs[i]),
            active_tf ? 0xFFFFFFFFU : col_muted,
            r->font_small
        );
        tx += 34.0f;
    }

    /* Right-side toolbar actions */
    float bx = w_dip - 14.0f;
    L->theme_btn = (D2D1_RECT_F){ bx - 64.0f, 10.0f, bx, 38.0f };
    draw_btn_box(r, L->theme_btn, col_surface, col_border, dark ? L"Clair" : L"Sombre", col_text, r->font_small);
    bx -= 72.0f;

    int ov_cnt = 0;
    for (int s = 0; s < SRC_COUNT; s++) {
        if (s != (int)feed->primary_source && feed->overlay_sources[s]) ov_cnt++;
    }
    wchar_t src_btn_lbl[48];
    if (ov_cnt > 0) swprintf(src_btn_lbl, 48, L"Sources: %ls +%d", sources[feed->primary_source].label, ov_cnt);
    else            swprintf(src_btn_lbl, 48, L"Sources: %ls", sources[feed->primary_source].label);
    L->sources_btn = (D2D1_RECT_F){ bx - 134.0f, 10.0f, bx, 38.0f };
    if (L->sources_btn.left > tx + 8.0f) {
        draw_btn_box(r, L->sources_btn, col_surface, col_border, src_btn_lbl, col_text, r->font_small);
    }
    bx -= 142.0f;

    wchar_t ind_btn_lbl[32];
    swprintf(ind_btn_lbl, 32, L"Indicateurs (%d)", indicators_active_count(ind_cfg));
    L->ind_btn = (D2D1_RECT_F){ bx - 110.0f, 10.0f, bx, 38.0f };
    if (L->ind_btn.left > tx + 8.0f) {
        draw_btn_box(r, L->ind_btn, col_surface, col_border, ind_btn_lbl, col_text, r->font_small);
    }

    const uint64_t now_ms = GetTickCount64();
    const bool graph_has_data = primary_count > 0;
    const bool has_recent_tick =
        vp->last_primary_tick_ms > 0 &&
        now_ms >= vp->last_primary_tick_ms &&
        now_ms - vp->last_primary_tick_ms < 5000;
    const SourceStatusInfo* primary_status = &sources[feed->primary_source];
    const bool pulse_on = graph_has_data && has_recent_tick && ((now_ms / 400) % 2 == 0);
    uint32_t activity_color = col_muted;
    const wchar_t* activity_label = L"EN ATTENTE";
    if (!graph_has_data) {
        activity_label = L"CHARGEMENT";
    } else if (has_recent_tick && primary_status->state == STATE_LIVE) {
        activity_color = col_bull;
        activity_label = L"LIVE";
    } else if (has_recent_tick && primary_status->state == STATE_FALLBACK) {
        activity_color = col_ema1;
        activity_label = L"RELAIS";
    } else if (primary_status->state == STATE_STATIC) {
        activity_color = col_accent;
        activity_label = L"HISTORIQUE";
    } else if (primary_status->state == STATE_LIVE) {
        activity_color = col_bull;
        activity_label = L"CONNECTÉ";
    } else if (primary_status->state == STATE_FALLBACK) {
        activity_color = col_ema1;
        activity_label = L"SECOURS";
    } else if (primary_status->state == STATE_OFFLINE) {
        activity_color = col_bear;
        activity_label = L"HORS LIGNE";
    } else {
        activity_color = col_ema1;
        activity_label = L"SYNC";
    }

    const float activity_x = tx + 10.0f;
    if (activity_x + 104.0f < L->ind_btn.left - 4.0f) {
        D2D1_ELLIPSE activity_dot = {
            { activity_x + 5.0f, 24.0f },
            pulse_on ? 4.5f : 3.5f,
            pulse_on ? 4.5f : 3.5f
        };
        set_brush_argb(r->brush_dyn, activity_color, pulse_on ? 1.0f : 0.45f);
        ID2D1HwndRenderTarget_FillEllipse(r->target, &activity_dot, (ID2D1Brush*)r->brush_dyn);
        draw_txt(
            r, r->font_small, activity_label,
            (D2D1_RECT_F){ activity_x + 14.0f, 12.0f, activity_x + 104.0f, 36.0f },
            activity_color
        );
    }

    /* =====================================================================
     * ZONE 2: LEFT DRAWING SIDEBAR
     * ===================================================================== */
    D2D1_RECT_F left_rc = { 0, top_h, left_w, h_dip };
    set_brush_argb(r->brush_dyn, col_panel, 1.0f);
    ID2D1HwndRenderTarget_FillRectangle(r->target, &left_rc, (ID2D1Brush*)r->brush_dyn);
    set_brush_argb(r->brush_dyn, col_border, 1.0f);
    ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(left_w, top_h), d2d_pt(left_w, h_dip), (ID2D1Brush*)r->brush_dyn, 1.0f, NULL);

    float ty = top_h + 10.0f;
    for (int t = 0; t < 7; t++) {
        L->tool_btns[t] = (D2D1_RECT_F){ 7.0f, ty, left_w - 7.0f, ty + 30.0f };
        bool act = ((int)vp->active_tool == t);
        draw_btn_box(
            r, L->tool_btns[t],
            act ? col_accent : 0,
            act ? col_accent : col_border,
            NULL, 0, r->font_small
        );
        draw_tool_glyph(r, t, L->tool_btns[t], act ? 0xFFFFFFFFU : col_text);
        ty += 35.0f;
    }

    ty += 8.0f;
    for (int c = 0; c < 7; c++) {
        L->swatch_colors[c] = kPaletteSwatches[c];
        L->swatch_btns[c]   = (D2D1_RECT_F){ 12.0f, ty, left_w - 12.0f, ty + 16.0f };
        D2D1_ROUNDED_RECT sr = { L->swatch_btns[c], 3.0f, 3.0f };
        set_brush_argb(r->brush_dyn, kPaletteSwatches[c], 1.0f);
        ID2D1HwndRenderTarget_FillRoundedRectangle(r->target, &sr, (ID2D1Brush*)r->brush_dyn);
        if (vp->drawing_color == kPaletteSwatches[c]) {
            set_brush_argb(r->brush_dyn, col_bright, 1.0f);
            ID2D1HwndRenderTarget_DrawRoundedRectangle(r->target, &sr, (ID2D1Brush*)r->brush_dyn, 1.6f, NULL);
        }
        ty += 21.0f;
    }

    float by = h_dip - 108.0f;
    if (by > ty + 6.0f) {
        L->undo_btn  = (D2D1_RECT_F){ 6.0f, by,      left_w - 6.0f, by + 26.0f };
        L->del_btn   = (D2D1_RECT_F){ 6.0f, by + 32, left_w - 6.0f, by + 58.0f };
        L->clear_btn = (D2D1_RECT_F){ 6.0f, by + 64, left_w - 6.0f, by + 90.0f };
        draw_btn_box(r, L->undo_btn,  col_surface, col_border, L"Ann.", col_text, r->font_small);
        draw_btn_box(r, L->del_btn,   col_surface, col_border, L"Suppr", vp->selected_drawing_id ? col_bear : col_muted, r->font_small);
        draw_btn_box(r, L->clear_btn, col_surface, col_border, L"Vider", col_muted, r->font_small);
    }

    /* =====================================================================
     * ZONE 3: CENTRAL CHART CANVAS
     * ===================================================================== */
    const float c_left   = left_w;
    const float c_right  = w_dip - right_w - price_w;
    const float c_top    = top_h;
    const float c_bot    = h_dip - time_h;
    const float c_w      = (c_right - c_left > 100.0f) ? (c_right - c_left) : 100.0f;
    const float total_ch = (c_bot - c_top > 120.0f) ? (c_bot - c_top) : 120.0f;

    int sub_cnt = (ind_cfg->enable_rsi ? 1 : 0) + (ind_cfg->enable_macd ? 1 : 0);
    float sub_h = 0.0f;
    if (sub_cnt > 0) {
        sub_h = total_ch * 0.19f;
        if (sub_h < 78.0f) sub_h = 78.0f;
        if (sub_h > 125.0f) sub_h = 125.0f;
    }
    const float main_top = c_top;
    const float main_bot = c_bot - (float)sub_cnt * sub_h;
    const float main_h   = (main_bot - main_top > 80.0f) ? (main_bot - main_top) : 80.0f;

    L->chart_rect      = (D2D1_RECT_F){ c_left, c_top, c_right, c_bot };
    L->main_pane_rect  = (D2D1_RECT_F){ c_left, main_top, c_right, main_bot };
    L->price_axis_rect = (D2D1_RECT_F){ c_right, c_top, c_right + price_w, c_bot };

    /* Price & time axis separators */
    set_brush_argb(r->brush_dyn, col_border, 1.0f);
    ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(c_right, c_top), d2d_pt(c_right, c_bot), (ID2D1Brush*)r->brush_dyn, 1.0f, NULL);
    ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(c_left, c_bot), d2d_pt(c_right + price_w, c_bot), (ID2D1Brush*)r->brush_dyn, 1.0f, NULL);

    if (primary_count > 0) {
        float step = vp->bar_width;
        if (step < 3.5f) step = 3.5f;
        int vis_cnt = (int)(c_w / step);
        int end_idx = (int)primary_count - 1 + (int)vp->pan_bars;
        /* Keep clamp inside available bars for rendering window */
        int render_end = end_idx;
        if (render_end >= (int)primary_count) render_end = (int)primary_count - 1;
        if (render_end < 0) render_end = 0;
        int start_idx = end_idx - vis_cnt - 1;
        if (start_idx < 0) start_idx = 0;

        double min_p = primary_bars[start_idx].low;
        double max_p = primary_bars[start_idx].high;
        double max_v = 0.0;
        for (int i = start_idx; i <= render_end; i++) {
            if (primary_bars[i].low < min_p)    min_p = primary_bars[i].low;
            if (primary_bars[i].high > max_p)   max_p = primary_bars[i].high;
            if (primary_bars[i].volume > max_v) max_v = primary_bars[i].volume;
            if (ind_cfg->enable_ema_fast && ind->valid_ema_fast[i]) {
                if (ind->ema_fast[i] < min_p) min_p = ind->ema_fast[i];
                if (ind->ema_fast[i] > max_p) max_p = ind->ema_fast[i];
            }
            if (ind_cfg->enable_ema_slow && ind->valid_ema_slow[i]) {
                if (ind->ema_slow[i] < min_p) min_p = ind->ema_slow[i];
                if (ind->ema_slow[i] > max_p) max_p = ind->ema_slow[i];
            }
            if (ind_cfg->enable_sma && ind->valid_sma[i]) {
                if (ind->sma[i] < min_p) min_p = ind->sma[i];
                if (ind->sma[i] > max_p) max_p = ind->sma[i];
            }
            if (ind_cfg->enable_bb && ind->valid_bb[i]) {
                if (ind->bb_lower[i] < min_p) min_p = ind->bb_lower[i];
                if (ind->bb_upper[i] > max_p) max_p = ind->bb_upper[i];
            }
        }

        double mid_p = (min_p + max_p) * 0.5;
        double span  = max_p - min_p;
        if (span < mid_p * 0.002) span = (mid_p > 0.0 ? mid_p * 0.002 : 1.0);
        double scaled = span * 1.18 * (double)(vp->y_scale_factor > 0.1f ? vp->y_scale_factor : 1.0f);
        double y_shift = scaled * (double)vp->y_pan_factor;
        min_p = mid_p - scaled * 0.5 + y_shift;
        max_p = mid_p + scaled * 0.5 + y_shift;

        L->min_price  = min_p;
        L->max_price  = max_p;
        L->start_idx  = start_idx;
        L->end_idx    = end_idx;
        L->step_px    = step;
        L->bar_count  = primary_count;
        L->first_time = primary_bars[0].time_utc;
        L->last_time  = primary_bars[primary_count - 1].time_utc;
        L->tf         = feed->timeframe;

        #define P2Y(p) (main_bot - (float)(((p) - min_p) / (max_p - min_p)) * main_h)
        #define I2X(i) (c_right - (float)(end_idx - (i) + 1) * step + step * 0.5f)

        /* Horizontal grid & price axis ticks */
        for (int g = 0; g <= 5; g++) {
            float frac = (float)g / 5.0f;
            float gy = main_top + 14.0f + frac * (main_h - 28.0f);
            double gp = max_p - ((gy - main_top) / main_h) * (max_p - min_p);
            set_brush_argb(r->brush_dyn, col_grid, 1.0f);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(c_left, gy), d2d_pt(c_right, gy), (ID2D1Brush*)r->brush_dyn, 1.0f, NULL);

            wchar_t pbuf[32];
            format_price_w(gp, pbuf, 32);
            draw_txt(r, r->font_mono, pbuf, (D2D1_RECT_F){ c_right + 6.0f, gy - 7.0f, c_right + price_w - 2.0f, gy + 10.0f }, col_muted);
        }

        /* Vertical grid & time axis ticks */
        int t_step = (vis_cnt / 7);
        if (t_step < 1) t_step = 1;
        for (int i = start_idx; i <= render_end; i += t_step) {
            float vx = I2X(i);
            if (vx < c_left + 12.0f || vx > c_right - 12.0f) continue;
            set_brush_argb(r->brush_dyn, col_grid, 1.0f);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(vx, c_top), d2d_pt(vx, c_bot), (ID2D1Brush*)r->brush_dyn, 1.0f, NULL);
            wchar_t tbuf[32];
            format_time_w(primary_bars[i].time_utc, feed->timeframe, tbuf, 32);
            IDWriteTextFormat_SetTextAlignment(r->font_mono, DWRITE_TEXT_ALIGNMENT_CENTER);
            draw_txt(r, r->font_mono, tbuf, (D2D1_RECT_F){ vx - 45.0f, c_bot + 5.0f, vx + 45.0f, h_dip }, col_muted);
            IDWriteTextFormat_SetTextAlignment(r->font_mono, DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        /* Clip main pane */
        ID2D1HwndRenderTarget_PushAxisAlignedClip(r->target, &L->main_pane_rect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        /* Volume bars + Candlesticks */
        float vol_h = main_h * 0.16f;
        float bw = step * 0.65f;
        if (bw < 1.5f) bw = 1.5f;

        for (int i = start_idx; i <= render_end; i++) {
            const CandleBar* b = &primary_bars[i];
            float x  = I2X(i);
            float yh = P2Y(b->high);
            float yl = P2Y(b->low);
            float yo = P2Y(b->open);
            float yc = P2Y(b->close);
            uint32_t ccol = (b->close >= b->open) ? col_bull : col_bear;

            if (max_v > 0.0) {
                float vh = (float)(b->volume / max_v) * vol_h;
                D2D1_RECT_F vrc = { x - bw * 0.5f, main_bot - vh, x + bw * 0.5f, main_bot };
                set_brush_argb(r->brush_dyn, ccol, 0.22f);
                ID2D1HwndRenderTarget_FillRectangle(r->target, &vrc, (ID2D1Brush*)r->brush_dyn);
            }

            set_brush_argb(r->brush_dyn, ccol, 1.0f);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(x, yh), d2d_pt(x, yl), (ID2D1Brush*)r->brush_dyn, 1.1f, NULL);

            float bt = (yo < yc) ? yo : yc;
            float bb = (yo > yc) ? yo : yc;
            if (bb - bt < 1.5f) bb = bt + 1.5f;
            D2D1_RECT_F brc = { x - bw * 0.5f, bt, x + bw * 0.5f, bb };
            ID2D1HwndRenderTarget_FillRectangle(r->target, &brc, (ID2D1Brush*)r->brush_dyn);
        }

        /* Multi-source overlay curves */
        static const uint32_t kSrcColors[SRC_COUNT] = {
            0xFFF59E0BU, 0xFF38BDF8U, 0xFFA855F7U, 0xFF22C55EU, 0xFF14B8A6U
        };
        for (int s = 0; s < SRC_COUNT; s++) {
            if (s == (int)feed->primary_source || !feed->overlay_sources[s]) continue;
            size_t scnt = feed->candle_counts[s];
            const CandleBar* sbars = feed->candles[s];
            if (scnt < 2) continue;
            set_brush_argb(r->brush_dyn, kSrcColors[s], 0.9f);
            for (size_t i = 1; i < scnt; i++) {
                float x0 = d2d_time_to_x(L, primary_bars, sbars[i - 1].time_utc);
                float x1 = d2d_time_to_x(L, primary_bars, sbars[i].time_utc);
                if (x1 < c_left || x0 > c_right) continue;
                ID2D1HwndRenderTarget_DrawLine(
                    r->target,
                    d2d_pt(x0, P2Y(sbars[i - 1].close)),
                    d2d_pt(x1, P2Y(sbars[i].close)),
                    (ID2D1Brush*)r->brush_dyn, 1.6f, r->stroke_dashed
                );
            }
        }

        /* Overlay technical indicators */
        for (int i = start_idx + 1; i <= render_end; i++) {
            float x0 = I2X(i - 1), x1 = I2X(i);
            if (ind_cfg->enable_ema_fast && ind->valid_ema_fast[i] && ind->valid_ema_fast[i - 1]) {
                set_brush_argb(r->brush_dyn, col_ema1, 1.0f);
                ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(x0, P2Y(ind->ema_fast[i - 1])), d2d_pt(x1, P2Y(ind->ema_fast[i])), (ID2D1Brush*)r->brush_dyn, 1.5f, NULL);
            }
            if (ind_cfg->enable_ema_slow && ind->valid_ema_slow[i] && ind->valid_ema_slow[i - 1]) {
                set_brush_argb(r->brush_dyn, col_ema2, 1.0f);
                ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(x0, P2Y(ind->ema_slow[i - 1])), d2d_pt(x1, P2Y(ind->ema_slow[i])), (ID2D1Brush*)r->brush_dyn, 1.5f, NULL);
            }
            if (ind_cfg->enable_sma && ind->valid_sma[i] && ind->valid_sma[i - 1]) {
                set_brush_argb(r->brush_dyn, col_sma, 1.0f);
                ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(x0, P2Y(ind->sma[i - 1])), d2d_pt(x1, P2Y(ind->sma[i])), (ID2D1Brush*)r->brush_dyn, 1.5f, NULL);
            }
            if (ind_cfg->enable_bb && ind->valid_bb[i] && ind->valid_bb[i - 1]) {
                set_brush_argb(r->brush_dyn, col_bb, 0.85f);
                ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(x0, P2Y(ind->bb_upper[i - 1])), d2d_pt(x1, P2Y(ind->bb_upper[i])), (ID2D1Brush*)r->brush_dyn, 1.2f, NULL);
                ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(x0, P2Y(ind->bb_lower[i - 1])), d2d_pt(x1, P2Y(ind->bb_lower[i])), (ID2D1Brush*)r->brush_dyn, 1.2f, NULL);
                set_brush_argb(r->brush_dyn, col_muted, 0.7f);
                ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(x0, P2Y(ind->bb_mid[i - 1])), d2d_pt(x1, P2Y(ind->bb_mid[i])), (ID2D1Brush*)r->brush_dyn, 1.0f, r->stroke_dashed);
            }
        }

        /* Render all drawings scoped to active watchlist entry */
        bool is_primary_entry = true;
        if (feed->active_entry_id != 0) {
            for (size_t w = 0; w < watchlist_count; w++) {
                if (!watchlist[w].removed && wcscmp(watchlist[w].symbol, feed->active_symbol) == 0) {
                    is_primary_entry = (watchlist[w].id == feed->active_entry_id);
                    break;
                }
            }
        }
        for (size_t d = 0; d < drawings->count; d++) {
            const ChartDrawing* dr = &drawings->items[d];
            if (!drawings_matches_scope(dr, feed->active_entry_id, feed->active_symbol, is_primary_entry)) continue;

            float sx[3], sy[3];
            for (size_t p = 0; p < dr->pt_count && p < 3; p++) {
                sx[p] = d2d_time_to_x(L, primary_bars, dr->pts[p].time_utc);
                sy[p] = P2Y(dr->pts[p].price);
            }

            ID2D1StrokeStyle* sstyle = NULL;
            if (dr->line_style == LINE_DASHED)      sstyle = r->stroke_dashed;
            else if (dr->line_style == LINE_DOTTED) sstyle = r->stroke_dotted;

            set_brush_argb(r->brush_dyn, dr->argb_color, 1.0f);
            float sw = (dr->stroke_width > 0.2f) ? dr->stroke_width : 1.8f;

            if (dr->kind == TOOL_HLINE && dr->pt_count >= 1) {
                ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(c_left, sy[0]), d2d_pt(c_right, sy[0]), (ID2D1Brush*)r->brush_dyn, sw, sstyle);
                wchar_t hlbl[96];
                format_price_w(dr->pts[0].price, hlbl, 32);
                if (dr->label[0]) {
                    wchar_t ponly[32];
                    ov_wcscpy(ponly, 32, hlbl);
                    swprintf(hlbl, 96, L"%ls · %ls", dr->label, ponly);
                }
                draw_aligned_drawing_label(
                    r, hlbl, c_left + 4.0f, c_right - 4.0f, sy[0], sy[0],
                    dr->label_align_h, dr->label_align_v, dr->argb_color, col_bg
                );
            } else if (dr->kind == TOOL_TRENDLINE && dr->pt_count >= 2) {
                ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(sx[0], sy[0]), d2d_pt(sx[1], sy[1]), (ID2D1Brush*)r->brush_dyn, sw, sstyle);
                if (dr->label[0]) {
                    bool left_first = (sx[0] <= sx[1]);
                    float lx = left_first ? sx[0] : sx[1];
                    float ly = left_first ? sy[0] : sy[1];
                    float rx = left_first ? sx[1] : sx[0];
                    float ry = left_first ? sy[1] : sy[0];
                    float ax = (dr->label_align_h == ALIGN_H_LEFT) ? lx : (dr->label_align_h == ALIGN_H_RIGHT ? rx : (lx + rx) * 0.5f);
                    float ay = (dr->label_align_h == ALIGN_H_LEFT) ? ly : (dr->label_align_h == ALIGN_H_RIGHT ? ry : (ly + ry) * 0.5f);
                    draw_aligned_drawing_label(
                        r, dr->label, ax, ax, ay, ay,
                        dr->label_align_h, dr->label_align_v, dr->argb_color, col_bg
                    );
                }
            } else if (dr->kind == TOOL_FIBONACCI && dr->pt_count >= 2) {
                float lx = (sx[0] < sx[1]) ? sx[0] : sx[1];
                float rx = (sx[0] > sx[1]) ? sx[0] : sx[1];
                if (rx - lx < 40.0f) rx = lx + 120.0f;
                float yt = (sy[0] < sy[1]) ? sy[0] : sy[1];
                float yb = (sy[0] > sy[1]) ? sy[0] : sy[1];
                set_brush_argb(r->brush_dyn, dr->argb_color, 0.08f);
                D2D1_RECT_F frc = { lx, yt, rx, yb };
                ID2D1HwndRenderTarget_FillRectangle(r->target, &frc, (ID2D1Brush*)r->brush_dyn);
                for (int k = 0; k < 7; k++) {
                    double lvl_p = dr->pts[0].price + (dr->pts[1].price - dr->pts[0].price) * kFibLevels[k];
                    float ly = P2Y(lvl_p);
                    set_brush_argb(r->brush_dyn, dr->argb_color, 0.9f);
                    ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(lx, ly), d2d_pt(rx, ly), (ID2D1Brush*)r->brush_dyn, 1.1f, sstyle ? sstyle : r->stroke_dashed);
                    wchar_t flbl[48], fp[24];
                    format_price_w(lvl_p, fp, 24);
                    swprintf(flbl, 48, L"%.1f%% (%ls)", kFibLevels[k] * 100.0f, fp);
                    draw_txt(r, r->font_small, flbl, (D2D1_RECT_F){ lx + 4.0f, ly - 14.0f, rx, ly }, dr->argb_color);
                }
                if (dr->label[0]) {
                    draw_aligned_drawing_label(
                        r, dr->label, lx, rx, yt, yb,
                        dr->label_align_h, dr->label_align_v, dr->argb_color, col_bg
                    );
                }
            } else if (dr->kind == TOOL_RECTANGLE && dr->pt_count >= 2) {
                float x0 = (sx[0] < sx[1]) ? sx[0] : sx[1];
                float x1 = (sx[0] > sx[1]) ? sx[0] : sx[1];
                float y0 = (sy[0] < sy[1]) ? sy[0] : sy[1];
                float y1 = (sy[0] > sy[1]) ? sy[0] : sy[1];
                D2D1_RECT_F rrc = { x0, y0, x1, y1 };
                set_brush_argb(r->brush_dyn, dr->argb_color, 0.14f);
                ID2D1HwndRenderTarget_FillRectangle(r->target, &rrc, (ID2D1Brush*)r->brush_dyn);
                set_brush_argb(r->brush_dyn, dr->argb_color, 1.0f);
                ID2D1HwndRenderTarget_DrawRectangle(r->target, &rrc, (ID2D1Brush*)r->brush_dyn, sw, sstyle);
                if (dr->label[0]) {
                    draw_aligned_drawing_label(
                        r, dr->label, x0, x1, y0, y1,
                        dr->label_align_h, dr->label_align_v, dr->argb_color, col_bg
                    );
                }
            } else if (dr->kind == TOOL_CHANNEL && dr->pt_count >= 3) {
                float off_y = sy[2] - sy[0];
                set_brush_argb(r->brush_dyn, dr->argb_color, 1.0f);
                ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(sx[0], sy[0]), d2d_pt(sx[1], sy[1]), (ID2D1Brush*)r->brush_dyn, sw, sstyle);
                ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(sx[0], sy[0] + off_y), d2d_pt(sx[1], sy[1] + off_y), (ID2D1Brush*)r->brush_dyn, sw, sstyle);
                if (dr->label[0]) {
                    bool left_first = (sx[0] <= sx[1]);
                    float lx = left_first ? sx[0] : sx[1];
                    float ly = left_first ? sy[0] : sy[1];
                    float rx = left_first ? sx[1] : sx[0];
                    float ry = left_first ? sy[1] : sy[0];
                    float ax = (dr->label_align_h == ALIGN_H_LEFT) ? lx : (dr->label_align_h == ALIGN_H_RIGHT ? rx : (lx + rx) * 0.5f);
                    float ay = (dr->label_align_h == ALIGN_H_LEFT) ? ly : (dr->label_align_h == ALIGN_H_RIGHT ? ry : (ly + ry) * 0.5f);
                    float yt = (ay < ay + off_y) ? ay : (ay + off_y);
                    float yb = (ay > ay + off_y) ? ay : (ay + off_y);
                    draw_aligned_drawing_label(
                        r, dr->label, ax, ax, yt, yb,
                        dr->label_align_h, dr->label_align_v, dr->argb_color, col_bg
                    );
                }
            } else if (dr->kind == TOOL_TEXT && dr->pt_count >= 1) {
                D2D1_RECT_F trc = calc_text_tool_rect(sx[0], sy[0], dr);
                draw_btn_box(r, trc, col_panel, dr->argb_color, dr->label[0] ? dr->label : L"Note", dr->argb_color, r->font_small);
            }

            if (dr->id == vp->selected_drawing_id) {
                for (size_t p = 0; p < dr->pt_count && p < 3; p++) {
                    D2D1_RECT_F hrc = { sx[p] - 4.0f, sy[p] - 4.0f, sx[p] + 4.0f, sy[p] + 4.0f };
                    set_brush_argb(r->brush_dyn, 0xFFFFFFFFU, 1.0f);
                    ID2D1HwndRenderTarget_FillRectangle(r->target, &hrc, (ID2D1Brush*)r->brush_dyn);
                    set_brush_argb(r->brush_dyn, dr->argb_color, 1.0f);
                    ID2D1HwndRenderTarget_DrawRectangle(r->target, &hrc, (ID2D1Brush*)r->brush_dyn, 1.5f, NULL);
                }
            }
        }

        /* Live draft preview */
        if (vp->active_tool != TOOL_CURSOR && vp->draft_count > 0) {
            float dx0 = d2d_time_to_x(L, primary_bars, vp->draft_pts[0].time_utc);
            float dy0 = P2Y(vp->draft_pts[0].price);
            float mx  = vp->mouse_x / dpi;
            float my  = vp->mouse_y / dpi;
            set_brush_argb(r->brush_dyn, vp->drawing_color, 0.8f);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(dx0, dy0), d2d_pt(mx, my), (ID2D1Brush*)r->brush_dyn, 1.5f, r->stroke_dashed);
        }

        ID2D1HwndRenderTarget_PopAxisAlignedClip(r->target);

        /* Last price horizontal badge on right axis */
        const CandleBar* last_bar = &primary_bars[primary_count - 1];
        float last_y = P2Y(last_bar->close);
        if (last_y >= main_top && last_y <= main_bot) {
            uint32_t lcol = (last_bar->close >= last_bar->open) ? col_bull : col_bear;
            set_brush_argb(r->brush_dyn, lcol, 0.45f);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(c_left, last_y), d2d_pt(c_right, last_y), (ID2D1Brush*)r->brush_dyn, 1.0f, r->stroke_dotted);

            wchar_t lbuf[32];
            format_price_w(last_bar->close, lbuf, 32);
            D2D1_RECT_F tag_rc = { c_right + 2.0f, last_y - 10.0f, c_right + price_w - 2.0f, last_y + 10.0f };
            draw_btn_box(r, tag_rc, lcol, lcol, lbuf, 0xFFFFFFFFU, r->font_mono);
        }

        /* =================================================================
         * SUB-PANES: RSI & MACD
         * ================================================================= */
        float next_sub_top = main_bot;
        if (ind_cfg->enable_rsi) {
            float rt = next_sub_top;
            float rb = rt + sub_h;
            next_sub_top = rb;

            set_brush_argb(r->brush_dyn, col_border, 1.0f);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(c_left, rt), d2d_pt(c_right + price_w, rt), (ID2D1Brush*)r->brush_dyn, 1.0f, NULL);

            float y70 = rb - 0.70f * sub_h;
            float y30 = rb - 0.30f * sub_h;
            set_brush_argb(r->brush_dyn, col_muted, 0.4f);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(c_left, y70), d2d_pt(c_right, y70), (ID2D1Brush*)r->brush_dyn, 1.0f, r->stroke_dashed);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(c_left, y30), d2d_pt(c_right, y30), (ID2D1Brush*)r->brush_dyn, 1.0f, r->stroke_dashed);

            set_brush_argb(r->brush_dyn, col_bb, 1.0f);
            for (int i = start_idx + 1; i <= render_end; i++) {
                if (ind->valid_rsi[i] && ind->valid_rsi[i - 1]) {
                    float y0 = rb - (float)(ind->rsi[i - 1] / 100.0) * sub_h;
                    float y1 = rb - (float)(ind->rsi[i]     / 100.0) * sub_h;
                    ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(I2X(i - 1), y0), d2d_pt(I2X(i), y1), (ID2D1Brush*)r->brush_dyn, 1.5f, NULL);
                }
            }
            wchar_t rlbl[48];
            if (ind->valid_rsi[primary_count - 1]) swprintf(rlbl, 48, L"RSI (%d): %.1f", ind_cfg->rsi_period, ind->rsi[primary_count - 1]);
            else                                   swprintf(rlbl, 48, L"RSI (%d)", ind_cfg->rsi_period);
            draw_txt(r, r->font_small, rlbl, (D2D1_RECT_F){ c_left + 10.0f, rt + 4.0f, c_left + 180.0f, rt + 22.0f }, col_bb);
            draw_txt(r, r->font_mono, L"70", (D2D1_RECT_F){ c_right + 6.0f, y70 - 7.0f, c_right + 60.0f, y70 + 10.0f }, col_muted);
            draw_txt(r, r->font_mono, L"30", (D2D1_RECT_F){ c_right + 6.0f, y30 - 7.0f, c_right + 60.0f, y30 + 10.0f }, col_muted);
        }

        if (ind_cfg->enable_macd) {
            float mt = next_sub_top;
            set_brush_argb(r->brush_dyn, col_border, 1.0f);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(c_left, mt), d2d_pt(c_right + price_w, mt), (ID2D1Brush*)r->brush_dyn, 1.0f, NULL);

            double max_abs = 0.0001;
            for (int i = start_idx; i <= render_end; i++) {
                if (!ind->valid_macd[i]) continue;
                double a1 = fabs(ind->macd_line[i]);
                double a2 = fabs(ind->macd_signal[i]);
                double a3 = fabs(ind->macd_hist[i]);
                if (a1 > max_abs) max_abs = a1;
                if (a2 > max_abs) max_abs = a2;
                if (a3 > max_abs) max_abs = a3;
            }
            float y_zero = mt + sub_h * 0.5f;
            float scale_m = (sub_h * 0.42f) / (float)max_abs;
            set_brush_argb(r->brush_dyn, col_muted, 0.35f);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(c_left, y_zero), d2d_pt(c_right, y_zero), (ID2D1Brush*)r->brush_dyn, 1.0f, r->stroke_dashed);

            for (int i = start_idx; i <= render_end; i++) {
                if (!ind->valid_macd[i]) continue;
                float x = I2X(i);
                float yh = y_zero - (float)ind->macd_hist[i] * scale_m;
                D2D1_RECT_F hrc = { x - bw * 0.4f, (yh < y_zero ? yh : y_zero), x + bw * 0.4f, (yh > y_zero ? yh : y_zero) };
                set_brush_argb(r->brush_dyn, ind->macd_hist[i] >= 0.0 ? col_bull : col_bear, 0.65f);
                ID2D1HwndRenderTarget_FillRectangle(r->target, &hrc, (ID2D1Brush*)r->brush_dyn);

                if (i > start_idx && ind->valid_macd[i - 1]) {
                    float x0 = I2X(i - 1);
                    set_brush_argb(r->brush_dyn, col_accent, 1.0f);
                    ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(x0, y_zero - (float)ind->macd_line[i - 1] * scale_m), d2d_pt(x, y_zero - (float)ind->macd_line[i] * scale_m), (ID2D1Brush*)r->brush_dyn, 1.3f, NULL);
                    set_brush_argb(r->brush_dyn, col_macd_sig, 1.0f);
                    ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(x0, y_zero - (float)ind->macd_signal[i - 1] * scale_m), d2d_pt(x, y_zero - (float)ind->macd_signal[i] * scale_m), (ID2D1Brush*)r->brush_dyn, 1.3f, NULL);
                }
            }
            wchar_t mlbl[64];
            swprintf(mlbl, 64, L"MACD (%d,%d,%d)", ind_cfg->macd_fast, ind_cfg->macd_slow, ind_cfg->macd_signal);
            draw_txt(r, r->font_small, mlbl, (D2D1_RECT_F){ c_left + 10.0f, mt + 4.0f, c_left + 200.0f, mt + 22.0f }, col_accent);
        }

        /* Top-left OHLCV Readout */
        float mx_dip = vp->mouse_x / dpi;
        float my_dip = vp->mouse_y / dpi;
        int hov_idx = (int)primary_count - 1;
        if (vp->mouse_inside && mx_dip >= c_left && mx_dip < c_right && my_dip >= c_top && my_dip <= c_bot) {
            float idx_f = (float)end_idx + 1.0f - (c_right - mx_dip + step * 0.5f) / step;
            int cand = (int)roundf(idx_f);
            if (cand >= 0 && cand < (int)primary_count) hov_idx = cand;
        }
        const CandleBar* hb = &primary_bars[hov_idx];
        wchar_t po[24], ph[24], pl[24], pc[24], ohlc[220];
        format_price_w(hb->open, po, 24);
        format_price_w(hb->high, ph, 24);
        format_price_w(hb->low, pl, 24);
        format_price_w(hb->close, pc, 24);
        swprintf(
            ohlc, 220,
            L"%ls · %ls · %ls   O %ls  H %ls  L %ls  C %ls  V %.1f",
            feed->active_symbol, ov_tf_label(feed->timeframe), sources[feed->primary_source].label,
            po, ph, pl, pc, hb->volume
        );
        draw_txt(r, r->font_small, ohlc, (D2D1_RECT_F){ c_left + 12.0f, c_top + 8.0f, c_right - 8.0f, c_top + 28.0f }, col_text);

        /* Crosshair + Axis tags */
        if (vp->mouse_inside && mx_dip >= c_left && mx_dip <= c_right && my_dip >= c_top && my_dip <= c_bot) {
            set_brush_argb(r->brush_dyn, col_muted, 0.7f);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(c_left, my_dip), d2d_pt(c_right, my_dip), (ID2D1Brush*)r->brush_dyn, 0.8f, r->stroke_dashed);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(mx_dip, c_top), d2d_pt(mx_dip, c_bot), (ID2D1Brush*)r->brush_dyn, 0.8f, r->stroke_dashed);

            if (my_dip <= main_bot) {
                double hp = d2d_y_to_price(L, my_dip);
                wchar_t hpbuf[24];
                format_price_w(hp, hpbuf, 24);
                draw_btn_box(r, (D2D1_RECT_F){ c_right + 2.0f, my_dip - 10.0f, c_right + price_w - 2.0f, my_dip + 10.0f }, col_border, col_border, hpbuf, col_bright, r->font_mono);
            }
            int64_t ht = d2d_x_to_time(L, primary_bars, mx_dip);
            wchar_t htbuf[32];
            format_time_w(ht, feed->timeframe, htbuf, 32);
            draw_btn_box(r, (D2D1_RECT_F){ mx_dip - 46.0f, c_bot + 2.0f, mx_dip + 46.0f, h_dip - 2.0f }, col_border, col_border, htbuf, col_bright, r->font_mono);
        }

        /* Toast notification if active */
        if (vp->has_toast) {
            D2D1_RECT_F trc = { c_right - 256.0f, c_top + 12.0f, c_right - 12.0f, c_top + 66.0f };
            draw_btn_box(r, trc, col_panel, col_bull, NULL, 0, r->font_small);
            draw_txt(r, r->font_small, L"Alerte déclenchée", (D2D1_RECT_F){ trc.left + 10.0f, trc.top + 6.0f, trc.right - 8.0f, trc.top + 22.0f }, col_bull);
            draw_txt(r, r->font_small, vp->toast_entry.rule_name, (D2D1_RECT_F){ trc.left + 10.0f, trc.top + 23.0f, trc.right - 8.0f, trc.top + 39.0f }, col_bright);
            draw_txt(r, r->font_mono, vp->toast_entry.summary, (D2D1_RECT_F){ trc.left + 10.0f, trc.top + 39.0f, trc.right - 8.0f, trc.bottom - 2.0f }, col_muted);
        }

        #undef P2Y
        #undef I2X
    } else {
        D2D1_RECT_F loading_rc = {
            c_left + (c_w - 240.0f) * 0.5f,
            main_top + (main_h - 44.0f) * 0.5f,
            c_left + (c_w + 240.0f) * 0.5f,
            main_top + (main_h + 44.0f) * 0.5f
        };
        draw_btn_box(r, loading_rc, col_panel, col_border, NULL, 0, r->font_small);
        wchar_t loading_text[96];
        swprintf(loading_text, 96, L"Chargement du graphique : %ls...", feed->active_symbol);
        draw_txt(r, r->font_small, loading_text, loading_rc, col_text);
    }

    /* =====================================================================
     * ZONE 4: RIGHT SIDEBAR (Watchlist / Alertes / Sources)
     * ===================================================================== */
    const float rx = w_dip - right_w;
    D2D1_RECT_F side_rc = { rx, top_h, w_dip, h_dip };
    set_brush_argb(r->brush_dyn, col_panel, 1.0f);
    ID2D1HwndRenderTarget_FillRectangle(r->target, &side_rc, (ID2D1Brush*)r->brush_dyn);
    set_brush_argb(r->brush_dyn, col_border, 1.0f);
    ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(rx, top_h), d2d_pt(rx, h_dip), (ID2D1Brush*)r->brush_dyn, 1.0f, NULL);

    const wchar_t* tab_labels[3] = { L"Watchlist", L"Alertes", L"Sources" };
    float tw = right_w / 3.0f;
    for (int t = 0; t < 3; t++) {
        L->tab_btns[t] = (D2D1_RECT_F){ rx + t * tw, top_h, rx + (t + 1) * tw, top_h + 36.0f };
        bool act = ((int)vp->active_tab == t);
        IDWriteTextFormat_SetTextAlignment(r->font_small, DWRITE_TEXT_ALIGNMENT_CENTER);
        draw_txt(
            r, r->font_small, tab_labels[t],
            (D2D1_RECT_F){ L->tab_btns[t].left, top_h + 10.0f, L->tab_btns[t].right, top_h + 34.0f },
            act ? col_accent : col_muted
        );
        IDWriteTextFormat_SetTextAlignment(r->font_small, DWRITE_TEXT_ALIGNMENT_LEADING);
        if (act) {
            set_brush_argb(r->brush_dyn, col_accent, 1.0f);
            ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(L->tab_btns[t].left, top_h + 35.0f), d2d_pt(L->tab_btns[t].right, top_h + 35.0f), (ID2D1Brush*)r->brush_dyn, 2.0f, NULL);
        }
    }
    set_brush_argb(r->brush_dyn, col_border, 1.0f);
    ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(rx, top_h + 36.0f), d2d_pt(w_dip, top_h + 36.0f), (ID2D1Brush*)r->brush_dyn, 1.0f, NULL);

    float sy_cur = top_h + 46.0f;
    if (vp->active_tab == TAB_WATCHLIST) {
        draw_txt(r, r->font_small, L"Tokens suivis", (D2D1_RECT_F){ rx + 12.0f, sy_cur + 5.0f, rx + 130.0f, sy_cur + 24.0f }, col_bright);
        L->wl_sec_add_btn = (D2D1_RECT_F){ w_dip - 178.0f, sy_cur, w_dip - 96.0f, sy_cur + 26.0f };
        L->wl_add_btn     = (D2D1_RECT_F){ w_dip - 90.0f,  sy_cur, w_dip - 12.0f, sy_cur + 26.0f };
        draw_btn_box(r, L->wl_sec_add_btn, col_surface, col_border, L"+ Section", col_text, r->font_small);
        draw_btn_box(r, L->wl_add_btn,     col_accent,  col_accent, L"+ Token",   0xFFFFFFFFU, r->font_small);
        sy_cur += 34.0f;

        const WatchlistItem* active_tok = NULL;
        for (size_t w = 0; w < watchlist_count; w++) {
            const WatchlistItem* it = &watchlist[w];
            if (it->removed) continue;
            bool is_act = (feed->active_entry_id != 0)
                ? (it->id == feed->active_entry_id)
                : (wcscmp(it->symbol, feed->active_symbol) == 0);
            if (is_act) {
                active_tok = it;
                break;
            }
        }

        const float list_max_y = h_dip - 126.0f;
        for (size_t si = 0; si < section_count && sy_cur + 26.0f < list_max_y; si++) {
            const WatchlistSection* sec = &sections[si];
            size_t sec_cnt = 0;
            for (size_t w = 0; w < watchlist_count; w++) {
                if (!watchlist[w].removed && watchlist[w].section_id == sec->id) sec_cnt++;
            }

            float sec_block_top = sy_cur;
            size_t svi_cur = MAX_WL_SECTIONS;
            if (L->wl_sec_visible_count < MAX_WL_SECTIONS) {
                svi_cur = L->wl_sec_visible_count++;
                L->wl_sec_ids[svi_cur]         = sec->id;
                L->wl_sec_headers[svi_cur]     = (D2D1_RECT_F){ rx + 8.0f, sy_cur, w_dip - 8.0f, sy_cur + 24.0f };
                L->wl_sec_rename_btns[svi_cur] = (D2D1_RECT_F){ w_dip - 54.0f, sy_cur + 2.0f, w_dip - 32.0f, sy_cur + 22.0f };
                if (section_count > 1) {
                    L->wl_sec_del_btns[svi_cur] = (D2D1_RECT_F){ w_dip - 30.0f, sy_cur + 2.0f, w_dip - 10.0f, sy_cur + 22.0f };
                }

                bool sec_drop_inside = vp->wl_drag_active && vp->wl_drag_kind == 1 &&
                                       vp->wl_drop_section_id == sec->id && vp->wl_drop_entry_id == 0;
                bool sec_dragging    = vp->wl_drag_active && vp->wl_drag_kind == 2 && vp->wl_drag_id == sec->id;
                uint32_t sec_fill    = sec_drop_inside
                    ? (dark ? 0xFF1F2B47U : 0xFFDBEAFEU)
                    : (dark ? 0xFF252F45U : 0xFFE9EFFCU);
                uint32_t sec_border  = (sec_drop_inside || sec_dragging)
                    ? col_accent
                    : (dark ? 0xFF3B4D70U : 0xFFB8C8EAU);

                draw_btn_box(r, L->wl_sec_headers[svi_cur], sec_fill, sec_border, NULL, 0, r->font_small);
                wchar_t sec_hdr[72];
                swprintf(sec_hdr, 72, L"⋮ %ls %ls (%zu)", sec->collapsed ? L"▸" : L"▾", sec->name, sec_cnt);
                D2D1_RECT_F sec_accent = { rx + 8.0f, sy_cur + 3.0f, rx + 11.0f, sy_cur + 21.0f };
                set_brush_argb(r->brush_dyn, sec_dragging ? col_muted : col_accent, 1.0f);
                ID2D1HwndRenderTarget_FillRectangle(r->target, &sec_accent, (ID2D1Brush*)r->brush_dyn);
                draw_txt(r, r->font_ui_bold, sec_hdr, (D2D1_RECT_F){ rx + 16.0f, sy_cur + 3.0f, w_dip - 58.0f, sy_cur + 22.0f }, sec_dragging ? col_muted : col_bright);
                draw_btn_box(r, L->wl_sec_rename_btns[svi_cur], 0, 0, L"✎", col_muted, r->font_small);
                if (section_count > 1) {
                    draw_btn_box(r, L->wl_sec_del_btns[svi_cur], 0, 0, L"×", col_bear, r->font_small);
                }

                if (vp->wl_drag_active && vp->wl_drag_kind == 2 &&
                    vp->wl_drop_section_id == sec->id && vp->wl_drag_id != sec->id && !vp->wl_drop_after) {
                    set_brush_argb(r->brush_dyn, col_accent, 1.0f);
                    ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(rx + 8.0f, sy_cur - 1.0f), d2d_pt(w_dip - 8.0f, sy_cur - 1.0f), (ID2D1Brush*)r->brush_dyn, 2.2f, NULL);
                }
            }
            sy_cur += 27.0f;

            if (!sec->collapsed) {
                for (size_t w = 0; w < watchlist_count && sy_cur + 44.0f < list_max_y; w++) {
                    const WatchlistItem* it = &watchlist[w];
                    if (it->removed) continue;
                    bool belongs = (it->section_id == sec->id);
                    if (!belongs && si == 0) {
                        bool found_sec = false;
                        for (size_t k = 0; k < section_count; k++) {
                            if (sections[k].id == it->section_id) { found_sec = true; break; }
                        }
                        if (!found_sec) belongs = true;
                    }
                    if (!belongs) continue;
                    if (L->wl_visible_count >= MAX_WATCHLIST) break;

                    bool is_act = (feed->active_entry_id != 0)
                        ? (it->id == feed->active_entry_id)
                        : (wcscmp(it->symbol, feed->active_symbol) == 0);
                    bool is_drag_row = vp->wl_drag_active && vp->wl_drag_kind == 1 && vp->wl_drag_id == it->id;

                    size_t vi = L->wl_visible_count++;
                    L->wl_indices[vi]    = w;
                    L->wl_entry_ids[vi]  = it->id;
                    L->wl_rows[vi]       = (D2D1_RECT_F){ rx + 8.0f, sy_cur, w_dip - 96.0f, sy_cur + 42.0f };
                    L->wl_rename_btns[vi]= (D2D1_RECT_F){ w_dip - 94.0f, sy_cur + 10.0f, w_dip - 74.0f, sy_cur + 31.0f };
                    L->wl_move_btns[vi]  = (D2D1_RECT_F){ w_dip - 72.0f, sy_cur + 10.0f, w_dip - 52.0f, sy_cur + 31.0f };
                    L->wl_clone_btns[vi] = (D2D1_RECT_F){ w_dip - 50.0f, sy_cur + 10.0f, w_dip - 30.0f, sy_cur + 31.0f };
                    L->wl_del_btns[vi]   = (D2D1_RECT_F){ w_dip - 28.0f, sy_cur + 10.0f, w_dip - 8.0f,  sy_cur + 31.0f };

                    if (is_act || is_drag_row) {
                        D2D1_ROUNDED_RECT act_rc = { (D2D1_RECT_F){ rx + 8.0f, sy_cur, w_dip - 8.0f, sy_cur + 42.0f }, 4.0f, 4.0f };
                        set_brush_argb(r->brush_dyn, col_accent, is_drag_row ? 0.08f : 0.14f);
                        ID2D1HwndRenderTarget_FillRoundedRectangle(r->target, &act_rc, (ID2D1Brush*)r->brush_dyn);
                        if (is_drag_row) {
                            set_brush_argb(r->brush_dyn, col_accent, 0.7f);
                            ID2D1HwndRenderTarget_DrawRoundedRectangle(r->target, &act_rc, (ID2D1Brush*)r->brush_dyn, 1.0f, r->stroke_dashed);
                        }
                    }

                    const wchar_t* line1 = (it->display_name[0]) ? it->display_name : it->symbol;
                    const wchar_t* line2 = (it->display_name[0]) ? it->symbol : it->name;
                    draw_txt(r, r->font_small, line1, (D2D1_RECT_F){ rx + 14.0f, sy_cur + 5.0f,  rx + 132.0f, sy_cur + 22.0f }, is_drag_row ? col_muted : col_bright);
                    draw_txt(r, r->font_small, line2, (D2D1_RECT_F){ rx + 14.0f, sy_cur + 22.0f, rx + 132.0f, sy_cur + 39.0f }, col_muted);

                    wchar_t px_s[32], pct_s[32];
                    format_price_w(it->price, px_s, 32);
                    if (it->has_24h_data) {
                        swprintf(pct_s, 32, L"%+.2f%%", it->change_pct);
                    } else {
                        ov_wcscpy(pct_s, 32, L"—");
                    }
                    IDWriteTextFormat_SetTextAlignment(r->font_mono, DWRITE_TEXT_ALIGNMENT_TRAILING);
                    draw_txt(r, r->font_mono, px_s,  (D2D1_RECT_F){ rx + 128.0f, sy_cur + 6.0f,  w_dip - 98.0f, sy_cur + 22.0f }, col_bright);
                    draw_txt(r, r->font_mono, pct_s, (D2D1_RECT_F){ rx + 128.0f, sy_cur + 23.0f, w_dip - 98.0f, sy_cur + 39.0f }, !it->has_24h_data ? col_muted : (it->change_pct >= 0.0 ? col_bull : col_bear));
                    IDWriteTextFormat_SetTextAlignment(r->font_mono, DWRITE_TEXT_ALIGNMENT_LEADING);

                    draw_btn_box(r, L->wl_rename_btns[vi], 0, col_border, L"✎", col_muted,  r->font_small);
                    draw_btn_box(r, L->wl_move_btns[vi],   0, col_border, L"⇄", col_muted,  r->font_small);
                    draw_btn_box(r, L->wl_clone_btns[vi],  0, col_border, L"+", col_accent, r->font_small);
                    draw_btn_box(r, L->wl_del_btns[vi],    0, col_border, L"×", col_bear,   r->font_small);

                    if (vp->wl_drag_active && vp->wl_drag_kind == 1 &&
                        vp->wl_drop_entry_id == it->id && vp->wl_drag_id != it->id) {
                        float line_y = vp->wl_drop_after ? (sy_cur + 43.0f) : (sy_cur - 1.0f);
                        set_brush_argb(r->brush_dyn, col_accent, 1.0f);
                        ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(rx + 8.0f, line_y), d2d_pt(w_dip - 8.0f, line_y), (ID2D1Brush*)r->brush_dyn, 2.2f, NULL);
                    }
                    sy_cur += 44.0f;
                }
            }

            if (svi_cur < MAX_WL_SECTIONS) {
                L->wl_sec_blocks[svi_cur] = (D2D1_RECT_F){ rx + 8.0f, sec_block_top, w_dip - 8.0f, sy_cur };
                if (vp->wl_drag_active && vp->wl_drag_kind == 2 &&
                    vp->wl_drop_section_id == sec->id && vp->wl_drag_id != sec->id && vp->wl_drop_after) {
                    set_brush_argb(r->brush_dyn, col_accent, 1.0f);
                    ID2D1HwndRenderTarget_DrawLine(r->target, d2d_pt(rx + 8.0f, sy_cur - 1.0f), d2d_pt(w_dip - 8.0f, sy_cur - 1.0f), (ID2D1Brush*)r->brush_dyn, 2.2f, NULL);
                }
            }
        }

        /* Floating drag ghost near cursor */
        if (vp->wl_drag_active && vp->wl_drag_id != 0) {
            const wchar_t* ghost_lbl = NULL;
            if (vp->wl_drag_kind == 1) {
                for (size_t w = 0; w < watchlist_count; w++) {
                    if (!watchlist[w].removed && watchlist[w].id == vp->wl_drag_id) {
                        ghost_lbl = watchlist[w].display_name[0] ? watchlist[w].display_name : watchlist[w].symbol;
                        break;
                    }
                }
            } else if (vp->wl_drag_kind == 2) {
                for (size_t s = 0; s < section_count; s++) {
                    if (sections[s].id == vp->wl_drag_id) {
                        ghost_lbl = sections[s].name;
                        break;
                    }
                }
            }
            if (ghost_lbl) {
                float gx = vp->mouse_x / dpi;
                float gy = vp->mouse_y / dpi;
                D2D1_RECT_F grc = { gx - 70.0f, gy - 14.0f, gx + 70.0f, gy + 14.0f };
                draw_btn_box(r, grc, col_panel, col_accent, ghost_lbl, col_bright, r->font_small);
            }
        }

        /* Bottom active token details card */
        if (active_tok) {
            D2D1_RECT_F det_rc = { rx + 10.0f, h_dip - 122.0f, w_dip - 10.0f, h_dip - 10.0f };
            draw_btn_box(r, det_rc, col_surface, col_border, NULL, 0, r->font_small);
            wchar_t p_hi[24], p_lo[24], p_op[24], det[256];
            format_price_w(active_tok->high_24h, p_hi, 24);
            format_price_w(active_tok->low_24h,  p_lo, 24);
            format_price_w(active_tok->open_24h, p_op, 24);
            const wchar_t* hdr_name = active_tok->display_name[0] ? active_tok->display_name : active_tok->symbol;
            swprintf(
                det, 256,
                L"%ls (%ls) · 24h\nOuv: %ls   Vol: %.0f\nHaut: %ls  Bas: %ls\nB %.2f C %.2f K %.2f M %.2f G %.2f",
                hdr_name, active_tok->symbol, p_op, active_tok->volume_24h, p_hi, p_lo,
                active_tok->source_prices[SRC_BINANCE],
                active_tok->source_prices[SRC_COINBASE],
                active_tok->source_prices[SRC_KRAKEN],
                active_tok->source_prices[SRC_MEXC],
                active_tok->source_prices[SRC_COINGECKO]
            );
            draw_txt(r, r->font_mono, det, (D2D1_RECT_F){ det_rc.left + 10.0f, det_rc.top + 8.0f, det_rc.right - 8.0f, det_rc.bottom - 6.0f }, col_text);
        }
    } else if (vp->active_tab == TAB_ALERTS) {
        L->alert_add_btn = (D2D1_RECT_F){ rx + 12.0f, sy_cur, w_dip - 12.0f, sy_cur + 28.0f };
        draw_btn_box(r, L->alert_add_btn, col_accent, col_accent, L"+ Nouvelle alerte multi-conditions", 0xFFFFFFFFU, r->font_small);
        sy_cur += 38.0f;

        draw_txt(r, r->font_small, L"Règles actives", (D2D1_RECT_F){ rx + 12.0f, sy_cur, w_dip - 12.0f, sy_cur + 20.0f }, col_muted);
        sy_cur += 22.0f;

        for (size_t i = 0; i < alerts->rule_count && sy_cur + 52.0f < h_dip - 180.0f; i++) {
            const AlertRule* rl = &alerts->rules[i];
            size_t ai = L->alert_visible_count++;
            L->alert_ids[ai] = rl->id;

            D2D1_RECT_F card = { rx + 10.0f, sy_cur, w_dip - 10.0f, sy_cur + 46.0f };
            draw_btn_box(r, card, col_surface, col_border, NULL, 0, r->font_small);

            L->alert_toggle_btns[ai] = (D2D1_RECT_F){ w_dip - 86.0f, sy_cur + 10.0f, w_dip - 42.0f, sy_cur + 32.0f };
            L->alert_del_btns[ai]    = (D2D1_RECT_F){ w_dip - 38.0f, sy_cur + 10.0f, w_dip - 16.0f, sy_cur + 32.0f };

            draw_txt(r, r->font_small, rl->name, (D2D1_RECT_F){ card.left + 8.0f, sy_cur + 5.0f, w_dip - 90.0f, sy_cur + 23.0f }, col_bright);
            wchar_t meta[64];
            swprintf(meta, 64, L"%ls · %ls · %zu cond · %u×", rl->symbol, rl->logic == LOGIC_AND ? L"AND" : L"OR", rl->condition_count, rl->fire_count);
            draw_txt(r, r->font_mono, meta, (D2D1_RECT_F){ card.left + 8.0f, sy_cur + 24.0f, w_dip - 90.0f, sy_cur + 42.0f }, col_muted);

            draw_btn_box(r, L->alert_toggle_btns[ai], rl->enabled ? col_bull : col_border, 0, rl->enabled ? L"ON" : L"OFF", 0xFFFFFFFFU, r->font_small);
            draw_btn_box(r, L->alert_del_btns[ai], 0, col_border, L"×", col_bear, r->font_small);
            sy_cur += 52.0f;
        }

        sy_cur += 8.0f;
        draw_txt(r, r->font_small, L"Journal des déclenchements", (D2D1_RECT_F){ rx + 12.0f, sy_cur + 3.0f, rx + 210.0f, sy_cur + 22.0f }, col_muted);
        L->alert_clear_btn = (D2D1_RECT_F){ w_dip - 76.0f, sy_cur, w_dip - 12.0f, sy_cur + 22.0f };
        draw_btn_box(r, L->alert_clear_btn, col_surface, col_border, L"Effacer", col_muted, r->font_small);
        sy_cur += 28.0f;

        for (size_t i = 0; i < alerts->log_count && sy_cur + 38.0f < h_dip - 8.0f; i++) {
            const AlertLogEntry* lg = &alerts->logs[i];
            D2D1_RECT_F lrc = { rx + 10.0f, sy_cur, w_dip - 10.0f, sy_cur + 36.0f };
            draw_btn_box(r, lrc, col_surface, col_border, NULL, 0, r->font_small);
            draw_txt(r, r->font_small, lg->rule_name, (D2D1_RECT_F){ lrc.left + 8.0f, sy_cur + 3.0f, lrc.right - 6.0f, sy_cur + 19.0f }, col_bull);
            draw_txt(r, r->font_mono,  lg->summary,   (D2D1_RECT_F){ lrc.left + 8.0f, sy_cur + 19.0f, lrc.right - 6.0f, sy_cur + 34.0f }, col_muted);
            sy_cur += 40.0f;
        }
    } else {
        /* TAB_SOURCES */
        draw_txt(r, r->font_small, L"Gestionnaire Multi-Sources Async", (D2D1_RECT_F){ rx + 12.0f, sy_cur, w_dip - 12.0f, sy_cur + 22.0f }, col_bright);
        sy_cur += 28.0f;

        for (int s = 0; s < SRC_COUNT; s++) {
            const SourceStatusInfo* src = &sources[s];
            D2D1_RECT_F card = { rx + 10.0f, sy_cur, w_dip - 10.0f, sy_cur + 94.0f };
            draw_btn_box(r, card, col_surface, col_border, NULL, 0, r->font_small);

            const wchar_t* st_txt =
                src->state == STATE_LIVE ? L"Flux réel" :
                src->state == STATE_STATIC ? L"Historique" :
                src->state == STATE_SYNCING ? L"Sync" :
                src->state == STATE_OFFLINE ? L"Hors ligne" : L"Relais sync";
            uint32_t st_col =
                src->state == STATE_LIVE ? col_bull :
                src->state == STATE_STATIC ? col_accent :
                src->state == STATE_OFFLINE ? col_bear : col_ema1;
            draw_txt(r, r->font_ui_bold, src->label, (D2D1_RECT_F){ card.left + 10.0f, sy_cur + 8.0f, card.left + 140.0f, sy_cur + 28.0f }, col_bright);
            draw_txt(r, r->font_small, st_txt, (D2D1_RECT_F){ w_dip - 105.0f, sy_cur + 10.0f, w_dip - 18.0f, sy_cur + 28.0f }, st_col);

            wchar_t meta[96];
            swprintf(meta, 96, L"%ls · %u ms · %llu ticks", src->endpoint, src->latency_ms, (unsigned long long)src->ticks_received);
            draw_txt(r, r->font_mono, meta, (D2D1_RECT_F){ card.left + 10.0f, sy_cur + 30.0f, card.right - 8.0f, sy_cur + 48.0f }, col_muted);

            bool is_prim = ((int)feed->primary_source == s);
            bool is_ov   = feed->overlay_sources[s];

            L->src_primary_btns[s] = (D2D1_RECT_F){ card.left + 10.0f, sy_cur + 56.0f, card.left + 145.0f, sy_cur + 82.0f };
            L->src_overlay_btns[s] = (D2D1_RECT_F){ card.left + 154.0f, sy_cur + 56.0f, card.right - 10.0f, sy_cur + 82.0f };

            draw_btn_box(
                r, L->src_primary_btns[s],
                is_prim ? col_accent : col_panel,
                is_prim ? col_accent : col_border,
                is_prim ? L"● Principale" : L"○ Principale",
                is_prim ? 0xFFFFFFFFU : col_text,
                r->font_small
            );
            draw_btn_box(
                r, L->src_overlay_btns[s],
                is_ov ? col_bull : col_panel,
                is_ov ? col_bull : col_border,
                is_ov ? L"☑ Superposer" : L"☐ Superposer",
                is_ov ? 0xFFFFFFFFU : col_text,
                r->font_small
            );

            sy_cur += 104.0f;
        }
    }

    HRESULT hr = ID2D1HwndRenderTarget_EndDraw(r->target, NULL, NULL);
    if (hr == (HRESULT)D2DERR_RECREATE_TARGET) {
        discard_device_resources(r);
    }
}
