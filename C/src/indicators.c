#include "indicators.h"
#include <math.h>
#include <string.h>

void indicators_default_settings(IndicatorSettings* cfg) {
    cfg->enable_ema_fast = true;
    cfg->enable_ema_slow = true;
    cfg->enable_sma      = false;
    cfg->enable_bb       = false;
    cfg->enable_rsi      = true;
    cfg->enable_macd     = false;

    cfg->ema_fast_period = 12;
    cfg->ema_slow_period = 26;
    cfg->sma_period      = 50;
    cfg->bb_period       = 20;
    cfg->bb_stddev       = 2.0;
    cfg->rsi_period      = 14;
    cfg->macd_fast       = 12;
    cfg->macd_slow       = 26;
    cfg->macd_signal     = 9;
}

int indicators_active_count(const IndicatorSettings* cfg) {
    int n = 0;
    if (cfg->enable_ema_fast) n++;
    if (cfg->enable_ema_slow) n++;
    if (cfg->enable_sma)      n++;
    if (cfg->enable_bb)       n++;
    if (cfg->enable_rsi)      n++;
    if (cfg->enable_macd)     n++;
    return n;
}

static void calc_sma(
    const CandleBar* bars,
    size_t count,
    int period,
    double* out,
    bool* valid
) {
    memset(valid, 0, sizeof(bool) * count);
    if (period <= 0 || count < (size_t)period) return;
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        sum += bars[i].close;
        if (i >= (size_t)period) sum -= bars[i - period].close;
        if (i >= (size_t)(period - 1)) {
            out[i]   = sum / (double)period;
            valid[i] = true;
        }
    }
}

static void calc_ema_values(
    const double* input,
    size_t count,
    int period,
    double* out,
    bool* valid
) {
    memset(valid, 0, sizeof(bool) * count);
    if (period <= 0 || count < (size_t)period) return;
    double k   = 2.0 / ((double)period + 1.0);
    double sum = 0.0;
    for (int i = 0; i < period; i++) sum += input[i];
    double ema = sum / (double)period;
    out[period - 1]   = ema;
    valid[period - 1] = true;

    for (size_t i = (size_t)period; i < count; i++) {
        ema      = input[i] * k + ema * (1.0 - k);
        out[i]   = ema;
        valid[i] = true;
    }
}

static void calc_bollinger(
    const CandleBar* bars,
    size_t count,
    int period,
    double mult,
    IndicatorBuffers* out
) {
    calc_sma(bars, count, period, out->bb_mid, out->valid_bb);
    if (period <= 0 || count < (size_t)period) return;
    for (size_t i = (size_t)(period - 1); i < count; i++) {
        double mean = out->bb_mid[i];
        double sq   = 0.0;
        for (size_t j = i - (size_t)period + 1; j <= i; j++) {
            double d = bars[j].close - mean;
            sq += d * d;
        }
        double sd = sqrt(sq / (double)period);
        out->bb_upper[i] = mean + mult * sd;
        out->bb_lower[i] = mean - mult * sd;
    }
}

static void calc_rsi(
    const CandleBar* bars,
    size_t count,
    int period,
    double* out,
    bool* valid
) {
    memset(valid, 0, sizeof(bool) * count);
    if (period <= 0 || count <= (size_t)period) return;
    double avg_gain = 0.0, avg_loss = 0.0;
    for (int i = 1; i <= period; i++) {
        double d = bars[i].close - bars[i - 1].close;
        if (d >= 0.0) avg_gain += d;
        else avg_loss -= d;
    }
    avg_gain /= (double)period;
    avg_loss /= (double)period;
    out[period]   = (avg_loss == 0.0) ? 100.0 : (100.0 - 100.0 / (1.0 + avg_gain / avg_loss));
    valid[period] = true;

    for (size_t i = (size_t)(period + 1); i < count; i++) {
        double d    = bars[i].close - bars[i - 1].close;
        double gain = d > 0.0 ? d : 0.0;
        double loss = d < 0.0 ? -d : 0.0;
        avg_gain    = (avg_gain * (period - 1) + gain) / (double)period;
        avg_loss    = (avg_loss * (period - 1) + loss) / (double)period;
        out[i]      = (avg_loss == 0.0) ? 100.0 : (100.0 - 100.0 / (1.0 + avg_gain / avg_loss));
        valid[i]    = true;
    }
}

void indicators_compute_all(
    const CandleBar* bars,
    size_t count,
    const IndicatorSettings* cfg,
    IndicatorBuffers* out
) {
    ZeroMemory(out, sizeof(*out));
    if (count == 0 || !cfg) return;
    if (count > MAX_CANDLES) count = MAX_CANDLES;

    double closes[MAX_CANDLES];
    for (size_t i = 0; i < count; i++) closes[i] = bars[i].close;

    calc_ema_values(closes, count, cfg->ema_fast_period, out->ema_fast, out->valid_ema_fast);
    calc_ema_values(closes, count, cfg->ema_slow_period, out->ema_slow, out->valid_ema_slow);
    calc_sma(bars, count, cfg->sma_period, out->sma, out->valid_sma);
    calc_bollinger(bars, count, cfg->bb_period, cfg->bb_stddev, out);
    calc_rsi(bars, count, cfg->rsi_period, out->rsi, out->valid_rsi);

    double fast[MAX_CANDLES], slow[MAX_CANDLES];
    bool vf[MAX_CANDLES], vs[MAX_CANDLES];
    calc_ema_values(closes, count, cfg->macd_fast, fast, vf);
    calc_ema_values(closes, count, cfg->macd_slow, slow, vs);

    double macd_compact[MAX_CANDLES];
    size_t macd_idx[MAX_CANDLES];
    size_t m_cnt = 0;

    for (size_t i = 0; i < count; i++) {
        if (vf[i] && vs[i]) {
            out->macd_line[i]   = fast[i] - slow[i];
            macd_compact[m_cnt] = out->macd_line[i];
            macd_idx[m_cnt]     = i;
            m_cnt++;
        }
    }

    if (m_cnt >= (size_t)cfg->macd_signal) {
        double sig[MAX_CANDLES];
        bool vsig[MAX_CANDLES];
        calc_ema_values(macd_compact, m_cnt, cfg->macd_signal, sig, vsig);
        for (size_t j = 0; j < m_cnt; j++) {
            if (vsig[j]) {
                size_t orig = macd_idx[j];
                out->macd_signal[orig] = sig[j];
                out->macd_hist[orig]   = out->macd_line[orig] - sig[j];
                out->valid_macd[orig]  = true;
            }
        }
    }
}
