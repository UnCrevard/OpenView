import {
  AlertCondition,
  AlertRule,
  AlertTriggerLog,
  CandleBar,
  ConditionMetric,
  WatchlistToken,
} from '../types/trading';
import {
  computeBollingerBands,
  computeEMA,
  computeMACD,
  computeRSI,
  computeSMA,
} from './indicators';

function resolveMetricSeries(
  candles: CandleBar[],
  metric: ConditionMetric,
  param: number | undefined,
  spreadBps: number
): { current: number | null; previous: number | null } {
  const n = candles.length;
  if (n < 2) return { current: null, previous: null };
  const closes = candles.map((c) => c.close);

  switch (metric) {
    case 'price':
      return { current: closes[n - 1], previous: closes[n - 2] };
    case 'ema': {
      const series = computeEMA(closes, Math.max(2, Math.round(param ?? 20)));
      return { current: series[n - 1], previous: series[n - 2] };
    }
    case 'sma': {
      const series = computeSMA(closes, Math.max(2, Math.round(param ?? 50)));
      return { current: series[n - 1], previous: series[n - 2] };
    }
    case 'rsi': {
      const series = computeRSI(closes, Math.max(2, Math.round(param ?? 14)));
      return { current: series[n - 1], previous: series[n - 2] };
    }
    case 'macd': {
      const res = computeMACD(closes, 12, 26, 9);
      return { current: res.macd[n - 1], previous: res.macd[n - 2] };
    }
    case 'macd_signal': {
      const res = computeMACD(closes, 12, 26, 9);
      return { current: res.signal[n - 1], previous: res.signal[n - 2] };
    }
    case 'bb_upper': {
      const res = computeBollingerBands(closes, Math.max(2, Math.round(param ?? 20)), 2);
      return { current: res.upper[n - 1], previous: res.upper[n - 2] };
    }
    case 'bb_lower': {
      const res = computeBollingerBands(closes, Math.max(2, Math.round(param ?? 20)), 2);
      return { current: res.lower[n - 1], previous: res.lower[n - 2] };
    }
    case 'spread_bps':
      return { current: spreadBps, previous: spreadBps };
  }
}

export function evaluateCondition(
  cond: AlertCondition,
  candles: CandleBar[],
  spreadBps: number
): boolean {
  const left = resolveMetricSeries(candles, cond.leftMetric, cond.leftParam, spreadBps);
  if (left.current === null) return false;

  let rightCur: number | null = null;
  let rightPrev: number | null = null;

  if (cond.rightType === 'value') {
    rightCur = cond.rightValue ?? 0;
    rightPrev = rightCur;
  } else {
    const right = resolveMetricSeries(
      candles,
      cond.rightMetric ?? 'ema',
      cond.rightParam,
      spreadBps
    );
    rightCur = right.current;
    rightPrev = right.previous;
  }

  if (rightCur === null) return false;

  switch (cond.operator) {
    case '>':
      return left.current > rightCur;
    case '<':
      return left.current < rightCur;
    case '>=':
      return left.current >= rightCur;
    case '<=':
      return left.current <= rightCur;
    case 'cross_above':
      if (left.previous === null || rightPrev === null) return false;
      return left.previous <= rightPrev && left.current > rightCur;
    case 'cross_below':
      if (left.previous === null || rightPrev === null) return false;
      return left.previous >= rightPrev && left.current < rightCur;
  }
}

export function computeSpreadBps(token?: WatchlistToken): number {
  if (!token) return 0;
  const prices = Object.values(token.sourcePrices).filter(
    (p): p is number => typeof p === 'number' && p > 0
  );
  if (prices.length < 2) return 0;
  const minP = Math.min(...prices);
  const maxP = Math.max(...prices);
  return minP > 0 ? ((maxP - minP) / minP) * 10000 : 0;
}

export function formatConditionSummary(cond: AlertCondition): string {
  const left =
    cond.leftParam && cond.leftMetric !== 'price' && cond.leftMetric !== 'spread_bps'
      ? `${cond.leftMetric.toUpperCase()}(${cond.leftParam})`
      : cond.leftMetric.toUpperCase();
  const right =
    cond.rightType === 'value'
      ? String(cond.rightValue ?? 0)
      : cond.rightParam &&
          cond.rightMetric !== 'price' &&
          cond.rightMetric !== 'spread_bps'
        ? `${(cond.rightMetric ?? 'ema').toUpperCase()}(${cond.rightParam})`
        : (cond.rightMetric ?? 'ema').toUpperCase();

  return `${left} ${cond.operator} ${right}`;
}

export function evaluateAlertRules(
  rules: AlertRule[],
  activeSymbol: string,
  candles: CandleBar[],
  token?: WatchlistToken,
  nowMs: number = Date.now()
): { triggeredLogs: AlertTriggerLog[]; updatedRules: AlertRule[] } {
  if (candles.length < 2) return { triggeredLogs: [], updatedRules: rules };

  const spreadBps = computeSpreadBps(token);
  const lastPrice = candles[candles.length - 1].close;
  const triggeredLogs: AlertTriggerLog[] = [];

  const updatedRules = rules.map((rule) => {
    if (!rule.enabled || rule.symbol !== activeSymbol || rule.conditions.length === 0) {
      return rule;
    }

    const cooldownMs = Math.max(5, rule.cooldownSec) * 1000;
    if (rule.lastTriggeredAt && nowMs - rule.lastTriggeredAt < cooldownMs) {
      return rule;
    }

    const results = rule.conditions.map((c) => evaluateCondition(c, candles, spreadBps));
    const matched =
      rule.logic === 'AND' ? results.every(Boolean) : results.some(Boolean);

    if (!matched) return rule;

    const summary = rule.conditions.map(formatConditionSummary).join(` ${rule.logic} `);
    triggeredLogs.push({
      id: `log_${nowMs}_${Math.random().toString(36).slice(2, 7)}`,
      ruleId: rule.id,
      ruleName: rule.name,
      symbol: rule.symbol,
      price: lastPrice,
      timestamp: nowMs,
      summary,
    });

    return {
      ...rule,
      lastTriggeredAt: nowMs,
      triggerCount: rule.triggerCount + 1,
    };
  });

  return { triggeredLogs, updatedRules };
}

export function playAlertBeep(): void {
  try {
    const AudioCtx =
      window.AudioContext ||
      (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
    if (!AudioCtx) return;
    const ctx = new AudioCtx();
    const osc = ctx.createOscillator();
    const gain = ctx.createGain();
    osc.type = 'sine';
    osc.frequency.setValueAtTime(880, ctx.currentTime);
    osc.frequency.exponentialRampToValueAtTime(1174.66, ctx.currentTime + 0.12);
    gain.gain.setValueAtTime(0.08, ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + 0.25);
    osc.connect(gain);
    gain.connect(ctx.destination);
    osc.start();
    osc.stop(ctx.currentTime + 0.26);
  } catch {
    // Ignore audio context restrictions before user gesture
  }
}
