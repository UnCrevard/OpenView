#include "alerts.h"
#include <stdio.h>
#include <string.h>

static bool read_metric(
    AlertMetric m,
    size_t idx,
    const CandleBar* bars,
    const IndicatorBuffers* ind,
    const WatchlistItem* token_info,
    double* out_val
) {
    switch (m) {
        case METRIC_PRICE:       *out_val = bars[idx].close; return true;
        case METRIC_EMA_FAST:    *out_val = ind->ema_fast[idx]; return ind->valid_ema_fast[idx];
        case METRIC_EMA_SLOW:    *out_val = ind->ema_slow[idx]; return ind->valid_ema_slow[idx];
        case METRIC_SMA:         *out_val = ind->sma[idx]; return ind->valid_sma[idx];
        case METRIC_RSI:         *out_val = ind->rsi[idx]; return ind->valid_rsi[idx];
        case METRIC_MACD:        *out_val = ind->macd_line[idx]; return ind->valid_macd[idx];
        case METRIC_MACD_SIGNAL: *out_val = ind->macd_signal[idx]; return ind->valid_macd[idx];
        case METRIC_BB_UPPER:    *out_val = ind->bb_upper[idx]; return ind->valid_bb[idx];
        case METRIC_BB_LOWER:    *out_val = ind->bb_lower[idx]; return ind->valid_bb[idx];
        case METRIC_SPREAD_BPS: {
            if (!token_info || token_info->price <= 0.0) return false;
            double lo = token_info->source_prices[0];
            double hi = token_info->source_prices[0];
            for (int s = 1; s < SRC_COUNT; s++) {
                double p = token_info->source_prices[s];
                if (p <= 0.0) continue;
                if (lo <= 0.0 || p < lo) lo = p;
                if (p > hi) hi = p;
            }
            if (lo <= 0.0 || hi <= 0.0) return false;
            *out_val = ((hi - lo) / token_info->price) * 10000.0;
            return true;
        }
    }
    return false;
}

static bool eval_single(
    const AlertCondition* c,
    size_t cur_idx,
    const CandleBar* bars,
    const IndicatorBuffers* ind,
    const WatchlistItem* token_info
) {
    if (cur_idx == 0) return false;
    double l_cur = 0.0, l_prev = 0.0, r_cur = c->right_value, r_prev = c->right_value;

    if (!read_metric(c->left_metric, cur_idx, bars, ind, token_info, &l_cur)) return false;
    if (c->right_is_metric) {
        if (!read_metric(c->right_metric, cur_idx, bars, ind, token_info, &r_cur)) return false;
    }

    switch (c->op) {
        case OP_GT:  return l_cur > r_cur;
        case OP_LT:  return l_cur < r_cur;
        case OP_GTE: return l_cur >= r_cur;
        case OP_LTE: return l_cur <= r_cur;
        case OP_CROSS_ABOVE:
            if (!read_metric(c->left_metric, cur_idx - 1, bars, ind, token_info, &l_prev)) return false;
            if (c->right_is_metric &&
                !read_metric(c->right_metric, cur_idx - 1, bars, ind, token_info, &r_prev)) return false;
            return (l_prev <= r_prev) && (l_cur > r_cur);
        case OP_CROSS_BELOW:
            if (!read_metric(c->left_metric, cur_idx - 1, bars, ind, token_info, &l_prev)) return false;
            if (c->right_is_metric &&
                !read_metric(c->right_metric, cur_idx - 1, bars, ind, token_info, &r_prev)) return false;
            return (l_prev >= r_prev) && (l_cur < r_cur);
    }
    return false;
}

void alerts_init(AlertEngine* engine) {
    ZeroMemory(engine, sizeof(*engine));
    engine->next_rule_id = 1;

    AlertRule r = {0};
    ov_wcscpy(r.name, 64, L"Croisement EMA Haussier + RSI sain");
    ov_wcscpy(r.symbol, 24, L"BTC/USDT");
    r.logic = LOGIC_AND;
    r.enabled = true;
    r.sound = true;
    r.cooldown_ms = 60000;
    r.condition_count = 2;
    r.conditions[0] = (AlertCondition){
        .left_metric     = METRIC_EMA_FAST,
        .op              = OP_CROSS_ABOVE,
        .right_is_metric = true,
        .right_metric    = METRIC_EMA_SLOW
    };
    r.conditions[1] = (AlertCondition){
        .left_metric     = METRIC_RSI,
        .op              = OP_LT,
        .right_is_metric = false,
        .right_value     = 70.0
    };
    alerts_add_rule(engine, &r);
}

uint32_t alerts_add_rule(AlertEngine* engine, const AlertRule* rule) {
    if (engine->rule_count >= MAX_ALERTS) return 0;
    AlertRule* dst = &engine->rules[engine->rule_count++];
    *dst = *rule;
    dst->id = engine->next_rule_id++;
    dst->last_fired_ms = 0;
    dst->fire_count = 0;
    return dst->id;
}

void alerts_toggle_rule(AlertEngine* engine, uint32_t id) {
    for (size_t i = 0; i < engine->rule_count; i++) {
        if (engine->rules[i].id == id) {
            engine->rules[i].enabled = !engine->rules[i].enabled;
            break;
        }
    }
}

void alerts_delete_rule(AlertEngine* engine, uint32_t id) {
    for (size_t i = 0; i < engine->rule_count; i++) {
        if (engine->rules[i].id == id) {
            for (size_t j = i; j + 1 < engine->rule_count; j++) {
                engine->rules[j] = engine->rules[j + 1];
            }
            engine->rule_count--;
            break;
        }
    }
}

void alerts_clear_logs(AlertEngine* engine) {
    engine->log_count = 0;
}

void alerts_evaluate(
    AlertEngine* engine,
    const wchar_t* symbol,
    const CandleBar* bars,
    size_t bar_count,
    const IndicatorBuffers* ind,
    const WatchlistItem* token_info,
    int64_t now_ms,
    HWND notify_hwnd
) {
    if (bar_count < 2) return;
    size_t last = bar_count - 1;

    for (size_t i = 0; i < engine->rule_count; i++) {
        AlertRule* r = &engine->rules[i];
        if (!r->enabled || wcscmp(r->symbol, symbol) != 0 || r->condition_count == 0) continue;
        if (r->last_fired_ms > 0 && (now_ms - r->last_fired_ms) < r->cooldown_ms) continue;

        bool matched = (r->logic == LOGIC_AND);
        for (size_t c = 0; c < r->condition_count; c++) {
            bool res = eval_single(&r->conditions[c], last, bars, ind, token_info);
            if (r->logic == LOGIC_AND && !res) { matched = false; break; }
            if (r->logic == LOGIC_OR && res)   { matched = true; break; }
        }

        if (matched) {
            r->last_fired_ms = now_ms;
            r->fire_count++;
            if (engine->log_count >= MAX_ALERT_LOGS) {
                memmove(&engine->logs[1], &engine->logs[0], (MAX_ALERT_LOGS - 1) * sizeof(AlertLogEntry));
            } else {
                if (engine->log_count > 0) {
                    memmove(&engine->logs[1], &engine->logs[0], engine->log_count * sizeof(AlertLogEntry));
                }
                engine->log_count++;
            }
            AlertLogEntry* entry = &engine->logs[0];
            ZeroMemory(entry, sizeof(*entry));
            entry->rule_id = r->id;
            ov_wcscpy(entry->rule_name, 64, r->name);
            ov_wcscpy(entry->symbol, 24, r->symbol);
            entry->price        = bars[last].close;
            entry->timestamp_ms = now_ms;
            swprintf(
                entry->summary, 96, L"%ls · %.2f · %ls",
                r->symbol, bars[last].close,
                r->logic == LOGIC_AND ? L"AND" : L"OR"
            );

            if (r->sound) {
                MessageBeep(MB_ICONASTERISK);
            }
            if (notify_hwnd) {
                PostMessageW(notify_hwnd, WM_APP_ALERT_FIRED, (WPARAM)r->id, 0);
            }
        }
    }
}
