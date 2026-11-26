import { CandleBar, IndicatorConfig } from '../types/trading';

export interface BollingerResult {
  upper: (number | null)[];
  middle: (number | null)[];
  lower: (number | null)[];
}

export interface MacdResult {
  macd: (number | null)[];
  signal: (number | null)[];
  histogram: (number | null)[];
}

export interface ComputedIndicators {
  byId: Record<
    string,
    {
      config: IndicatorConfig;
      values?: (number | null)[];
      bollinger?: BollingerResult;
      macd?: MacdResult;
    }
  >;
}

export function computeSMA(closes: number[], period: number): (number | null)[] {
  const n = closes.length;
  const out: (number | null)[] = new Array(n).fill(null);
  if (period <= 0 || n < period) return out;

  let sum = 0;
  for (let i = 0; i < n; i++) {
    sum += closes[i];
    if (i >= period) {
      sum -= closes[i - period];
    }
    if (i >= period - 1) {
      out[i] = sum / period;
    }
  }
  return out;
}

export function computeEMA(closes: number[], period: number): (number | null)[] {
  const n = closes.length;
  const out: (number | null)[] = new Array(n).fill(null);
  if (period <= 0 || n < period) return out;

  const k = 2 / (period + 1);
  let sum = 0;
  for (let i = 0; i < period; i++) {
    sum += closes[i];
  }
  let ema = sum / period;
  out[period - 1] = ema;

  for (let i = period; i < n; i++) {
    ema = closes[i] * k + ema * (1 - k);
    out[i] = ema;
  }
  return out;
}

export function computeBollingerBands(
  closes: number[],
  period: number,
  stdDevMult: number
): BollingerResult {
  const n = closes.length;
  const middle = computeSMA(closes, period);
  const upper: (number | null)[] = new Array(n).fill(null);
  const lower: (number | null)[] = new Array(n).fill(null);

  if (period <= 0 || n < period) {
    return { upper, middle, lower };
  }

  for (let i = period - 1; i < n; i++) {
    const mean = middle[i];
    if (mean === null) continue;
    let sqSum = 0;
    for (let j = i - period + 1; j <= i; j++) {
      const diff = closes[j] - mean;
      sqSum += diff * diff;
    }
    const sd = Math.sqrt(sqSum / period);
    upper[i] = mean + stdDevMult * sd;
    lower[i] = mean - stdDevMult * sd;
  }

  return { upper, middle, lower };
}

export function computeRSI(closes: number[], period: number): (number | null)[] {
  const n = closes.length;
  const out: (number | null)[] = new Array(n).fill(null);
  if (period <= 0 || n <= period) return out;

  let avgGain = 0;
  let avgLoss = 0;
  for (let i = 1; i <= period; i++) {
    const delta = closes[i] - closes[i - 1];
    if (delta >= 0) avgGain += delta;
    else avgLoss -= delta;
  }
  avgGain /= period;
  avgLoss /= period;

  out[period] = avgLoss === 0 ? 100 : 100 - 100 / (1 + avgGain / avgLoss);

  for (let i = period + 1; i < n; i++) {
    const delta = closes[i] - closes[i - 1];
    const gain = delta > 0 ? delta : 0;
    const loss = delta < 0 ? -delta : 0;
    avgGain = (avgGain * (period - 1) + gain) / period;
    avgLoss = (avgLoss * (period - 1) + loss) / period;
    out[i] = avgLoss === 0 ? 100 : 100 - 100 / (1 + avgGain / avgLoss);
  }

  return out;
}

export function computeMACD(
  closes: number[],
  fastPeriod: number,
  slowPeriod: number,
  signalPeriod: number
): MacdResult {
  const n = closes.length;
  const macd: (number | null)[] = new Array(n).fill(null);
  const signal: (number | null)[] = new Array(n).fill(null);
  const histogram: (number | null)[] = new Array(n).fill(null);

  const fastEma = computeEMA(closes, fastPeriod);
  const slowEma = computeEMA(closes, slowPeriod);

  const validMacdValues: number[] = [];
  const validIndices: number[] = [];

  for (let i = 0; i < n; i++) {
    const f = fastEma[i];
    const s = slowEma[i];
    if (f !== null && s !== null) {
      const m = f - s;
      macd[i] = m;
      validMacdValues.push(m);
      validIndices.push(i);
    }
  }

  if (validMacdValues.length >= signalPeriod && signalPeriod > 0) {
    const sigArr = computeEMA(validMacdValues, signalPeriod);
    for (let j = 0; j < sigArr.length; j++) {
      const sigVal = sigArr[j];
      const origIdx = validIndices[j];
      if (sigVal !== null && macd[origIdx] !== null) {
        signal[origIdx] = sigVal;
        histogram[origIdx] = (macd[origIdx] as number) - sigVal;
      }
    }
  }

  return { macd, signal, histogram };
}

export function computeAllIndicators(
  candles: CandleBar[],
  configs: IndicatorConfig[]
): ComputedIndicators {
  const closes = candles.map((c) => c.close);
  const byId: ComputedIndicators['byId'] = {};

  for (const config of configs) {
    if (!config.enabled) continue;
    switch (config.type) {
      case 'EMA': {
        const period = Math.max(2, Math.round(config.params.period ?? 20));
        byId[config.id] = { config, values: computeEMA(closes, period) };
        break;
      }
      case 'SMA': {
        const period = Math.max(2, Math.round(config.params.period ?? 50));
        byId[config.id] = { config, values: computeSMA(closes, period) };
        break;
      }
      case 'BB': {
        const period = Math.max(2, Math.round(config.params.period ?? 20));
        const stdDev = Math.max(0.5, config.params.stdDev ?? 2);
        byId[config.id] = {
          config,
          bollinger: computeBollingerBands(closes, period, stdDev),
        };
        break;
      }
      case 'RSI': {
        const period = Math.max(2, Math.round(config.params.period ?? 14));
        byId[config.id] = { config, values: computeRSI(closes, period) };
        break;
      }
      case 'MACD': {
        const fast = Math.max(2, Math.round(config.params.fast ?? 12));
        const slow = Math.max(fast + 1, Math.round(config.params.slow ?? 26));
        const signal = Math.max(2, Math.round(config.params.signal ?? 9));
        byId[config.id] = {
          config,
          macd: computeMACD(closes, fast, slow, signal),
        };
        break;
      }
    }
  }

  return { byId };
}
