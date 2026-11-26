#ifndef OV_ALERTS_H
#define OV_ALERTS_H

#include "common.h"
#include "indicators.h"

typedef enum {
    METRIC_PRICE = 0,
    METRIC_EMA_FAST,
    METRIC_EMA_SLOW,
    METRIC_SMA,
    METRIC_RSI,
    METRIC_MACD,
    METRIC_MACD_SIGNAL,
    METRIC_BB_UPPER,
    METRIC_BB_LOWER,
    METRIC_SPREAD_BPS
} AlertMetric;

typedef enum {
    OP_GT = 0,
    OP_LT,
    OP_GTE,
    OP_LTE,
    OP_CROSS_ABOVE,
    OP_CROSS_BELOW
} AlertOp;

typedef struct {
    AlertMetric left_metric;
    AlertOp     op;
    bool        right_is_metric;
    double      right_value;
    AlertMetric right_metric;
} AlertCondition;

typedef enum {
    LOGIC_AND = 0,
    LOGIC_OR  = 1
} AlertLogic;

typedef struct {
    uint32_t       id;
    wchar_t        name[64];
    wchar_t        symbol[24];
    AlertLogic     logic;
    AlertCondition conditions[MAX_ALERT_CONDS];
    size_t         condition_count;
    bool           enabled;
    bool           sound;
    int64_t        cooldown_ms;
    int64_t        last_fired_ms;
    uint32_t       fire_count;
} AlertRule;

typedef struct {
    uint32_t rule_id;
    wchar_t  rule_name[64];
    wchar_t  symbol[24];
    double   price;
    int64_t  timestamp_ms;
    wchar_t  summary[96];
} AlertLogEntry;

typedef struct {
    AlertRule     rules[MAX_ALERTS];
    size_t        rule_count;
    uint32_t      next_rule_id;
    AlertLogEntry logs[MAX_ALERT_LOGS];
    size_t        log_count;
} AlertEngine;

void alerts_init(AlertEngine* engine);
uint32_t alerts_add_rule(AlertEngine* engine, const AlertRule* rule);
void alerts_toggle_rule(AlertEngine* engine, uint32_t id);
void alerts_delete_rule(AlertEngine* engine, uint32_t id);
void alerts_clear_logs(AlertEngine* engine);

void alerts_evaluate(
    AlertEngine* engine,
    const wchar_t* symbol,
    const CandleBar* bars,
    size_t bar_count,
    const IndicatorBuffers* ind,
    const WatchlistItem* token_info,
    int64_t now_ms,
    HWND notify_hwnd
);

#endif /* OV_ALERTS_H */
