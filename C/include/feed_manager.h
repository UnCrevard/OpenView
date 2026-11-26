#ifndef OV_FEED_MANAGER_H
#define OV_FEED_MANAGER_H

#include "common.h"

typedef struct {
    FeedTick           items[RING_CAPACITY];
    volatile LONG      head;
    volatile LONG      tail;
    SRWLOCK            lock;
    CONDITION_VARIABLE not_empty;
} TickRingBuffer;

typedef struct {
    HWND               ui_hwnd;
    volatile LONG      running;
    volatile LONG      resync_seq;
    HANDLE             worker_threads[SRC_COUNT];
    HANDLE             sync_thread;
    HANDLE             rest_thread;
    TickRingBuffer     ring;
    SRWLOCK            data_lock;
    CandleBar          candles[SRC_COUNT][MAX_CANDLES];
    size_t             candle_counts[SRC_COUNT];
    WatchlistSection   sections[MAX_WL_SECTIONS];
    size_t             section_count;
    uint32_t           next_section_id;
    WatchlistItem      watchlist[MAX_WATCHLIST];
    size_t             watchlist_count;
    uint32_t           next_wl_id;
    uint32_t           active_entry_id;
    SourceStatusInfo   sources[SRC_COUNT];
    wchar_t            active_symbol[24];
    FeedSourceId       primary_source;
    TimeframeSec       timeframe;
    bool               overlay_sources[SRC_COUNT];
} AsyncFeedManager;

bool feed_manager_init(AsyncFeedManager* mgr, HWND ui_hwnd);
void feed_manager_shutdown(AsyncFeedManager* mgr);
void feed_manager_set_active_symbol(AsyncFeedManager* mgr, const wchar_t* symbol);
void feed_manager_set_active_entry(AsyncFeedManager* mgr, uint32_t entry_id);
void feed_manager_set_timeframe(AsyncFeedManager* mgr, TimeframeSec tf);
void feed_manager_set_primary_source(AsyncFeedManager* mgr, FeedSourceId src);
void feed_manager_toggle_overlay(AsyncFeedManager* mgr, FeedSourceId src);

bool feed_manager_add_token(AsyncFeedManager* mgr, const wchar_t* symbol, const wchar_t* name, uint32_t section_id);
bool feed_manager_clone_entry(AsyncFeedManager* mgr, uint32_t entry_id);
void feed_manager_rename_entry(AsyncFeedManager* mgr, uint32_t entry_id, const wchar_t* display_name);
void feed_manager_move_entry(AsyncFeedManager* mgr, uint32_t entry_id, uint32_t target_section_id);
void feed_manager_reorder_entry(
    AsyncFeedManager* mgr,
    uint32_t entry_id,
    uint32_t target_section_id,
    uint32_t target_entry_id,
    bool insert_after
);
bool feed_manager_is_primary_entry(AsyncFeedManager* mgr, uint32_t entry_id, const wchar_t* symbol);
void feed_manager_remove_entry(AsyncFeedManager* mgr, uint32_t entry_id);
void feed_manager_remove_token(AsyncFeedManager* mgr, const wchar_t* symbol);

uint32_t feed_manager_add_section(AsyncFeedManager* mgr, const wchar_t* name);
void     feed_manager_rename_section(AsyncFeedManager* mgr, uint32_t section_id, const wchar_t* name);
void     feed_manager_toggle_section(AsyncFeedManager* mgr, uint32_t section_id);
void     feed_manager_reorder_section(
    AsyncFeedManager* mgr,
    uint32_t section_id,
    uint32_t target_section_id,
    bool insert_after
);
void     feed_manager_delete_section(AsyncFeedManager* mgr, uint32_t section_id);

int64_t feed_bucket_start_sec(int64_t ts_sec, TimeframeSec tf);

size_t feed_manager_copy_candles(
    AsyncFeedManager* mgr,
    FeedSourceId src,
    CandleBar* out_bars,
    size_t max_bars
);

size_t feed_manager_copy_watchlist(
    AsyncFeedManager* mgr,
    WatchlistItem* out_items,
    size_t max_items
);

size_t feed_manager_copy_sections(
    AsyncFeedManager* mgr,
    WatchlistSection* out_sections,
    size_t max_sections
);

void feed_manager_copy_sources(
    AsyncFeedManager* mgr,
    SourceStatusInfo out_sources[SRC_COUNT]
);

#endif /* OV_FEED_MANAGER_H */
