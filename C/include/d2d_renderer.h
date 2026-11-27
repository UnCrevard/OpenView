#ifndef OV_D2D_RENDERER_H
#define OV_D2D_RENDERER_H

#include "common.h"
#include "feed_manager.h"
#include "indicators.h"
#include "drawings.h"
#include "alerts.h"
#include <d2d1.h>
#include <dwrite.h>

typedef enum {
    TAB_WATCHLIST = 0,
    TAB_ALERTS    = 1,
    TAB_SOURCES   = 2
} RightSidebarTab;

typedef struct {
    float           pan_bars;
    float           bar_width;
    float           y_scale_factor;
    float           y_pan_factor;
    float           mouse_x;
    float           mouse_y;
    bool            mouse_inside;
    DrawingToolKind active_tool;
    uint32_t        drawing_color;
    uint32_t        selected_drawing_id;
    AnchorPoint     draft_pts[3];
    size_t          draft_count;
    bool            dark_theme;
    RightSidebarTab active_tab;
    bool            has_toast;
    AlertLogEntry   toast_entry;
    int64_t         toast_until_ms;
    uint64_t        last_primary_tick_ms;
    int             wl_drag_kind;
    uint32_t        wl_drag_id;
    bool            wl_drag_active;
    uint32_t        wl_drop_entry_id;
    bool            wl_drop_after;
    uint32_t        wl_drop_section_id;
} ChartViewportState;

typedef struct {
    D2D1_RECT_F symbol_btn;
    D2D1_RECT_F tf_btns[9];
    TimeframeSec tf_values[9];
    D2D1_RECT_F ind_btn;
    D2D1_RECT_F sources_btn;
    D2D1_RECT_F theme_btn;

    D2D1_RECT_F tool_btns[7];
    D2D1_RECT_F swatch_btns[7];
    uint32_t    swatch_colors[7];
    D2D1_RECT_F undo_btn;
    D2D1_RECT_F del_btn;
    D2D1_RECT_F clear_btn;

    D2D1_RECT_F chart_rect;
    D2D1_RECT_F main_pane_rect;
    D2D1_RECT_F price_axis_rect;

    D2D1_RECT_F tab_btns[3];
    D2D1_RECT_F wl_add_btn;
    D2D1_RECT_F wl_sec_add_btn;
    D2D1_RECT_F wl_sec_headers[MAX_WL_SECTIONS];
    D2D1_RECT_F wl_sec_blocks[MAX_WL_SECTIONS];
    D2D1_RECT_F wl_sec_rename_btns[MAX_WL_SECTIONS];
    D2D1_RECT_F wl_sec_del_btns[MAX_WL_SECTIONS];
    uint32_t    wl_sec_ids[MAX_WL_SECTIONS];
    size_t      wl_sec_visible_count;

    D2D1_RECT_F wl_rows[MAX_WATCHLIST];
    D2D1_RECT_F wl_rename_btns[MAX_WATCHLIST];
    D2D1_RECT_F wl_move_btns[MAX_WATCHLIST];
    D2D1_RECT_F wl_clone_btns[MAX_WATCHLIST];
    D2D1_RECT_F wl_del_btns[MAX_WATCHLIST];
    uint32_t    wl_entry_ids[MAX_WATCHLIST];
    size_t      wl_indices[MAX_WATCHLIST];
    size_t      wl_visible_count;

    D2D1_RECT_F alert_add_btn;
    D2D1_RECT_F alert_clear_btn;
    D2D1_RECT_F alert_toggle_btns[MAX_ALERTS];
    D2D1_RECT_F alert_del_btns[MAX_ALERTS];
    uint32_t    alert_ids[MAX_ALERTS];
    size_t      alert_visible_count;

    D2D1_RECT_F src_primary_btns[SRC_COUNT];
    D2D1_RECT_F src_overlay_btns[SRC_COUNT];

    /* Cached chart mapping for hit-testing & coordinate conversions */
    double      min_price;
    double      max_price;
    int         start_idx;
    int         end_idx;
    float       step_px;
    size_t      bar_count;
    int64_t     first_time;
    int64_t     last_time;
    TimeframeSec tf;
} UILayoutCache;

typedef struct {
    ID2D1Factory*          d2d_factory;
    IDWriteFactory*        dwrite_factory;
    ID2D1HwndRenderTarget* target;
    IDWriteTextFormat*     font_ui;
    IDWriteTextFormat*     font_ui_bold;
    IDWriteTextFormat*     font_mono;
    IDWriteTextFormat*     font_small;
    ID2D1StrokeStyle*      stroke_dashed;
    ID2D1StrokeStyle*      stroke_dotted;
    ID2D1SolidColorBrush*  brush_dyn;
    UILayoutCache          layout;
} D2DRenderer;

bool d2d_renderer_init(D2DRenderer* r, HWND hwnd);
void d2d_renderer_resize(D2DRenderer* r, UINT width, UINT height);
void d2d_renderer_destroy(D2DRenderer* r);

float d2d_time_to_x(const UILayoutCache* l, const CandleBar* bars, int64_t time_utc);
int64_t d2d_x_to_time(const UILayoutCache* l, const CandleBar* bars, float x_dip);
float d2d_price_to_y(const UILayoutCache* l, double price);
double d2d_y_to_price(const UILayoutCache* l, float y_dip);

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
);

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
);

#endif /* OV_D2D_RENDERER_H */
