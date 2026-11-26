#include "drawings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void drawings_init(DrawingStore* store) {
    ZeroMemory(store, sizeof(*store));
    store->next_id = 1;
}

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
) {
    if (store->count >= MAX_DRAWINGS || pt_count == 0 || pt_count > 3) {
        return 0;
    }
    ChartDrawing* d = &store->items[store->count++];
    ZeroMemory(d, sizeof(*d));
    d->id = store->next_id++;
    d->entry_id = entry_id;
    ov_wcscpy(d->symbol, 24, symbol);
    d->kind = kind;
    d->pt_count = pt_count;
    for (size_t i = 0; i < pt_count; i++) d->pts[i] = pts[i];
    d->argb_color    = argb_color;
    d->stroke_width  = (stroke_width > 0.2f) ? stroke_width : 1.8f;
    d->line_style    = line_style;
    d->label_align_h = (kind == TOOL_TRENDLINE || kind == TOOL_CHANNEL) ? ALIGN_H_CENTER : ALIGN_H_LEFT;
    d->label_align_v = ALIGN_V_TOP;
    d->locked        = false;
    if (label) {
        ov_wcscpy(d->label, 64, label);
    }
    return d->id;
}

ChartDrawing* drawings_find_by_id(DrawingStore* store, uint32_t id) {
    for (size_t i = 0; i < store->count; i++) {
        if (store->items[i].id == id) return &store->items[i];
    }
    return NULL;
}

bool drawings_remove_by_id(DrawingStore* store, uint32_t id) {
    for (size_t i = 0; i < store->count; i++) {
        if (store->items[i].id == id) {
            for (size_t j = i; j + 1 < store->count; j++) {
                store->items[j] = store->items[j + 1];
            }
            store->count--;
            return true;
        }
    }
    return false;
}

bool drawings_matches_scope(
    const ChartDrawing* d,
    uint32_t entry_id,
    const wchar_t* symbol,
    bool is_primary_entry
) {
    if (!d || !symbol) return false;
    if (d->entry_id != 0) {
        if (entry_id != 0) return d->entry_id == entry_id;
        return wcscmp(d->symbol, symbol) == 0;
    }
    return wcscmp(d->symbol, symbol) == 0 && is_primary_entry;
}

bool drawings_undo_scope(
    DrawingStore* store,
    uint32_t entry_id,
    const wchar_t* symbol,
    bool is_primary_entry
) {
    for (size_t i = store->count; i > 0; i--) {
        if (drawings_matches_scope(&store->items[i - 1], entry_id, symbol, is_primary_entry)) {
            return drawings_remove_by_id(store, store->items[i - 1].id);
        }
    }
    return false;
}

void drawings_clear_scope(
    DrawingStore* store,
    uint32_t entry_id,
    const wchar_t* symbol,
    bool is_primary_entry
) {
    size_t write_idx = 0;
    for (size_t i = 0; i < store->count; i++) {
        if (!drawings_matches_scope(&store->items[i], entry_id, symbol, is_primary_entry)) {
            store->items[write_idx++] = store->items[i];
        }
    }
    store->count = write_idx;
}

void drawings_remove_by_entry(DrawingStore* store, uint32_t entry_id) {
    if (entry_id == 0) return;
    size_t write_idx = 0;
    for (size_t i = 0; i < store->count; i++) {
        if (store->items[i].entry_id != entry_id) {
            store->items[write_idx++] = store->items[i];
        }
    }
    store->count = write_idx;
}

bool drawings_undo_symbol(DrawingStore* store, const wchar_t* symbol) {
    for (size_t i = store->count; i > 0; i--) {
        if (wcscmp(store->items[i - 1].symbol, symbol) == 0) {
            return drawings_remove_by_id(store, store->items[i - 1].id);
        }
    }
    return false;
}

void drawings_clear_symbol(DrawingStore* store, const wchar_t* symbol) {
    size_t write_idx = 0;
    for (size_t i = 0; i < store->count; i++) {
        if (wcscmp(store->items[i].symbol, symbol) != 0) {
            store->items[write_idx++] = store->items[i];
        }
    }
    store->count = write_idx;
}

size_t drawings_count_symbol(const DrawingStore* store, const wchar_t* symbol) {
    size_t cnt = 0;
    for (size_t i = 0; i < store->count; i++) {
        if (wcscmp(store->items[i].symbol, symbol) == 0) cnt++;
    }
    return cnt;
}

static void write_utf8_str(HANDLE h, const char* s) {
    DWORD written = 0;
    WriteFile(h, s, (DWORD)strlen(s), &written, NULL);
}

static void write_json_escaped(HANDLE h, const wchar_t* ws) {
    char utf8[256];
    WideCharToMultiByte(CP_UTF8, 0, ws ? ws : L"", -1, utf8, sizeof(utf8), NULL, NULL);
    write_utf8_str(h, "\"");
    for (const char* p = utf8; *p; p++) {
        if (*p == '"' || *p == '\\') {
            char esc[3] = { '\\', *p, '\0' };
            write_utf8_str(h, esc);
        } else if ((unsigned char)*p >= 0x20) {
            DWORD w = 0;
            WriteFile(h, p, 1, &w, NULL);
        }
    }
    write_utf8_str(h, "\"");
}

bool drawings_save_file_w(const DrawingStore* store, const wchar_t* file_path) {
    HANDLE h = CreateFileW(
        file_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (h == INVALID_HANDLE_VALUE) return false;

    write_utf8_str(h, "[\n");
    for (size_t i = 0; i < store->count; i++) {
        const ChartDrawing* d = &store->items[i];
        char buf[256];
        write_utf8_str(h, "  {\"symbol\":");
        write_json_escaped(h, d->symbol);
        snprintf(
            buf, sizeof(buf),
            ",\"entry_id\":%u,\"kind\":%d,\"color\":%u,\"width\":%.2f,\"style\":%d,\"align_h\":%d,\"align_v\":%d,\"locked\":%s,\"label\":",
            (unsigned)d->entry_id,
            (int)d->kind,
            (unsigned)d->argb_color,
            d->stroke_width,
            (int)d->line_style,
            (int)d->label_align_h,
            (int)d->label_align_v,
            d->locked ? "true" : "false"
        );
        write_utf8_str(h, buf);
        write_json_escaped(h, d->label);
        write_utf8_str(h, ",\"pts\":[");
        for (size_t p = 0; p < d->pt_count; p++) {
            snprintf(
                buf, sizeof(buf),
                "{\"t\":%lld,\"p\":%.8f}%s",
                (long long)d->pts[p].time_utc,
                d->pts[p].price,
                (p + 1 < d->pt_count) ? "," : ""
            );
            write_utf8_str(h, buf);
        }
        write_utf8_str(h, (i + 1 < store->count) ? "]}\n," : "]}\n");
    }
    write_utf8_str(h, "]\n");
    CloseHandle(h);
    return true;
}

static bool extract_json_str(const char* obj, const char* key, wchar_t* out, size_t cap) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char* p = strstr(obj, pat);
    if (!p) return false;
    p += strlen(pat);
    char utf8[256];
    size_t idx = 0;
    while (*p && *p != '"' && idx + 1 < sizeof(utf8)) {
        if (*p == '\\' && p[1]) p++;
        utf8[idx++] = *p++;
    }
    utf8[idx] = '\0';
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, (int)cap);
    return true;
}

bool drawings_load_file_w(DrawingStore* store, const wchar_t* file_path) {
    HANDLE h = CreateFileW(
        file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD sz = GetFileSize(h, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0 || sz > 2 * 1024 * 1024) {
        CloseHandle(h);
        return false;
    }

    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) {
        CloseHandle(h);
        return false;
    }
    DWORD got = 0;
    BOOL ok = ReadFile(h, buf, sz, &got, NULL);
    CloseHandle(h);
    if (!ok || got == 0) {
        free(buf);
        return false;
    }
    buf[got] = '\0';

    drawings_init(store);
    const char* cur = buf;
    while ((cur = strstr(cur, "{\"symbol\":")) != NULL && store->count < MAX_DRAWINGS) {
        const char* next_obj = strstr(cur + 10, "{\"symbol\":");
        size_t span = next_obj ? (size_t)(next_obj - cur) : strlen(cur);
        char* obj = (char*)malloc(span + 1);
        if (!obj) break;
        memcpy(obj, cur, span);
        obj[span] = '\0';

        wchar_t sym[24] = L"";
        wchar_t lbl[64] = L"";
        extract_json_str(obj, "symbol", sym, 24);
        extract_json_str(obj, "label", lbl, 64);

        unsigned entry_id = 0;
        int kind = 1, style = 0;
        int align_h = -1, align_v = 0;
        unsigned color = 0xFF2962FFU;
        double width = 1.8;
        const char* peid = strstr(obj, "\"entry_id\":");
        if (peid) entry_id = (unsigned)strtoul(peid + 11, NULL, 10);
        const char* pk = strstr(obj, "\"kind\":");
        if (pk) kind = atoi(pk + 7);
        const char* pc = strstr(obj, "\"color\":");
        if (pc) color = (unsigned)strtoul(pc + 8, NULL, 10);
        const char* pw = strstr(obj, "\"width\":");
        if (pw) width = strtod(pw + 8, NULL);
        const char* ps = strstr(obj, "\"style\":");
        if (ps) style = atoi(ps + 8);
        const char* pah = strstr(obj, "\"align_h\":");
        if (pah) align_h = atoi(pah + 10);
        const char* pav = strstr(obj, "\"align_v\":");
        if (pav) align_v = atoi(pav + 10);
        bool locked = (strstr(obj, "\"locked\":true") != NULL);

        AnchorPoint pts[3];
        size_t pt_count = 0;
        const char* ppts = strstr(obj, "\"pts\":[");
        if (ppts) {
            const char* pt_it = ppts;
            while (pt_count < 3 && (pt_it = strstr(pt_it, "{\"t\":")) != NULL) {
                long long t = strtoll(pt_it + 5, NULL, 10);
                const char* pp = strstr(pt_it, "\"p\":");
                double pr = pp ? strtod(pp + 4, NULL) : 0.0;
                pts[pt_count].time_utc = (int64_t)t;
                pts[pt_count].price    = pr;
                pt_count++;
                pt_it += 5;
            }
        }

        if (sym[0] && pt_count > 0) {
            uint32_t id = drawings_add(
                store, (uint32_t)entry_id, sym, (DrawingToolKind)kind, pts, pt_count,
                (uint32_t)color, (float)width, (DrawingLineStyle)style, lbl
            );
            ChartDrawing* added = drawings_find_by_id(store, id);
            if (added) {
                added->locked = locked;
                if (align_h >= 0 && align_h <= 2) added->label_align_h = (DrawingLabelAlignH)align_h;
                if (align_v >= 0 && align_v <= 2) added->label_align_v = (DrawingLabelAlignV)align_v;
            }
        }

        free(obj);
        cur += 10;
    }

    free(buf);
    return true;
}
