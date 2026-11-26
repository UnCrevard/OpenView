import express from 'express';
import http from 'http';
import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';
import WebSocket, { WebSocketServer } from 'ws';
import JSZip from 'jszip';
import { C_WIN32_PROJECT_FILES } from './src/c_win32/cProjectFiles';
import type {
  CandleBar,
  DataSourceId,
  SourceStatus,
  Timeframe,
  WatchlistToken,
} from './src/types/trading';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const PORT = Number(process.env.PORT || 3000);

const TIMEFRAME_SECONDS: Record<Timeframe, number> = {
  '1m': 60,
  '5m': 300,
  '15m': 900,
  '1h': 3600,
  '4h': 14400,
  '1d': 86400,
  '1w': 604800,
  '1M': 2592000,
  '1y': 31536000,
};

const BINANCE_INTERVAL: Record<Timeframe, string> = {
  '1m': '1m',
  '5m': '5m',
  '15m': '15m',
  '1h': '1h',
  '4h': '4h',
  '1d': '1d',
  '1w': '1w',
  '1M': '1M',
  '1y': '1M',
};

const COINBASE_GRANULARITY: Record<Timeframe, number> = {
  '1m': 60,
  '5m': 300,
  '15m': 900,
  '1h': 3600,
  '4h': 14400,
  '1d': 86400,
  '1w': 86400,
  '1M': 86400,
  '1y': 86400,
};

const KRAKEN_INTERVAL: Record<Timeframe, number> = {
  '1m': 1,
  '5m': 5,
  '15m': 15,
  '1h': 60,
  '4h': 240,
  '1d': 1440,
  '1w': 10080,
  '1M': 10080,
  '1y': 10080,
};

function getBucketStartSec(tsSec: number, tf: Timeframe): number {
  if (tf === '1w') {
    const mondayOffset = 345600; // 1970-01-05 00:00:00 UTC (Monday)
    return Math.floor((tsSec - mondayOffset) / 604800) * 604800 + mondayOffset;
  }
  if (tf === '1M') {
    const d = new Date(tsSec * 1000);
    return Math.floor(Date.UTC(d.getUTCFullYear(), d.getUTCMonth(), 1) / 1000);
  }
  if (tf === '1y') {
    const d = new Date(tsSec * 1000);
    return Math.floor(Date.UTC(d.getUTCFullYear(), 0, 1) / 1000);
  }
  const step = TIMEFRAME_SECONDS[tf];
  return Math.floor(tsSec / step) * step;
}

function aggregateCandles(
  bars: CandleBar[],
  targetTf: Timeframe,
  source: DataSourceId
): CandleBar[] {
  if (bars.length === 0) return [];
  const sorted = [...bars].sort((a, b) => a.time - b.time);
  const out: CandleBar[] = [];

  for (const b of sorted) {
    const bucket = getBucketStartSec(b.time, targetTf);
    const last = out[out.length - 1];
    if (last && last.time === bucket) {
      last.high = Math.max(last.high, b.high);
      last.low = Math.min(last.low, b.low);
      last.close = b.close;
      last.volume += b.volume;
    } else {
      out.push({
        time: bucket,
        open: b.open,
        high: b.high,
        low: b.low,
        close: b.close,
        volume: b.volume,
        source,
      });
    }
  }
  return out;
}

const DEFAULT_TOKENS: Array<{
  symbol: string;
  base: string;
  quote: string;
  name: string;
  seedPrice: number;
}> = [
  { symbol: 'BTC/USDT', base: 'BTC', quote: 'USDT', name: 'Bitcoin', seedPrice: 96420 },
  { symbol: 'ETH/USDT', base: 'ETH', quote: 'USDT', name: 'Ethereum', seedPrice: 3425 },
  { symbol: 'SOL/USDT', base: 'SOL', quote: 'USDT', name: 'Solana', seedPrice: 214.8 },
  { symbol: 'AVAX/USDT', base: 'AVAX', quote: 'USDT', name: 'Avalanche', seedPrice: 41.9 },
  { symbol: 'LINK/USDT', base: 'LINK', quote: 'USDT', name: 'Chainlink', seedPrice: 19.45 },
  { symbol: 'ARB/USDT', base: 'ARB', quote: 'USDT', name: 'Arbitrum', seedPrice: 1.14 },
  { symbol: 'XRP/USDT', base: 'XRP', quote: 'USDT', name: 'Ripple', seedPrice: 1.48 },
  { symbol: 'DOGE/USDT', base: 'DOGE', quote: 'USDT', name: 'Dogecoin', seedPrice: 0.392 },
];

class CentralizedAsyncFeedManager {
  private watchlist = new Map<string, WatchlistToken>();
  private candleStore = new Map<string, CandleBar[]>(); // key: `${symbol}|${tf}|${source}`
  private sources: Record<DataSourceId, SourceStatus> = {
    binance: {
      id: 'binance',
      label: 'Binance',
      endpoint: 'wss://stream.binance.com:9443',
      state: 'syncing',
      latencyMs: 0,
      lastTickAt: 0,
      ticksReceived: 0,
      modeLabel: 'Synchronisation',
    },
    coinbase: {
      id: 'coinbase',
      label: 'Coinbase',
      endpoint: 'wss://ws-feed.exchange.coinbase.com',
      state: 'syncing',
      latencyMs: 0,
      lastTickAt: 0,
      ticksReceived: 0,
      modeLabel: 'Synchronisation',
    },
    kraken: {
      id: 'kraken',
      label: 'Kraken',
      endpoint: 'wss://ws.kraken.com/v2',
      state: 'syncing',
      latencyMs: 0,
      lastTickAt: 0,
      ticksReceived: 0,
      modeLabel: 'Synchronisation',
    },
  };

  private upstreamSockets: Partial<Record<DataSourceId, WebSocket>> = {};
  private clientSubscriptions = new Map<
    WebSocket,
    { symbol: string; timeframe: Timeframe; primarySource: DataSourceId }
  >();
  private fallbackTimer: NodeJS.Timeout | null = null;
  private resyncTimer: NodeJS.Timeout | null = null;

  constructor() {
    const now = Date.now();
    for (const tok of DEFAULT_TOKENS) {
      const open24h = tok.seedPrice * (0.97 + (tok.base.charCodeAt(0) % 5) * 0.01);
      this.watchlist.set(tok.symbol, {
        symbol: tok.symbol,
        base: tok.base,
        quote: tok.quote,
        name: tok.name,
        price: tok.seedPrice,
        open24h,
        change24h: ((tok.seedPrice - open24h) / open24h) * 100,
        high24h: tok.seedPrice * 1.022,
        low24h: tok.seedPrice * 0.976,
        volume24h: tok.seedPrice > 1000 ? 18420 : 492000,
        sourcePrices: {
          binance: tok.seedPrice,
          coinbase: tok.seedPrice * 1.0002,
          kraken: tok.seedPrice * 0.9998,
        },
        lastUpdate: now,
      });
    }
  }

  private storeKey(symbol: string, tf: Timeframe, source: DataSourceId): string {
    return `${symbol}|${tf}|${source}`;
  }

  public start(): void {
    void this.syncAllWatchlistQuotes();
    this.connectBinanceWs();
    this.connectCoinbaseWs();
    this.connectKrakenWs();

    // Synchronized multi-source fallback & heartbeat loop
    this.fallbackTimer = setInterval(() => {
      this.runFallbackSyncTick();
    }, 650);

    // Periodic REST ticker reconciliation across all sources
    this.resyncTimer = setInterval(() => {
      void this.syncAllWatchlistQuotes();
    }, 20000);
  }

  public stop(): void {
    if (this.fallbackTimer) clearInterval(this.fallbackTimer);
    if (this.resyncTimer) clearInterval(this.resyncTimer);
    for (const sock of Object.values(this.upstreamSockets)) {
      try {
        sock?.close();
      } catch {
        // ignore
      }
    }
  }

  public getWatchlist(): WatchlistToken[] {
    return Array.from(this.watchlist.values());
  }

  public getSources(): SourceStatus[] {
    return [this.sources.binance, this.sources.coinbase, this.sources.kraken];
  }

  public ensureToken(symbol: string, name?: string): WatchlistToken {
    const clean = symbol.trim().toUpperCase();
    const existing = this.watchlist.get(clean);
    if (existing) return existing;

    const [base = clean, quote = 'USDT'] = clean.split('/');
    const seedPrice = 25.0;
    const tok: WatchlistToken = {
      symbol: clean,
      base,
      quote,
      name: name?.trim() || base,
      price: seedPrice,
      open24h: seedPrice * 0.99,
      change24h: 1.01,
      high24h: seedPrice * 1.03,
      low24h: seedPrice * 0.97,
      volume24h: 125000,
      sourcePrices: {
        binance: seedPrice,
        coinbase: seedPrice * 1.0002,
        kraken: seedPrice * 0.9998,
      },
      lastUpdate: Date.now(),
    };
    this.watchlist.set(clean, tok);
    void this.syncAllWatchlistQuotes();
    return tok;
  }

  public async getOrFetchSeries(
    symbol: string,
    timeframe: Timeframe,
    source: DataSourceId
  ): Promise<CandleBar[]> {
    const key = this.storeKey(symbol, timeframe, source);
    const minBars = timeframe === '1y' ? 2 : timeframe === '1M' ? 6 : 20;
    const cached = this.candleStore.get(key);
    if (cached && cached.length >= minBars) {
      return cached;
    }

    const fetched = await this.fetchExchangeCandles(symbol, timeframe, source);
    if (fetched.length >= minBars) {
      this.candleStore.set(key, fetched);
      const last = fetched[fetched.length - 1];
      const token = this.watchlist.get(symbol);
      if (token && last) {
        token.sourcePrices[source] = last.close;
        if (source === 'binance' || !token.price) {
          token.price = last.close;
          token.change24h = token.open24h > 0 ? ((token.price - token.open24h) / token.open24h) * 100 : 0;
        }
      }
      return fetched;
    }

    const synthetic = this.buildAlignedSeedCandles(symbol, timeframe, source);
    this.candleStore.set(key, synthetic);
    return synthetic;
  }

  private async fetchExchangeCandles(
    symbol: string,
    timeframe: Timeframe,
    source: DataSourceId
  ): Promise<CandleBar[]> {
    const [base, quote] = symbol.split('/');
    const t0 = Date.now();
    try {
      if (source === 'binance') {
        const pair = `${base}${quote === 'USD' ? 'USDT' : quote}`;
        const url = `https://api.binance.com/api/v3/klines?symbol=${encodeURIComponent(pair)}&interval=${BINANCE_INTERVAL[timeframe]}&limit=180`;
        const resp = await fetch(url, { signal: AbortSignal.timeout(3500) });
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        const raw = (await resp.json()) as unknown[][];
        if (!Array.isArray(raw) || raw.length === 0) throw new Error('Empty klines');
        this.sources.binance.latencyMs = Date.now() - t0;
        this.sources.binance.state = 'live';
        this.sources.binance.modeLabel = 'Flux réel';
        const mapped: CandleBar[] = raw.map((row) => ({
          time: Math.floor(Number(row[0]) / 1000),
          open: Number(row[1]),
          high: Number(row[2]),
          low: Number(row[3]),
          close: Number(row[4]),
          volume: Number(row[5]),
          source: 'binance',
        }));
        return timeframe === '1y' ? aggregateCandles(mapped, '1y', 'binance') : mapped;
      }

      if (source === 'coinbase') {
        const pair = `${base}-USD`;
        const gran = COINBASE_GRANULARITY[timeframe];
        const url = `https://api.exchange.coinbase.com/products/${encodeURIComponent(pair)}/candles?granularity=${gran}`;
        const resp = await fetch(url, {
          headers: { 'User-Agent': 'OpenViewTerminal/1.0' },
          signal: AbortSignal.timeout(3500),
        });
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        const raw = (await resp.json()) as number[][];
        if (!Array.isArray(raw) || raw.length === 0) throw new Error('Empty candles');
        this.sources.coinbase.latencyMs = Date.now() - t0;
        this.sources.coinbase.state = 'live';
        this.sources.coinbase.modeLabel = 'Flux réel';
        const mapped: CandleBar[] = raw
          .slice(0, timeframe === '1w' || timeframe === '1M' || timeframe === '1y' ? 300 : 180)
          .reverse()
          .map((row) => ({
            time: Number(row[0]),
            low: Number(row[1]),
            high: Number(row[2]),
            open: Number(row[3]),
            close: Number(row[4]),
            volume: Number(row[5]),
            source: 'coinbase',
          }));
        if (timeframe === '1w' || timeframe === '1M' || timeframe === '1y') {
          return aggregateCandles(mapped, timeframe, 'coinbase');
        }
        return mapped;
      }

      if (source === 'kraken') {
        const kBase = base === 'BTC' ? 'XBT' : base;
        const pair = `${kBase}USD`;
        const interval = KRAKEN_INTERVAL[timeframe];
        const url = `https://api.kraken.com/0/public/OHLC?pair=${encodeURIComponent(pair)}&interval=${interval}`;
        const resp = await fetch(url, { signal: AbortSignal.timeout(3500) });
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        const json = (await resp.json()) as { error?: string[]; result?: Record<string, unknown[][]> };
        if (json.error && json.error.length > 0) throw new Error(json.error[0]);
        const resultKeys = Object.keys(json.result ?? {}).filter((k) => k !== 'last');
        if (resultKeys.length === 0) throw new Error('No kraken pair');
        const rows = json.result![resultKeys[0]];
        this.sources.kraken.latencyMs = Date.now() - t0;
        this.sources.kraken.state = 'live';
        this.sources.kraken.modeLabel = 'Flux réel';
        const sliced =
          timeframe === '1M' || timeframe === '1y' ? rows : rows.slice(-180);
        const mapped: CandleBar[] = sliced.map((row) => ({
          time: Number(row[0]),
          open: Number(row[1]),
          high: Number(row[2]),
          low: Number(row[3]),
          close: Number(row[4]),
          volume: Number(row[6]),
          source: 'kraken',
        }));
        if (timeframe === '1M' || timeframe === '1y') {
          return aggregateCandles(mapped, timeframe, 'kraken');
        }
        return mapped;
      }
    } catch {
      this.sources[source].state = 'fallback';
      this.sources[source].modeLabel = 'Relais sync';
    }
    return [];
  }

  private buildAlignedSeedCandles(
    symbol: string,
    timeframe: Timeframe,
    source: DataSourceId
  ): CandleBar[] {
    const token = this.watchlist.get(symbol);
    const baseSeed = token?.price ?? 100;
    const step = TIMEFRAME_SECONDS[timeframe];
    const nowSec = Math.floor(Date.now() / 1000);
    const nowAligned = getBucketStartSec(nowSec, timeframe);
    const nowDate = new Date(nowAligned * 1000);
    const count =
      timeframe === '1y' ? 15 : timeframe === '1M' ? 72 : timeframe === '1w' ? 120 : 180;

    // Anchor source offset slightly so multi-source comparison shows realistic spread
    const srcOffset = source === 'binance' ? 0 : source === 'coinbase' ? 0.00035 : -0.00028;
    const bars: CandleBar[] = [];

    // Deterministic walk ending near baseSeed
    let cursorPrice = baseSeed * (timeframe === '1y' ? 0.25 : timeframe === '1M' ? 0.55 : 0.945);
    const symHash = symbol.split('').reduce((acc, ch) => acc + ch.charCodeAt(0), 0);

    for (let i = 0; i < count; i++) {
      const stepsBack = count - 1 - i;
      let time: number;
      if (timeframe === '1y') {
        time = Math.floor(Date.UTC(nowDate.getUTCFullYear() - stepsBack, 0, 1) / 1000);
      } else if (timeframe === '1M') {
        time = Math.floor(
          Date.UTC(nowDate.getUTCFullYear(), nowDate.getUTCMonth() - stepsBack, 1) / 1000
        );
      } else {
        time = nowAligned - stepsBack * step;
      }
      const wave =
        Math.sin((i + symHash) * 0.17) * 0.0042 +
        Math.cos((i * 0.43 + symHash) * 0.23) * 0.0026 +
        0.00032;
      const open = cursorPrice * (1 + srcOffset);
      const close = open * (1 + wave);
      const wickUp = Math.abs(Math.sin(i * 1.3 + symHash)) * open * 0.0032;
      const wickDn = Math.abs(Math.cos(i * 1.1 + symHash)) * open * 0.0032;
      const high = Math.max(open, close) + wickUp;
      const low = Math.min(open, close) - wickDn;
      const volBase = baseSeed > 1000 ? 65 : 1800;
      const volume = volBase * (0.6 + Math.abs(Math.sin(i * 0.7)) * 1.4);

      bars.push({
        time,
        open,
        high,
        low,
        close,
        volume,
        source,
      });
      cursorPrice = close / (1 + srcOffset);
    }

    // Scale final bar cleanly to current token price
    const lastClose = bars[bars.length - 1].close;
    const scale = lastClose > 0 ? (baseSeed * (1 + srcOffset)) / lastClose : 1;
    for (const b of bars) {
      b.open *= scale;
      b.high *= scale;
      b.low *= scale;
      b.close *= scale;
    }

    return bars;
  }

  private async syncAllWatchlistQuotes(): Promise<void> {
    const t0 = Date.now();
    try {
      const resp = await fetch('https://api.binance.com/api/v3/ticker/24hr', {
        signal: AbortSignal.timeout(3500),
      });
      if (resp.ok) {
        const list = (await resp.json()) as Array<{
          symbol: string;
          lastPrice: string;
          openPrice: string;
          priceChangePercent: string;
          highPrice: string;
          lowPrice: string;
          volume: string;
        }>;
        const map = new Map(list.map((item) => [item.symbol, item]));
        for (const tok of this.watchlist.values()) {
          const pair = `${tok.base}${tok.quote}`;
          const hit = map.get(pair);
          if (hit) {
            const lp = Number(hit.lastPrice);
            if (lp > 0) {
              tok.price = lp;
              tok.open24h = Number(hit.openPrice) || tok.open24h;
              tok.change24h = Number(hit.priceChangePercent) || 0;
              tok.high24h = Number(hit.highPrice) || tok.high24h;
              tok.low24h = Number(hit.lowPrice) || tok.low24h;
              tok.volume24h = Number(hit.volume) || tok.volume24h;
              tok.sourcePrices.binance = lp;
              tok.lastUpdate = Date.now();
            }
          }
        }
        this.sources.binance.latencyMs = Date.now() - t0;
        if (this.sources.binance.state !== 'live') {
          this.sources.binance.state = 'live';
          this.sources.binance.modeLabel = 'Flux réel';
        }
      }
    } catch {
      if (this.sources.binance.state === 'syncing') {
        this.sources.binance.state = 'fallback';
        this.sources.binance.modeLabel = 'Relais sync';
      }
    }
  }

  private connectBinanceWs(): void {
    const streams = Array.from(this.watchlist.values())
      .slice(0, 8)
      .map((t) => `${t.base.toLowerCase()}${t.quote.toLowerCase()}@trade`)
      .join('/');

    try {
      const ws = new WebSocket(`wss://stream.binance.com:9443/stream?streams=${streams}`);
      this.upstreamSockets.binance = ws;

      ws.on('open', () => {
        this.sources.binance.state = 'live';
        this.sources.binance.modeLabel = 'Flux réel';
      });

      ws.on('message', (raw) => {
        try {
          const msg = JSON.parse(raw.toString()) as {
            data?: { s?: string; p?: string; q?: string; T?: number };
          };
          const d = msg.data;
          if (!d?.s || !d?.p) return;
          const price = Number(d.p);
          const qty = Number(d.q ?? 0.1);
          const tsMs = d.T ?? Date.now();
          for (const tok of this.watchlist.values()) {
            if (`${tok.base}${tok.quote}` === d.s) {
              this.ingestTick(tok.symbol, 'binance', price, qty, tsMs);
              break;
            }
          }
        } catch {
          // ignore malformed frame
        }
      });

      ws.on('error', () => {
        this.sources.binance.state = 'fallback';
        this.sources.binance.modeLabel = 'Relais sync';
      });

      ws.on('close', () => {
        this.sources.binance.state = 'fallback';
        this.sources.binance.modeLabel = 'Relais sync';
      });
    } catch {
      this.sources.binance.state = 'fallback';
      this.sources.binance.modeLabel = 'Relais sync';
    }
  }

  private connectCoinbaseWs(): void {
    try {
      const ws = new WebSocket('wss://ws-feed.exchange.coinbase.com');
      this.upstreamSockets.coinbase = ws;

      ws.on('open', () => {
        this.sources.coinbase.state = 'live';
        this.sources.coinbase.modeLabel = 'Flux réel';
        const productIds = Array.from(this.watchlist.values())
          .slice(0, 8)
          .map((t) => `${t.base}-USD`);
        ws.send(
          JSON.stringify({
            type: 'subscribe',
            product_ids: productIds,
            channels: ['ticker'],
          })
        );
      });

      ws.on('message', (raw) => {
        try {
          const d = JSON.parse(raw.toString()) as {
            type?: string;
            product_id?: string;
            price?: string;
            last_size?: string;
            time?: string;
          };
          if (d.type !== 'ticker' || !d.product_id || !d.price) return;
          const base = d.product_id.split('-')[0];
          const price = Number(d.price);
          const qty = Number(d.last_size ?? 0.1);
          const tsMs = d.time ? Date.parse(d.time) : Date.now();
          for (const tok of this.watchlist.values()) {
            if (tok.base === base) {
              this.ingestTick(tok.symbol, 'coinbase', price, qty, tsMs);
              break;
            }
          }
        } catch {
          // ignore
        }
      });

      ws.on('error', () => {
        this.sources.coinbase.state = 'fallback';
        this.sources.coinbase.modeLabel = 'Relais sync';
      });

      ws.on('close', () => {
        this.sources.coinbase.state = 'fallback';
        this.sources.coinbase.modeLabel = 'Relais sync';
      });
    } catch {
      this.sources.coinbase.state = 'fallback';
      this.sources.coinbase.modeLabel = 'Relais sync';
    }
  }

  private connectKrakenWs(): void {
    try {
      const ws = new WebSocket('wss://ws.kraken.com/v2');
      this.upstreamSockets.kraken = ws;

      ws.on('open', () => {
        this.sources.kraken.state = 'live';
        this.sources.kraken.modeLabel = 'Flux réel';
        const symbols = Array.from(this.watchlist.values())
          .slice(0, 6)
          .map((t) => `${t.base}/USD`);
        ws.send(
          JSON.stringify({
            method: 'subscribe',
            params: { channel: 'ticker', symbol: symbols },
          })
        );
      });

      ws.on('message', (raw) => {
        try {
          const msg = JSON.parse(raw.toString()) as {
            channel?: string;
            data?: Array<{ symbol?: string; last?: number; volume?: number }>;
          };
          if (msg.channel !== 'ticker' || !Array.isArray(msg.data)) return;
          for (const item of msg.data) {
            if (!item.symbol || !item.last) continue;
            const rawBase = item.symbol.split('/')[0];
            const base = rawBase === 'XBT' ? 'BTC' : rawBase;
            for (const tok of this.watchlist.values()) {
              if (tok.base === base) {
                this.ingestTick(tok.symbol, 'kraken', item.last, 0.25, Date.now());
                break;
              }
            }
          }
        } catch {
          // ignore
        }
      });

      ws.on('error', () => {
        this.sources.kraken.state = 'fallback';
        this.sources.kraken.modeLabel = 'Relais sync';
      });

      ws.on('close', () => {
        this.sources.kraken.state = 'fallback';
        this.sources.kraken.modeLabel = 'Relais sync';
      });
    } catch {
      this.sources.kraken.state = 'fallback';
      this.sources.kraken.modeLabel = 'Relais sync';
    }
  }

  private runFallbackSyncTick(): void {
    const now = Date.now();
    const sourcesToEmulate = (['binance', 'coinbase', 'kraken'] as DataSourceId[]).filter(
      (s) => this.sources[s].state !== 'live' || now - this.sources[s].lastTickAt > 4000
    );

    if (sourcesToEmulate.length === 0) return;

    // Prioritize active subscribed symbols + rotate watchlist items
    const activeSymbols = new Set<string>();
    for (const sub of this.clientSubscriptions.values()) {
      activeSymbols.add(sub.symbol);
    }

    const tokens = this.getWatchlist();
    for (const tok of tokens) {
      const isWatchedChart = activeSymbols.has(tok.symbol);
      if (!isWatchedChart && Math.random() > 0.35) continue;

      const anchor = tok.price;
      for (const src of sourcesToEmulate) {
        const curSrcPrice = tok.sourcePrices[src] ?? anchor;
        // Mean-revert gently toward anchor while adding micro-volatility
        const meanPull = (anchor - curSrcPrice) * 0.25;
        const spreadBias = src === 'coinbase' ? 0.00018 : src === 'kraken' ? -0.00016 : 0;
        const jitter = (Math.random() - 0.495) * anchor * 0.00085;
        const nextPrice = Math.max(0.0001, curSrcPrice + meanPull + jitter + anchor * spreadBias * 0.15);
        this.ingestTick(tok.symbol, src, nextPrice, 0.2 + Math.random() * 2.4, now, true);
      }
    }
  }

  private ingestTick(
    symbol: string,
    source: DataSourceId,
    price: number,
    volume: number,
    tsMs: number,
    isFallback = false
  ): void {
    const tok = this.watchlist.get(symbol);
    if (!tok || !Number.isFinite(price) || price <= 0) return;

    const srcStatus = this.sources[source];
    srcStatus.lastTickAt = tsMs;
    srcStatus.ticksReceived += 1;
    if (!isFallback) {
      srcStatus.state = 'live';
      srcStatus.modeLabel = 'Flux réel';
      srcStatus.latencyMs = Math.max(8, Math.min(240, Math.abs(Date.now() - tsMs) || 18));
    } else if (srcStatus.state === 'syncing') {
      srcStatus.state = 'fallback';
      srcStatus.modeLabel = 'Relais sync';
      srcStatus.latencyMs = 14;
    }

    tok.sourcePrices[source] = price;
    tok.lastUpdate = tsMs;

    // Update primary reference price if source is binance or no live binance tick recently
    if (source === 'binance' || this.sources.binance.state !== 'live') {
      tok.price = price;
      if (price > tok.high24h) tok.high24h = price;
      if (price < tok.low24h) tok.low24h = price;
      tok.volume24h += volume;
      tok.change24h = tok.open24h > 0 ? ((price - tok.open24h) / tok.open24h) * 100 : 0;
    }

    // Update any cached timeframe bars for this (symbol, source) and broadcast to matching clients
    const tsSec = Math.floor(tsMs / 1000);
    for (const [client, sub] of this.clientSubscriptions.entries()) {
      if (client.readyState !== WebSocket.OPEN) continue;
      if (sub.symbol !== symbol) continue;

      const barTime = getBucketStartSec(tsSec, sub.timeframe);
      const key = this.storeKey(symbol, sub.timeframe, source);
      let bars = this.candleStore.get(key);
      if (!bars) {
        bars = this.buildAlignedSeedCandles(symbol, sub.timeframe, source);
        this.candleStore.set(key, bars);
      }

      let updatedBar: CandleBar;
      const last = bars[bars.length - 1];
      if (last && last.time === barTime) {
        last.high = Math.max(last.high, price);
        last.low = Math.min(last.low, price);
        last.close = price;
        last.volume += volume;
        updatedBar = last;
      } else {
        const prevClose = last ? last.close : price;
        updatedBar = {
          time: barTime,
          open: prevClose,
          high: Math.max(prevClose, price),
          low: Math.min(prevClose, price),
          close: price,
          volume,
          source,
        };
        bars.push(updatedBar);
        if (bars.length > 500) bars.shift();
      }

      client.send(
        JSON.stringify({
          type: 'tick',
          symbol,
          timeframe: sub.timeframe,
          source,
          candle: updatedBar,
          watchlistItem: tok,
          sources: this.getSources(),
        })
      );
    }
  }

  public async registerClient(
    ws: WebSocket,
    symbol: string,
    timeframe: Timeframe,
    primarySource: DataSourceId
  ): Promise<void> {
    this.ensureToken(symbol);
    this.clientSubscriptions.set(ws, { symbol, timeframe, primarySource });

    const [primaryCandles, binanceBars, coinbaseBars, krakenBars] = await Promise.all([
      this.getOrFetchSeries(symbol, timeframe, primarySource),
      this.getOrFetchSeries(symbol, timeframe, 'binance'),
      this.getOrFetchSeries(symbol, timeframe, 'coinbase'),
      this.getOrFetchSeries(symbol, timeframe, 'kraken'),
    ]);

    if (ws.readyState !== WebSocket.OPEN) return;

    ws.send(
      JSON.stringify({
        type: 'snapshot',
        symbol,
        timeframe,
        primarySource,
        candles: primaryCandles,
        secondarySeries: {
          binance: binanceBars.map((b) => ({ time: b.time, close: b.close })),
          coinbase: coinbaseBars.map((b) => ({ time: b.time, close: b.close })),
          kraken: krakenBars.map((b) => ({ time: b.time, close: b.close })),
        },
        watchlist: this.getWatchlist(),
        sources: this.getSources(),
      })
    );
  }

  public unregisterClient(ws: WebSocket): void {
    this.clientSubscriptions.delete(ws);
  }
}

async function startServer() {
  const app = express();
  app.use(express.json());

  const server = http.createServer(app);
  const wss = new WebSocketServer({ server, path: '/ws/feeds' });

  const feedManager = new CentralizedAsyncFeedManager();
  feedManager.start();

  // Periodic watchlist broadcast to all connected WS clients
  setInterval(() => {
    const payload = JSON.stringify({
      type: 'watchlist',
      watchlist: feedManager.getWatchlist(),
      sources: feedManager.getSources(),
    });
    for (const client of wss.clients) {
      if (client.readyState === WebSocket.OPEN) {
        client.send(payload);
      }
    }
  }, 1500);

  wss.on('connection', (ws) => {
    void feedManager.registerClient(ws, 'BTC/USDT', '15m', 'binance');

    ws.on('message', (raw) => {
      try {
        const msg = JSON.parse(raw.toString()) as {
          type?: string;
          symbol?: string;
          timeframe?: Timeframe;
          primarySource?: DataSourceId;
        };
        if (msg.type === 'subscribe' && msg.symbol && msg.timeframe && msg.primarySource) {
          void feedManager.registerClient(ws, msg.symbol, msg.timeframe, msg.primarySource);
        }
      } catch {
        // ignore invalid messages
      }
    });

    ws.on('close', () => {
      feedManager.unregisterClient(ws);
    });
  });

  app.get('/api/health', (_req, res) => {
    res.json({ ok: true, sources: feedManager.getSources() });
  });

  app.get('/api/watchlist', (_req, res) => {
    res.json({ watchlist: feedManager.getWatchlist(), sources: feedManager.getSources() });
  });

  const dataDir = path.resolve(__dirname, 'data');
  const drawingsFile = path.join(dataDir, 'drawings.json');

  app.get('/api/drawings', (_req, res) => {
    try {
      if (!fs.existsSync(drawingsFile)) {
        res.json({ drawings: [] });
        return;
      }
      const raw = fs.readFileSync(drawingsFile, 'utf8');
      const parsed = JSON.parse(raw) as unknown;
      res.json({ drawings: Array.isArray(parsed) ? parsed : [] });
    } catch {
      res.json({ drawings: [] });
    }
  });

  app.put('/api/drawings', (req, res) => {
    const body = req.body as { drawings?: unknown };
    if (!Array.isArray(body?.drawings)) {
      res.status(400).json({ error: 'Format invalide' });
      return;
    }
    try {
      if (!fs.existsSync(dataDir)) {
        fs.mkdirSync(dataDir, { recursive: true });
      }
      fs.writeFileSync(drawingsFile, JSON.stringify(body.drawings, null, 2), 'utf8');
      res.json({ ok: true, count: body.drawings.length });
    } catch {
      res.status(500).json({ error: 'Impossible de sauvegarder les dessins' });
    }
  });

  app.post('/api/watchlist', (req, res) => {
    const { symbol, name } = req.body as { symbol?: string; name?: string };
    if (!symbol || typeof symbol !== 'string') {
      res.status(400).json({ error: 'Symbole requis (ex. ADA/USDT)' });
      return;
    }
    const token = feedManager.ensureToken(symbol, name);
    res.json({ token, watchlist: feedManager.getWatchlist() });
  });

  app.get('/api/c-project.zip', async (_req, res) => {
    try {
      const zip = new JSZip();
      const root = zip.folder('OpenViewWin32');
      if (root) {
        for (const f of C_WIN32_PROJECT_FILES) {
          root.file(f.path, f.content);
        }
      }
      const buffer = await zip.generateAsync({ type: 'nodebuffer', compression: 'DEFLATE' });
      res.setHeader('Content-Type', 'application/zip');
      res.setHeader('Content-Disposition', 'attachment; filename="OpenViewWin32_C11_Direct2D.zip"');
      res.send(buffer);
    } catch {
      res.status(500).json({ error: 'Erreur lors de la génération de l’archive ZIP' });
    }
  });

  if (process.env.NODE_ENV !== 'production') {
    const { createServer: createViteServer } = await import('vite');
    const vite = await createViteServer({
      server: { middlewareMode: true },
      appType: 'spa',
    });
    app.use(vite.middlewares);
  } else {
    const distPath = path.resolve(__dirname, 'dist');
    if (fs.existsSync(distPath)) {
      app.use(express.static(distPath));
      app.get('*', (_req, res) => {
        res.sendFile(path.join(distPath, 'index.html'));
      });
    }
  }

  server.listen(PORT, '0.0.0.0', () => {
    console.log(`OpenView server listening on http://0.0.0.0:${PORT}`);
  });
}

void startServer();
