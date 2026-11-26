export type DataSourceId = 'binance' | 'coinbase' | 'kraken';

export type Timeframe = '1m' | '5m' | '15m' | '1h' | '4h' | '1d' | '1w' | '1M' | '1y';

export interface CandleBar {
  time: number; // Unix timestamp in seconds (UTC)
  open: number;
  high: number;
  low: number;
  close: number;
  volume: number;
  source: DataSourceId;
}

export interface WatchlistToken {
  symbol: string; // e.g. "BTC/USDT"
  base: string; // e.g. "BTC"
  quote: string; // e.g. "USDT"
  name: string;
  price: number;
  open24h: number;
  change24h: number;
  high24h: number;
  low24h: number;
  volume24h: number;
  sourcePrices: Partial<Record<DataSourceId, number>>;
  lastUpdate: number;
}

export interface WatchlistSection {
  id: string;
  name: string;
  collapsed?: boolean;
}

export interface WatchlistEntry {
  id: string;
  symbol: string;
  displayName?: string;
  sectionId: string;
}

export interface SourceStatus {
  id: DataSourceId;
  label: string;
  endpoint: string;
  state: 'live' | 'syncing' | 'fallback' | 'offline';
  latencyMs: number;
  lastTickAt: number;
  ticksReceived: number;
  modeLabel: string;
}

export type IndicatorType = 'EMA' | 'SMA' | 'BB' | 'RSI' | 'MACD';

export interface IndicatorConfig {
  id: string;
  type: IndicatorType;
  label: string;
  enabled: boolean;
  pane: 'main' | 'rsi' | 'macd';
  params: Record<string, number>;
  color: string;
  secondaryColor?: string;
  tertiaryColor?: string;
}

export type DrawingToolType =
  | 'cursor'
  | 'trendline'
  | 'hline'
  | 'fib'
  | 'rect'
  | 'channel'
  | 'text';

export interface DrawingPoint {
  time: number; // Unix timestamp in seconds
  price: number;
}

export type DrawingLineStyle = 'solid' | 'dashed' | 'dotted';
export type DrawingLabelAlignH = 'left' | 'center' | 'right';
export type DrawingLabelAlignV = 'top' | 'middle' | 'bottom';

export interface ChartDrawing {
  id: string;
  entryId?: string;
  symbol: string;
  tool: Exclude<DrawingToolType, 'cursor'>;
  points: DrawingPoint[];
  color: string;
  lineWidth: number;
  lineStyle?: DrawingLineStyle;
  text?: string;
  labelAlignH?: DrawingLabelAlignH;
  labelAlignV?: DrawingLabelAlignV;
  locked?: boolean;
}

export type ConditionMetric =
  | 'price'
  | 'ema'
  | 'sma'
  | 'rsi'
  | 'macd'
  | 'macd_signal'
  | 'bb_upper'
  | 'bb_lower'
  | 'spread_bps';

export type ConditionOperator =
  | '>'
  | '<'
  | '>='
  | '<='
  | 'cross_above'
  | 'cross_below';

export interface AlertCondition {
  id: string;
  leftMetric: ConditionMetric;
  leftParam?: number;
  operator: ConditionOperator;
  rightType: 'value' | 'metric';
  rightValue?: number;
  rightMetric?: ConditionMetric;
  rightParam?: number;
}

export interface AlertRule {
  id: string;
  name: string;
  symbol: string;
  logic: 'AND' | 'OR';
  conditions: AlertCondition[];
  enabled: boolean;
  sound: boolean;
  cooldownSec: number;
  lastTriggeredAt?: number;
  triggerCount: number;
}

export interface AlertTriggerLog {
  id: string;
  ruleId: string;
  ruleName: string;
  symbol: string;
  price: number;
  timestamp: number;
  summary: string;
}

export interface FeedSnapshotMessage {
  type: 'snapshot';
  symbol: string;
  timeframe: Timeframe;
  primarySource: DataSourceId;
  candles: CandleBar[];
  secondarySeries: Partial<Record<DataSourceId, { time: number; close: number }[]>>;
  watchlist: WatchlistToken[];
  sources: SourceStatus[];
}

export interface FeedTickMessage {
  type: 'tick';
  symbol: string;
  timeframe: Timeframe;
  source: DataSourceId;
  candle: CandleBar;
  watchlistItem?: WatchlistToken;
  sources?: SourceStatus[];
}

export interface WatchlistUpdateMessage {
  type: 'watchlist';
  watchlist: WatchlistToken[];
  sources: SourceStatus[];
}

export type ServerWsMessage =
  | FeedSnapshotMessage
  | FeedTickMessage
  | WatchlistUpdateMessage;
