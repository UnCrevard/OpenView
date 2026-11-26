#ifndef OV_DRAWINGS_H
#define OV_DRAWINGS_H

#include "common.h"

typedef enum {
    TOOL_CURSOR = 0,
    TOOL_TRENDLINE,
    TOOL_HLINE,
    TOOL_FIBONACCI,
    TOOL_CHANNEL,
    TOOL_RECTANGLE,
    TOOL_TEXT
} DrawingToolKind;

typedef enum {
    LINE_SOLID  = 0,
    LINE_DASHED = 1,
    LINE_DOTTED = 2
} DrawingLineStyle;

typedef enum {
    ALIGN_H_LEFT   = 0,
    ALIGN_H_CENTER = 1,
    ALIGN_H_RIGHT  = 2
} DrawingLabelAlignH;

typedef enum {
    ALIGN_V_TOP    = 0,
    ALIGN_V_MIDDLE = 1,
    ALIGN_V_BOTTOM = 2
} DrawingLabelAlignV;

typedef struct {
    int64_t time_utc;
    double  price;
} AnchorPoint;

typedef struct {
    uint32_t           id;
    uint32_t           entry_id;
    wchar_t            symbol[24];
    DrawingToolKind    kind;
    AnchorPoint        pts[3];
    size_t             pt_count;
    uint32_t           argb_color;
    float              stroke_width;
    DrawingLineStyle   line_style;
    wchar_t            label[64];
    DrawingLabelAlignH label_align_h;
    DrawingLabelAlignV label_align_v;
    bool               locked;
} ChartDrawing;

typedef struct {
    ChartDrawing items[MAX_DRAWINGS];
    size_t       count;
    uint32_t     next_id;
} DrawingStore;

void     drawings_init(DrawingStore* store);
uint32_t drawings_add(
    DrawingStore* store,
    uint32_t entry_id,
    const wchar_t* symbol,
    DrawingToolKind kind,
    const AnchorPoint* pts,
    size_t pt_count,
    uint32_t argb_color,
    float stroke_width,
    DrawingLineStyle line_style,
    const wchar_t* label
);
ChartDrawing* drawings_find_by_id(DrawingStore* store, uint32_t id);
bool          drawings_remove_by_id(DrawingStore* store, uint32_t id);
bool          drawings_matches_scope(
    const ChartDrawing* d,
    uint32_t entry_id,
    const wchar_t* symbol,
    bool is_primary_entry
);
bool          drawings_undo_scope(
    DrawingStore* store,
    uint32_t entry_id,
    const wchar_t* symbol,
    bool is_primary_entry
);
void          drawings_clear_scope(
    DrawingStore* store,
    uint32_t entry_id,
    const wchar_t* symbol,
    bool is_primary_entry
);
void          drawings_remove_by_entry(DrawingStore* store, uint32_t entry_id);
bool          drawings_undo_symbol(DrawingStore* store, const wchar_t* symbol);
void          drawings_clear_symbol(DrawingStore* store, const wchar_t* symbol);
size_t        drawings_count_symbol(const DrawingStore* store, const wchar_t* symbol);

bool drawings_save_file_w(const DrawingStore* store, const wchar_t* file_path);
bool drawings_load_file_w(DrawingStore* store, const wchar_t* file_path);

#endif /* OV_DRAWINGS_H */
