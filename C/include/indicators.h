#ifndef OV_INDICATORS_H
#define OV_INDICATORS_H

#include "common.h"

typedef struct {
    bool   enable_ema_fast;
    bool   enable_ema_slow;
    bool   enable_sma;
    bool   enable_bb;
    bool   enable_rsi;
    bool   enable_macd;
    int    ema_fast_period;
    int    ema_slow_period;
    int    sma_period;
    int    bb_period;
    double bb_stddev;
    int    rsi_period;
    int    macd_fast;
    int    macd_slow;
    int    macd_signal;
} IndicatorSettings;

typedef struct {
    double ema_fast[MAX_CANDLES];
    double ema_slow[MAX_CANDLES];
    double sma[MAX_CANDLES];
    double bb_upper[MAX_CANDLES];
    double bb_mid[MAX_CANDLES];
    double bb_lower[MAX_CANDLES];
    double rsi[MAX_CANDLES];
    double macd_line[MAX_CANDLES];
    double macd_signal[MAX_CANDLES];
    double macd_hist[MAX_CANDLES];
    bool   valid_ema_fast[MAX_CANDLES];
    bool   valid_ema_slow[MAX_CANDLES];
    bool   valid_sma[MAX_CANDLES];
    bool   valid_bb[MAX_CANDLES];
    bool   valid_rsi[MAX_CANDLES];
    bool   valid_macd[MAX_CANDLES];
} IndicatorBuffers;

void indicators_default_settings(IndicatorSettings* cfg);
int  indicators_active_count(const IndicatorSettings* cfg);
void indicators_compute_all(
    const CandleBar* bars,
    size_t count,
    const IndicatorSettings* cfg,
    IndicatorBuffers* out
);

#endif /* OV_INDICATORS_H */
