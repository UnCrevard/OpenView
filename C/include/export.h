#ifndef OV_EXPORT_H
#define OV_EXPORT_H

#include "common.h"
#include "indicators.h"
#include "drawings.h"
#include "alerts.h"

bool export_candles_csv_w(
    const wchar_t* file_path,
    const wchar_t* symbol,
    TimeframeSec timeframe,
    FeedSourceId source,
    const CandleBar* bars,
    size_t count,
    const IndicatorSettings* cfg,
    const IndicatorBuffers* ind
);

bool export_workspace_json_w(
    const wchar_t* file_path,
    uint32_t entry_id,
    bool is_primary_entry,
    const wchar_t* symbol,
    TimeframeSec timeframe,
    FeedSourceId source,
    const CandleBar* bars,
    size_t count,
    const DrawingStore* drawings,
    const AlertEngine* alerts
);

#endif /* OV_EXPORT_H */
