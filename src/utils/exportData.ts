import {
  AlertRule,
  AlertTriggerLog,
  CandleBar,
  ChartDrawing,
  DataSourceId,
  IndicatorConfig,
  Timeframe,
} from '../types/trading';
import { ComputedIndicators } from './indicators';

function triggerDownload(filename: string, content: string, mimeType: string) {
  const blob = new Blob([content], { type: mimeType });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
}

export function exportCandlesToCSV(
  symbol: string,
  timeframe: Timeframe,
  source: DataSourceId,
  candles: CandleBar[],
  computed: ComputedIndicators,
  indicators: IndicatorConfig[]
): void {
  const enabled = indicators.filter((ind) => ind.enabled);
  const headers = [
    'timestamp_utc',
    'iso_date',
    'symbol',
    'timeframe',
    'source',
    'open',
    'high',
    'low',
    'close',
    'volume',
  ];

  for (const ind of enabled) {
    if (ind.type === 'BB') {
      headers.push(`${ind.label}_upper`, `${ind.label}_mid`, `${ind.label}_lower`);
    } else if (ind.type === 'MACD') {
      headers.push(`${ind.label}_macd`, `${ind.label}_signal`, `${ind.label}_hist`);
    } else {
      headers.push(ind.label);
    }
  }

  const rows: string[] = [headers.join(',')];

  for (let i = 0; i < candles.length; i++) {
    const c = candles[i];
    const cols: string[] = [
      String(c.time),
      new Date(c.time * 1000).toISOString(),
      symbol,
      timeframe,
      source,
      c.open.toFixed(6),
      c.high.toFixed(6),
      c.low.toFixed(6),
      c.close.toFixed(6),
      c.volume.toFixed(4),
    ];

    for (const ind of enabled) {
      const res = computed.byId[ind.id];
      if (!res) {
        if (ind.type === 'BB' || ind.type === 'MACD') cols.push('', '', '');
        else cols.push('');
        continue;
      }
      if (ind.type === 'BB' && res.bollinger) {
        const u = res.bollinger.upper[i];
        const m = res.bollinger.middle[i];
        const l = res.bollinger.lower[i];
        cols.push(
          u !== null ? u.toFixed(6) : '',
          m !== null ? m.toFixed(6) : '',
          l !== null ? l.toFixed(6) : ''
        );
      } else if (ind.type === 'MACD' && res.macd) {
        const m = res.macd.macd[i];
        const s = res.macd.signal[i];
        const h = res.macd.histogram[i];
        cols.push(
          m !== null ? m.toFixed(6) : '',
          s !== null ? s.toFixed(6) : '',
          h !== null ? h.toFixed(6) : ''
        );
      } else if (res.values) {
        const v = res.values[i];
        cols.push(v !== null ? v.toFixed(6) : '');
      }
    }
    rows.push(cols.join(','));
  }

  const safeSymbol = symbol.replace(/[^A-Za-z0-9_-]/g, '_');
  const bom = '\uFEFF';
  triggerDownload(
    `openview_${safeSymbol}_${timeframe}_${source}.csv`,
    bom + rows.join('\n'),
    'text/csv;charset=utf-8;'
  );
}

export function exportWorkspaceToJSON(payload: {
  symbol: string;
  timeframe: Timeframe;
  source: DataSourceId;
  candles: CandleBar[];
  indicators: IndicatorConfig[];
  computed: ComputedIndicators;
  drawings: ChartDrawing[];
  alerts: AlertRule[];
  alertLogs: AlertTriggerLog[];
}): void {
  const { symbol, timeframe, source, candles, indicators, computed, drawings, alerts, alertLogs } =
    payload;

  const serializedIndicators = indicators
    .filter((i) => i.enabled)
    .map((ind) => ({
      ...ind,
      series: computed.byId[ind.id]
        ? computed.byId[ind.id].bollinger ?? computed.byId[ind.id].macd ?? computed.byId[ind.id].values
        : null,
    }));

  const doc = {
    exportedAt: new Date().toISOString(),
    symbol,
    timeframe,
    primarySource: source,
    candleCount: candles.length,
    candles,
    indicators: serializedIndicators,
    drawings: drawings.filter((d) => d.symbol === symbol),
    alerts,
    alertLogs,
  };

  const safeSymbol = symbol.replace(/[^A-Za-z0-9_-]/g, '_');
  triggerDownload(
    `openview_${safeSymbol}_${timeframe}_${source}.json`,
    JSON.stringify(doc, null, 2),
    'application/json;charset=utf-8;'
  );
}
