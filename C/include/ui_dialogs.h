#ifndef OV_UI_DIALOGS_H
#define OV_UI_DIALOGS_H

#include "common.h"
#include "indicators.h"
#include "drawings.h"
#include "alerts.h"

bool ui_show_indicators_dialog(HWND parent, IndicatorSettings* cfg, bool dark_theme);

/* Returns: 1 = saved, 2 = deleted, 0 = cancelled */
int  ui_show_drawing_dialog(HWND parent, ChartDrawing* drawing, bool dark_theme);

bool ui_show_text_prompt(
    HWND parent,
    const wchar_t* title,
    const wchar_t* prompt_label,
    wchar_t* out_buf,
    size_t cap,
    bool allow_empty,
    bool dark_theme
);

bool ui_show_add_alert_dialog(
    HWND parent,
    const wchar_t* default_symbol,
    AlertRule* out_rule,
    bool dark_theme
);

bool ui_show_move_section_dialog(
    HWND parent,
    const wchar_t* item_label,
    const WatchlistSection* sections,
    size_t section_count,
    uint32_t current_section_id,
    uint32_t* out_section_id,
    bool dark_theme
);

#endif /* OV_UI_DIALOGS_H */
