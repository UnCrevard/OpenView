import React, { useEffect, useRef, useState, useCallback } from 'react';
import {
  CandleBar,
  ChartDrawing,
  DataSourceId,
  DrawingLabelAlignH,
  DrawingLabelAlignV,
  DrawingPoint,
  DrawingToolType,
  Timeframe,
} from '../types/trading';
import { ComputedIndicators } from '../utils/indicators';

interface TradingCanvasProps {
  symbol: string;
  entryId?: string | null;
  timeframe: Timeframe;
  primarySource: DataSourceId;
  candles: CandleBar[];
  secondarySeries: Partial<Record<DataSourceId, { time: number; close: number }[]>>;
  overlaySources: DataSourceId[];
  computed: ComputedIndicators;
  activeTool: DrawingToolType;
  drawingColor: string;
  drawings: ChartDrawing[];
  selectedDrawingId: string | null;
  onSelectDrawing: (id: string | null) => void;
  onAddDrawing: (drawing: Omit<ChartDrawing, 'id'>) => void;
  onUpdateDrawing: (drawing: ChartDrawing) => void;
  onEditDrawing: (id: string) => void;
  onResetTool: () => void;
  theme: 'dark' | 'light';
}

const FIB_LEVELS = [0, 0.236, 0.382, 0.5, 0.618, 0.786, 1];
const SOURCE_COLORS: Record<DataSourceId, string> = {
  binance: '#F59E0B',
  coinbase: '#38BDF8',
  kraken: '#A855F7',
};

const TF_SEC: Record<Timeframe, number> = {
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

function formatTimeframeLabel(tf: Timeframe): string {
  if (tf === '1w') return '1S';
  if (tf === '1y') return '1A';
  return tf;
}

function formatPrice(p: number): string {
  if (!Number.isFinite(p)) return '0.00';
  if (p >= 1000) return p.toFixed(2);
  if (p >= 10) return p.toFixed(3);
  if (p >= 1) return p.toFixed(4);
  return p.toFixed(5);
}

function formatAxisTime(tsSec: number, tf: Timeframe): string {
  const d = new Date(tsSec * 1000);
  if (tf === '1y') {
    return d.toLocaleDateString('fr-FR', { year: 'numeric' });
  }
  if (tf === '1M') {
    return d.toLocaleDateString('fr-FR', { month: 'short', year: '2-digit' });
  }
  if (tf === '1w') {
    return d.toLocaleDateString('fr-FR', { day: '2-digit', month: 'short', year: '2-digit' });
  }
  if (tf === '1d') {
    return d.toLocaleDateString('fr-FR', { day: '2-digit', month: 'short' });
  }
  if (tf === '4h' || tf === '1h') {
    return d.toLocaleString('fr-FR', {
      day: '2-digit',
      month: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
    });
  }
  return d.toLocaleTimeString('fr-FR', { hour: '2-digit', minute: '2-digit' });
}

function distToSegment(
  px: number,
  py: number,
  x1: number,
  y1: number,
  x2: number,
  y2: number
): number {
  const dx = x2 - x1;
  const dy = y2 - y1;
  const lenSq = dx * dx + dy * dy;
  if (lenSq === 0) return Math.hypot(px - x1, py - y1);
  let t = ((px - x1) * dx + (py - y1) * dy) / lenSq;
  t = Math.max(0, Math.min(1, t));
  return Math.hypot(px - (x1 + t * dx), py - (y1 + t * dy));
}

export const TradingCanvas: React.FC<TradingCanvasProps> = ({
  symbol,
  entryId,
  timeframe,
  primarySource,
  candles,
  secondarySeries,
  overlaySources,
  computed,
  activeTool,
  drawingColor,
  drawings,
  selectedDrawingId,
  onSelectDrawing,
  onAddDrawing,
  onUpdateDrawing,
  onEditDrawing,
  onResetTool,
  theme,
}) => {
  const containerRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);

  const [size, setSize] = useState({ width: 900, height: 600 });
  const [barWidth, setBarWidth] = useState(10);
  const [panOffsetBars, setPanOffsetBars] = useState(4); // right-margin bars
  const [yScaleFactor, setYScaleFactor] = useState(1);

  const [mousePos, setMousePos] = useState<{ x: number; y: number; inside: boolean }>({
    x: 0,
    y: 0,
    inside: false,
  });

  const [draftPoints, setDraftPoints] = useState<DrawingPoint[]>([]);
  const [textModal, setTextModal] = useState<{
    open: boolean;
    x: number;
    y: number;
    point: DrawingPoint | null;
    value: string;
  }>({ open: false, x: 0, y: 0, point: null, value: '' });

  const dragRef = useRef<{
    mode: 'none' | 'pan' | 'yscale' | 'handle';
    startX: number;
    startY: number;
    startPan: number;
    startYScale: number;
    drawingId?: string;
    pointIdx?: number;
  }>({ mode: 'none', startX: 0, startY: 0, startPan: 0, startYScale: 1 });

  const pendingFitRef = useRef(false);

  // Reset draft points when changing symbol, entry, or tool
  useEffect(() => {
    setDraftPoints([]);
    setTextModal({ open: false, x: 0, y: 0, point: null, value: '' });
  }, [symbol, entryId, activeTool]);

  useEffect(() => {
    pendingFitRef.current = true;
  }, [timeframe, symbol]);

  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const ro = new ResizeObserver((entries) => {
      for (const entry of entries) {
        const cr = entry.contentRect;
        if (cr.width > 10 && cr.height > 10) {
          setSize({ width: Math.floor(cr.width), height: Math.floor(cr.height) });
        }
      }
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  // Compute layout geometry
  const activeIndicators = Object.values(computed.byId);
  const rsiInd = activeIndicators.find((i) => i.config.type === 'RSI');
  const macdInd = activeIndicators.find((i) => i.config.type === 'MACD');

  const priceAxisW = 76;
  const timeAxisH = 26;
  const chartW = Math.max(100, size.width - priceAxisW);
  const totalChartH = Math.max(140, size.height - timeAxisH);

  useEffect(() => {
    if (!pendingFitRef.current || candles.length === 0) return;
    pendingFitRef.current = false;
    setPanOffsetBars(candles.length <= 20 ? 2 : 4);
    setYScaleFactor(1);
    if (candles.length <= 45) {
      setBarWidth(Math.min(48, Math.max(10, Math.floor((chartW * 0.72) / candles.length))));
    } else {
      setBarWidth(10);
    }
  }, [candles, chartW]);

  const subPaneCount = (rsiInd ? 1 : 0) + (macdInd ? 1 : 0);
  const subPaneH = subPaneCount > 0 ? Math.max(82, Math.min(130, totalChartH * 0.19)) : 0;
  const mainTop = 0;
  const mainH = Math.max(120, totalChartH - subPaneCount * subPaneH);
  const rsiTop = mainH;
  const macdTop = rsiInd ? mainH + subPaneH : mainH;

  // Mapping helpers between index/time/price and screen coordinates
  const visibleBarsFloat = chartW / barWidth;
  const rightBarIndex = candles.length - 1 + panOffsetBars;
  const leftBarIndex = rightBarIndex - visibleBarsFloat;

  const indexToX = useCallback(
    (idx: number) => (idx - leftBarIndex) * barWidth + barWidth * 0.5,
    [leftBarIndex, barWidth]
  );

  const xToIndex = useCallback(
    (x: number) => leftBarIndex + (x - barWidth * 0.5) / barWidth,
    [leftBarIndex, barWidth]
  );

  const timeToX = useCallback(
    (timeSec: number): number => {
      if (candles.length === 0) return chartW * 0.5;
      if (candles.length === 1) return indexToX(0);
      const tfSec = TF_SEC[timeframe];
      const firstTime = candles[0].time;
      const lastIdx = candles.length - 1;
      const lastTime = candles[lastIdx].time;
      if (timeSec <= firstTime) {
        return indexToX((timeSec - firstTime) / tfSec);
      }
      if (timeSec >= lastTime) {
        return indexToX(lastIdx + (timeSec - lastTime) / tfSec);
      }
      // Binary search for closest candle interval
      let lo = 0;
      let hi = lastIdx;
      while (lo <= hi) {
        const mid = (lo + hi) >> 1;
        if (candles[mid].time === timeSec) return indexToX(mid);
        if (candles[mid].time < timeSec) lo = mid + 1;
        else hi = mid - 1;
      }
      const i0 = Math.max(0, hi);
      const i1 = Math.min(lastIdx, lo);
      const t0 = candles[i0].time;
      const t1 = candles[i1].time;
      const frac = t1 > t0 ? (timeSec - t0) / (t1 - t0) : 0;
      return indexToX(i0 + frac);
    },
    [candles, timeframe, chartW, indexToX]
  );

  const xToTime = useCallback(
    (x: number): number => {
      if (candles.length === 0) return Math.floor(Date.now() / 1000);
      const tfSec = TF_SEC[timeframe];
      const idxFloat = xToIndex(x);
      const lastIdx = candles.length - 1;
      if (idxFloat <= 0) {
        return Math.round(candles[0].time + idxFloat * tfSec);
      }
      if (idxFloat >= lastIdx) {
        return Math.round(candles[lastIdx].time + (idxFloat - lastIdx) * tfSec);
      }
      const i0 = Math.floor(idxFloat);
      const i1 = Math.min(lastIdx, i0 + 1);
      const frac = idxFloat - i0;
      return Math.round(candles[i0].time + frac * (candles[i1].time - candles[i0].time));
    },
    [candles, timeframe, xToIndex]
  );

  // Determine visible price range
  const startVis = Math.max(0, Math.floor(leftBarIndex) - 1);
  const endVis = Math.min(candles.length - 1, Math.ceil(rightBarIndex) + 1);

  let rawMin = Infinity;
  let rawMax = -Infinity;
  let maxVol = 0;

  for (let i = startVis; i <= endVis; i++) {
    const c = candles[i];
    if (!c) continue;
    if (c.low < rawMin) rawMin = c.low;
    if (c.high > rawMax) rawMax = c.high;
    if (c.volume > maxVol) maxVol = c.volume;
  }

  // Include active main-pane overlay indicator extremes so bands don't clip
  for (const ind of activeIndicators) {
    if (ind.config.pane !== 'main') continue;
    for (let i = startVis; i <= endVis; i++) {
      if (ind.values && ind.values[i] !== null && ind.values[i] !== undefined) {
        const v = ind.values[i] as number;
        if (v < rawMin) rawMin = v;
        if (v > rawMax) rawMax = v;
      }
      if (ind.bollinger) {
        const u = ind.bollinger.upper[i];
        const l = ind.bollinger.lower[i];
        if (u !== null && u !== undefined && u > rawMax) rawMax = u;
        if (l !== null && l !== undefined && l < rawMin) rawMin = l;
      }
    }
  }

  if (!Number.isFinite(rawMin) || !Number.isFinite(rawMax)) {
    rawMin = 0;
    rawMax = 100;
  }
  const midPrice = (rawMin + rawMax) * 0.5;
  const rawSpan = Math.max(rawMax - rawMin, midPrice * 0.002 || 1);
  const scaledSpan = rawSpan * 1.18 * yScaleFactor;
  const minPrice = midPrice - scaledSpan * 0.5;
  const maxPrice = midPrice + scaledSpan * 0.5;

  const priceToY = useCallback(
    (price: number) => mainTop + mainH - ((price - minPrice) / (maxPrice - minPrice)) * mainH,
    [mainTop, mainH, minPrice, maxPrice]
  );

  const yToPrice = useCallback(
    (y: number) => minPrice + ((mainTop + mainH - y) / mainH) * (maxPrice - minPrice),
    [mainTop, mainH, minPrice, maxPrice]
  );

  // Hovered candle index for OHLCV readout
  const hoveredIdxRaw = Math.round(xToIndex(mousePos.x));
  const hoveredIdx =
    mousePos.inside &&
    mousePos.x < chartW &&
    hoveredIdxRaw >= 0 &&
    hoveredIdxRaw < candles.length
      ? hoveredIdxRaw
      : candles.length - 1;
  const activeBar = candles[hoveredIdx];

  // Render canvas loop
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const dpr = window.devicePixelRatio || 1;
    canvas.width = Math.floor(size.width * dpr);
    canvas.height = Math.floor(size.height * dpr);
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    ctx.save();
    ctx.scale(dpr, dpr);

    const isDark = theme === 'dark';
    const bg = isDark ? '#131722' : '#FFFFFF';
    const gridColor = isDark ? 'rgba(42, 46, 57, 0.7)' : 'rgba(226, 232, 240, 0.9)';
    const borderColor = isDark ? '#2A2E39' : '#E2E8F0';
    const textMuted = isDark ? '#787B86' : '#64748B';
    const textMain = isDark ? '#D1D4DC' : '#0F172A';
    const bull = '#089981';
    const bear = '#F23645';

    // Clear canvas
    ctx.fillStyle = bg;
    ctx.fillRect(0, 0, size.width, size.height);

    // Clip main chart area
    ctx.save();
    ctx.beginPath();
    ctx.rect(0, 0, chartW, mainH);
    ctx.clip();

    // Horizontal price grid lines
    const gridSteps = 7;
    ctx.strokeStyle = gridColor;
    ctx.lineWidth = 1;
    for (let g = 1; g < gridSteps; g++) {
      const py = Math.round((mainH / gridSteps) * g) + 0.5;
      ctx.beginPath();
      ctx.moveTo(0, py);
      ctx.lineTo(chartW, py);
      ctx.stroke();
    }

    // Vertical time grid lines
    const approxStepBars = Math.max(4, Math.round(90 / barWidth));
    for (let i = Math.max(0, startVis); i <= endVis; i++) {
      if (i % approxStepBars === 0) {
        const vx = Math.round(indexToX(i)) + 0.5;
        ctx.beginPath();
        ctx.moveTo(vx, 0);
        ctx.lineTo(vx, mainH);
        ctx.stroke();
      }
    }

    // Volume histogram at bottom 16% of main pane
    const volMaxH = mainH * 0.16;
    const bodyW = Math.max(1.5, barWidth * 0.68);
    if (maxVol > 0) {
      for (let i = startVis; i <= endVis; i++) {
        const c = candles[i];
        if (!c) continue;
        const x = indexToX(i);
        const vh = (c.volume / maxVol) * volMaxH;
        ctx.fillStyle =
          c.close >= c.open ? 'rgba(8, 153, 129, 0.20)' : 'rgba(242, 54, 69, 0.20)';
        ctx.fillRect(x - bodyW * 0.5, mainH - vh, bodyW, vh);
      }
    }

    // Bollinger fill & main-pane indicators
    for (const ind of activeIndicators) {
      if (ind.config.pane !== 'main') continue;

      if (ind.bollinger) {
        const { upper, middle, lower } = ind.bollinger;
        ctx.save();
        ctx.fillStyle = `${ind.config.color}18`;
        ctx.beginPath();
        let started = false;
        for (let i = startVis; i <= endVis; i++) {
          const u = upper[i];
          if (u === null || u === undefined) continue;
          const x = indexToX(i);
          const y = priceToY(u);
          if (!started) {
            ctx.moveTo(x, y);
            started = true;
          } else {
            ctx.lineTo(x, y);
          }
        }
        for (let i = endVis; i >= startVis; i--) {
          const l = lower[i];
          if (l === null || l === undefined) continue;
          ctx.lineTo(indexToX(i), priceToY(l));
        }
        if (started) {
          ctx.closePath();
          ctx.fill();
        }
        ctx.restore();

        const drawSeries = (arr: (number | null)[], col: string, dash: number[] = []) => {
          ctx.save();
          ctx.strokeStyle = col;
          ctx.lineWidth = 1.3;
          ctx.setLineDash(dash);
          ctx.beginPath();
          let on = false;
          for (let i = startVis; i <= endVis; i++) {
            const v = arr[i];
            if (v === null || v === undefined) {
              on = false;
              continue;
            }
            const x = indexToX(i);
            const y = priceToY(v);
            if (!on) {
              ctx.moveTo(x, y);
              on = true;
            } else {
              ctx.lineTo(x, y);
            }
          }
          ctx.stroke();
          ctx.restore();
        };

        drawSeries(upper, ind.config.color);
        drawSeries(middle, ind.config.secondaryColor || '#94A3B8', [4, 3]);
        drawSeries(lower, ind.config.color);
      } else if (ind.values) {
        ctx.save();
        ctx.strokeStyle = ind.config.color;
        ctx.lineWidth = 1.6;
        ctx.beginPath();
        let on = false;
        for (let i = startVis; i <= endVis; i++) {
          const v = ind.values[i];
          if (v === null || v === undefined) {
            on = false;
            continue;
          }
          const x = indexToX(i);
          const y = priceToY(v);
          if (!on) {
            ctx.moveTo(x, y);
            on = true;
          } else {
            ctx.lineTo(x, y);
          }
        }
        ctx.stroke();
        ctx.restore();
      }
    }

    // Candlesticks
    for (let i = startVis; i <= endVis; i++) {
      const c = candles[i];
      if (!c) continue;
      const x = indexToX(i);
      const isBull = c.close >= c.open;
      const col = isBull ? bull : bear;

      const yH = priceToY(c.high);
      const yL = priceToY(c.low);
      const yO = priceToY(c.open);
      const yC = priceToY(c.close);

      ctx.strokeStyle = col;
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(Math.round(x) + 0.5, yH);
      ctx.lineTo(Math.round(x) + 0.5, yL);
      ctx.stroke();

      const top = Math.min(yO, yC);
      const height = Math.max(1.5, Math.abs(yO - yC));
      ctx.fillStyle = col;
      ctx.fillRect(x - bodyW * 0.5, top, bodyW, height);
    }

    // Secondary Multi-Source Sync Curves
    for (const src of overlaySources) {
      if (src === primarySource) continue;
      const pts = secondarySeries[src];
      if (!pts || pts.length < 2) continue;
      ctx.save();
      ctx.strokeStyle = SOURCE_COLORS[src];
      ctx.lineWidth = 1.5;
      ctx.setLineDash([5, 3]);
      ctx.beginPath();
      let started = false;
      for (const pt of pts) {
        const x = timeToX(pt.time);
        if (x < -40 || x > chartW + 40) continue;
        const y = priceToY(pt.close);
        if (!started) {
          ctx.moveTo(x, y);
          started = true;
        } else {
          ctx.lineTo(x, y);
        }
      }
      ctx.stroke();
      ctx.restore();
    }

    // Last price horizontal dashed line
    const latest = candles[candles.length - 1];
    if (latest) {
      const ly = Math.round(priceToY(latest.close)) + 0.5;
      const isUp = latest.close >= latest.open;
      ctx.save();
      ctx.strokeStyle = isUp ? bull : bear;
      ctx.lineWidth = 1;
      ctx.setLineDash([3, 3]);
      ctx.beginPath();
      ctx.moveTo(0, ly);
      ctx.lineTo(chartW, ly);
      ctx.stroke();
      ctx.restore();
    }

    // Vector Drawings (persisted + draft preview)
    const renderDrawing = (d: Omit<ChartDrawing, 'id'> & { id?: string }, isSelected: boolean) => {
      const ptsScreen = d.points.map((p) => ({
        x: timeToX(p.time),
        y: priceToY(p.price),
        price: p.price,
      }));
      if (ptsScreen.length === 0) return;

      ctx.save();
      ctx.strokeStyle = d.color;
      ctx.fillStyle = d.color;
      ctx.lineWidth = isSelected ? d.lineWidth + 1 : d.lineWidth;
      const dashPattern =
        d.lineStyle === 'dashed' ? [7, 5] : d.lineStyle === 'dotted' ? [2, 4] : [];
      ctx.setLineDash(dashPattern);

      const alignH: DrawingLabelAlignH =
        d.labelAlignH ?? (d.tool === 'trendline' || d.tool === 'channel' ? 'center' : 'left');
      const alignV: DrawingLabelAlignV = d.labelAlignV ?? 'top';

      const drawAlignedLabel = (
        label: string,
        xLeft: number,
        xRight: number,
        yTop: number,
        yBot: number
      ) => {
        ctx.save();
        ctx.setLineDash([]);
        ctx.font = '600 11px "DM Sans", sans-serif';
        const metrics = ctx.measureText(label);
        const w = metrics.width;
        const h = 16;

        let tx = xLeft + 6;
        if (alignH === 'center') {
          tx = (xLeft + xRight) * 0.5;
          ctx.textAlign = 'center';
        } else if (alignH === 'right') {
          tx = xRight - 6;
          ctx.textAlign = 'right';
        } else {
          ctx.textAlign = 'left';
        }

        let ty = yTop - 6;
        ctx.textBaseline = 'alphabetic';
        if (alignV === 'middle') {
          ty = (yTop + yBot) * 0.5;
          ctx.textBaseline = 'middle';
        } else if (alignV === 'bottom') {
          ty = yBot + 14;
        }

        if (alignV === 'middle') {
          const padX = 5;
          const rx =
            alignH === 'center'
              ? tx - w * 0.5 - padX
              : alignH === 'right'
                ? tx - w - padX
                : tx - padX;
          ctx.fillStyle = isDark ? 'rgba(19, 23, 34, 0.88)' : 'rgba(255, 255, 255, 0.90)';
          ctx.fillRect(rx, ty - h * 0.5, w + padX * 2, h);
        }

        ctx.fillStyle = d.color;
        ctx.fillText(label, tx, ty);
        ctx.restore();
      };

      if (d.tool === 'hline') {
        const y = Math.round(ptsScreen[0].y) + 0.5;
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(chartW, y);
        ctx.stroke();
        if (d.text) {
          drawAlignedLabel(d.text, 4, chartW - 4, y, y);
        }
      } else if (d.tool === 'trendline' && ptsScreen.length >= 2) {
        ctx.beginPath();
        ctx.moveTo(ptsScreen[0].x, ptsScreen[0].y);
        ctx.lineTo(ptsScreen[1].x, ptsScreen[1].y);
        ctx.stroke();
        if (d.text) {
          const leftFirst = ptsScreen[0].x <= ptsScreen[1].x;
          const lp = leftFirst ? ptsScreen[0] : ptsScreen[1];
          const rp = leftFirst ? ptsScreen[1] : ptsScreen[0];
          const ax =
            alignH === 'left'
              ? lp.x
              : alignH === 'right'
                ? rp.x
                : (lp.x + rp.x) * 0.5;
          const ay =
            alignH === 'left'
              ? lp.y
              : alignH === 'right'
                ? rp.y
                : (lp.y + rp.y) * 0.5;
          drawAlignedLabel(d.text, ax, ax, ay, ay);
        }
      } else if (d.tool === 'rect' && ptsScreen.length >= 2) {
        const rx = Math.min(ptsScreen[0].x, ptsScreen[1].x);
        const ry = Math.min(ptsScreen[0].y, ptsScreen[1].y);
        const rw = Math.abs(ptsScreen[1].x - ptsScreen[0].x);
        const rh = Math.abs(ptsScreen[1].y - ptsScreen[0].y);
        ctx.fillStyle = `${d.color}24`;
        ctx.fillRect(rx, ry, rw, rh);
        ctx.strokeRect(rx, ry, rw, rh);
        if (d.text) {
          drawAlignedLabel(d.text, rx, rx + rw, ry, ry + rh);
        }
      } else if (d.tool === 'fib' && ptsScreen.length >= 2) {
        const x1 = Math.min(ptsScreen[0].x, ptsScreen[1].x);
        const x2 = Math.max(ptsScreen[0].x, ptsScreen[1].x);
        const xEnd = Math.max(x2, x1 + 90);
        const pStart = ptsScreen[0].price;
        const pEnd = ptsScreen[1].price;
        const diff = pEnd - pStart;

        for (let f = 0; f < FIB_LEVELS.length; f++) {
          const lvl = FIB_LEVELS[f];
          const lvlPrice = pEnd - diff * lvl;
          const y = priceToY(lvlPrice);
          ctx.strokeStyle = d.color;
          ctx.lineWidth = lvl === 0 || lvl === 0.5 || lvl === 0.618 || lvl === 1 ? 1.4 : 1;
          ctx.setLineDash(
            d.lineStyle
              ? dashPattern
              : lvl === 0 || lvl === 1
                ? []
                : [4, 3]
          );
          ctx.beginPath();
          ctx.moveTo(x1, y);
          ctx.lineTo(xEnd, y);
          ctx.stroke();

          ctx.fillStyle = d.color;
          ctx.font = '11px "JetBrains Mono", monospace';
          ctx.fillText(
            `${(lvl * 100).toFixed(1)}% (${formatPrice(lvlPrice)})`,
            x1 + 6,
            y - 4
          );
        }
        ctx.setLineDash([]);
        if (d.text) {
          const fyTop = Math.min(ptsScreen[0].y, ptsScreen[1].y);
          const fyBot = Math.max(ptsScreen[0].y, ptsScreen[1].y);
          drawAlignedLabel(d.text, x1, xEnd, fyTop, fyBot);
        }
      } else if (d.tool === 'channel' && ptsScreen.length >= 2) {
        ctx.beginPath();
        ctx.moveTo(ptsScreen[0].x, ptsScreen[0].y);
        ctx.lineTo(ptsScreen[1].x, ptsScreen[1].y);
        ctx.stroke();

        let chDy = 0;
        if (ptsScreen.length >= 3) {
          chDy = ptsScreen[2].y - ptsScreen[1].y;
          ctx.beginPath();
          ctx.moveTo(ptsScreen[0].x, ptsScreen[0].y + chDy);
          ctx.lineTo(ptsScreen[1].x, ptsScreen[1].y + chDy);
          ctx.stroke();

          ctx.fillStyle = `${d.color}1C`;
          ctx.beginPath();
          ctx.moveTo(ptsScreen[0].x, ptsScreen[0].y);
          ctx.lineTo(ptsScreen[1].x, ptsScreen[1].y);
          ctx.lineTo(ptsScreen[1].x, ptsScreen[1].y + chDy);
          ctx.lineTo(ptsScreen[0].x, ptsScreen[0].y + chDy);
          ctx.closePath();
          ctx.fill();
        }
        if (d.text) {
          const leftFirst = ptsScreen[0].x <= ptsScreen[1].x;
          const lp = leftFirst ? ptsScreen[0] : ptsScreen[1];
          const rp = leftFirst ? ptsScreen[1] : ptsScreen[0];
          const ax =
            alignH === 'left'
              ? lp.x
              : alignH === 'right'
                ? rp.x
                : (lp.x + rp.x) * 0.5;
          const ay =
            alignH === 'left'
              ? lp.y
              : alignH === 'right'
                ? rp.y
                : (lp.y + rp.y) * 0.5;
          drawAlignedLabel(d.text, ax, ax, Math.min(ay, ay + chDy), Math.max(ay, ay + chDy));
        }
      } else if (d.tool === 'text') {
        const label = d.text || 'Note';
        ctx.font = '600 12px "DM Sans", sans-serif';
        const metrics = ctx.measureText(label);
        const padX = 7;
        const bw = metrics.width + padX * 2;
        const bh = 22;
        const bx =
          alignH === 'center'
            ? ptsScreen[0].x - bw * 0.5
            : alignH === 'right'
              ? ptsScreen[0].x - bw
              : ptsScreen[0].x;
        const by =
          alignV === 'middle'
            ? ptsScreen[0].y - bh * 0.5
            : alignV === 'bottom'
              ? ptsScreen[0].y + 4
              : ptsScreen[0].y - bh - 4;
        ctx.fillStyle = isDark ? 'rgba(30, 34, 45, 0.92)' : 'rgba(248, 250, 252, 0.95)';
        ctx.strokeStyle = d.color;
        ctx.lineWidth = 1;
        ctx.fillRect(bx, by, bw, bh);
        ctx.strokeRect(bx, by, bw, bh);
        ctx.fillStyle = isDark ? '#F8FAFC' : '#0F172A';
        ctx.fillText(label, bx + padX, by + 15);
      }

      if (isSelected) {
        for (const pt of ptsScreen) {
          ctx.fillStyle = '#FFFFFF';
          ctx.strokeStyle = '#2962FF';
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.arc(pt.x, pt.y, 4.5, 0, Math.PI * 2);
          ctx.fill();
          ctx.stroke();
        }
      }

      ctx.restore();
    };

    const symbolDrawings = drawings.filter((d) => d.symbol === symbol);
    for (const d of symbolDrawings) {
      renderDrawing(d, d.id === selectedDrawingId);
    }

    if (activeTool !== 'cursor' && draftPoints.length > 0 && mousePos.inside) {
      const hoverPt: DrawingPoint = {
        time: xToTime(mousePos.x),
        price: yToPrice(mousePos.y),
      };
      renderDrawing(
        {
          symbol,
          tool: activeTool,
          points: [...draftPoints, hoverPt],
          color: drawingColor,
          lineWidth: 2,
        },
        true
      );
    }

    ctx.restore(); // end main pane clip

    // Right price axis
    ctx.fillStyle = isDark ? '#131722' : '#FFFFFF';
    ctx.fillRect(chartW, 0, priceAxisW, totalChartH);
    ctx.strokeStyle = borderColor;
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(chartW + 0.5, 0);
    ctx.lineTo(chartW + 0.5, size.height);
    ctx.stroke();

    ctx.fillStyle = textMuted;
    ctx.font = '11px "JetBrains Mono", monospace';
    ctx.textAlign = 'left';
    for (let g = 1; g < gridSteps; g++) {
      const py = (mainH / gridSteps) * g;
      const pVal = yToPrice(py);
      ctx.fillText(formatPrice(pVal), chartW + 8, py + 4);
    }

    // Current price pill on Y-axis (real price marker)
    if (latest) {
      const ly = Math.max(10, Math.min(mainH - 12, priceToY(latest.close)));
      const tagCol = latest.close >= latest.open ? bull : bear;
      ctx.fillStyle = tagCol;
      ctx.fillRect(chartW + 1, ly - 10, priceAxisW - 2, 20);
      ctx.fillStyle = '#FFFFFF';
      ctx.font = '600 11px "JetBrains Mono", monospace';
      ctx.fillText(formatPrice(latest.close), chartW + 7, ly + 4);
    }

    // Hline price tags on Y-axis
    for (const d of symbolDrawings) {
      if (d.tool === 'hline' && d.points[0]) {
        const y = priceToY(d.points[0].price);
        if (y >= 8 && y <= mainH - 8) {
          ctx.fillStyle = d.color;
          ctx.fillRect(chartW + 1, y - 9, priceAxisW - 2, 18);
          ctx.fillStyle = '#FFFFFF';
          ctx.font = '600 10px "JetBrains Mono", monospace';
          ctx.fillText(formatPrice(d.points[0].price), chartW + 7, y + 3.5);
        }
      }
    }

    // Sub-pane: RSI
    if (rsiInd && rsiInd.values) {
      ctx.save();
      ctx.strokeStyle = borderColor;
      ctx.beginPath();
      ctx.moveTo(0, rsiTop + 0.5);
      ctx.lineTo(size.width, rsiTop + 0.5);
      ctx.stroke();

      const pTop = rsiTop + 14;
      const pBot = rsiTop + subPaneH - 8;
      const rsiToY = (v: number) => pBot - (v / 100) * (pBot - pTop);

      // 70 / 30 background zone
      const y70 = rsiToY(70);
      const y30 = rsiToY(30);
      ctx.fillStyle = 'rgba(124, 58, 237, 0.07)';
      ctx.fillRect(0, y70, chartW, y30 - y70);

      ctx.strokeStyle = isDark ? 'rgba(148,163,184,0.28)' : 'rgba(100,116,139,0.28)';
      ctx.setLineDash([4, 3]);
      ctx.beginPath();
      ctx.moveTo(0, y70);
      ctx.lineTo(chartW, y70);
      ctx.moveTo(0, y30);
      ctx.lineTo(chartW, y30);
      ctx.stroke();
      ctx.setLineDash([]);

      // RSI curve
      ctx.strokeStyle = rsiInd.config.color;
      ctx.lineWidth = 1.5;
      ctx.beginPath();
      let on = false;
      for (let i = startVis; i <= endVis; i++) {
        const v = rsiInd.values[i];
        if (v === null || v === undefined) {
          on = false;
          continue;
        }
        const x = indexToX(i);
        const y = rsiToY(v);
        if (!on) {
          ctx.moveTo(x, y);
          on = true;
        } else {
          ctx.lineTo(x, y);
        }
      }
      ctx.stroke();

      // Label & axis values
      const curRsi = rsiInd.values[hoveredIdx];
      ctx.fillStyle = textMuted;
      ctx.font = '11px "JetBrains Mono", monospace';
      ctx.fillText(
        `${rsiInd.config.label}: ${curRsi !== null && curRsi !== undefined ? curRsi.toFixed(2) : '—'}`,
        10,
        rsiTop + 13
      );
      ctx.fillText('70.0', chartW + 8, y70 + 4);
      ctx.fillText('30.0', chartW + 8, y30 + 4);
      ctx.restore();
    }

    // Sub-pane: MACD
    if (macdInd && macdInd.macd) {
      ctx.save();
      ctx.strokeStyle = borderColor;
      ctx.beginPath();
      ctx.moveTo(0, macdTop + 0.5);
      ctx.lineTo(size.width, macdTop + 0.5);
      ctx.stroke();

      const { macd, signal, histogram } = macdInd.macd;
      let mMin = 0;
      let mMax = 0;
      for (let i = startVis; i <= endVis; i++) {
        const vals = [macd[i], signal[i], histogram[i]];
        for (const v of vals) {
          if (v !== null && v !== undefined) {
            if (v < mMin) mMin = v;
            if (v > mMax) mMax = v;
          }
        }
      }
      const mAbs = Math.max(Math.abs(mMin), Math.abs(mMax), 1e-6);
      const pTop = macdTop + 16;
      const pBot = macdTop + subPaneH - 8;
      const pMid = (pTop + pBot) * 0.5;
      const macdToY = (v: number) => pMid - (v / mAbs) * ((pBot - pTop) * 0.46);

      // Zero line
      ctx.strokeStyle = isDark ? 'rgba(148,163,184,0.25)' : 'rgba(100,116,139,0.25)';
      ctx.beginPath();
      ctx.moveTo(0, pMid);
      ctx.lineTo(chartW, pMid);
      ctx.stroke();

      // Histogram
      for (let i = startVis; i <= endVis; i++) {
        const h = histogram[i];
        if (h === null || h === undefined) continue;
        const x = indexToX(i);
        const y = macdToY(h);
        ctx.fillStyle = h >= 0 ? 'rgba(8, 153, 129, 0.55)' : 'rgba(242, 54, 69, 0.55)';
        ctx.fillRect(x - bodyW * 0.4, Math.min(pMid, y), bodyW * 0.8, Math.max(1, Math.abs(y - pMid)));
      }

      const drawMacdLine = (arr: (number | null)[], col: string) => {
        ctx.strokeStyle = col;
        ctx.lineWidth = 1.4;
        ctx.beginPath();
        let on = false;
        for (let i = startVis; i <= endVis; i++) {
          const v = arr[i];
          if (v === null || v === undefined) {
            on = false;
            continue;
          }
          const x = indexToX(i);
          const y = macdToY(v);
          if (!on) {
            ctx.moveTo(x, y);
            on = true;
          } else {
            ctx.lineTo(x, y);
          }
        }
        ctx.stroke();
      };

      drawMacdLine(macd, macdInd.config.color);
      drawMacdLine(signal, macdInd.config.secondaryColor || '#F97316');

      const curM = macd[hoveredIdx];
      const curS = signal[hoveredIdx];
      ctx.fillStyle = textMuted;
      ctx.font = '11px "JetBrains Mono", monospace';
      ctx.fillText(
        `${macdInd.config.label}: ${curM !== null && curM !== undefined ? curM.toFixed(2) : '—'} / Sig ${curS !== null && curS !== undefined ? curS.toFixed(2) : '—'}`,
        10,
        macdTop + 13
      );
      ctx.restore();
    }

    // Bottom Time Axis
    ctx.fillStyle = isDark ? '#131722' : '#FFFFFF';
    ctx.fillRect(0, totalChartH, size.width, timeAxisH);
    ctx.strokeStyle = borderColor;
    ctx.beginPath();
    ctx.moveTo(0, totalChartH + 0.5);
    ctx.lineTo(size.width, totalChartH + 0.5);
    ctx.stroke();

    ctx.fillStyle = textMuted;
    ctx.font = '11px "JetBrains Mono", monospace';
    ctx.textAlign = 'center';
    for (let i = Math.max(0, startVis); i <= endVis; i++) {
      if (i % approxStepBars === 0 && candles[i]) {
        const vx = indexToX(i);
        if (vx > 32 && vx < chartW - 32) {
          ctx.fillText(formatAxisTime(candles[i].time, timeframe), vx, totalChartH + 17);
        }
      }
    }

    // Crosshair lines + coordinate pills
    if (mousePos.inside && mousePos.x < chartW && mousePos.y < totalChartH) {
      ctx.save();
      ctx.strokeStyle = isDark ? 'rgba(209, 212, 220, 0.35)' : 'rgba(15, 23, 42, 0.30)';
      ctx.lineWidth = 1;
      ctx.setLineDash([4, 4]);
      ctx.beginPath();
      ctx.moveTo(0, Math.round(mousePos.y) + 0.5);
      ctx.lineTo(chartW, Math.round(mousePos.y) + 0.5);
      ctx.moveTo(Math.round(mousePos.x) + 0.5, 0);
      ctx.lineTo(Math.round(mousePos.x) + 0.5, totalChartH);
      ctx.stroke();
      ctx.restore();

      if (mousePos.y <= mainH) {
        const hoverPrice = yToPrice(mousePos.y);
        ctx.fillStyle = isDark ? '#2A2E39' : '#1E293B';
        ctx.fillRect(chartW + 1, mousePos.y - 10, priceAxisW - 2, 20);
        ctx.fillStyle = '#FFFFFF';
        ctx.font = '11px "JetBrains Mono", monospace';
        ctx.textAlign = 'left';
        ctx.fillText(formatPrice(hoverPrice), chartW + 7, mousePos.y + 4);
      }

      const hoverTime = xToTime(mousePos.x);
      const timeText = formatAxisTime(hoverTime, timeframe);
      const tw = ctx.measureText(timeText).width + 14;
      ctx.fillStyle = isDark ? '#2A2E39' : '#1E293B';
      ctx.fillRect(mousePos.x - tw * 0.5, totalChartH + 2, tw, 20);
      ctx.fillStyle = '#FFFFFF';
      ctx.font = '11px "JetBrains Mono", monospace';
      ctx.textAlign = 'center';
      ctx.fillText(timeText, mousePos.x, totalChartH + 16);
    }

    ctx.restore();
    void textMain;
  }, [
    size,
    theme,
    candles,
    barWidth,
    panOffsetBars,
    yScaleFactor,
    mousePos,
    drawings,
    selectedDrawingId,
    draftPoints,
    activeTool,
    drawingColor,
    computed,
    overlaySources,
    primarySource,
    secondarySeries,
    symbol,
    timeframe,
    chartW,
    mainH,
    totalChartH,
    rsiTop,
    macdTop,
    subPaneH,
    startVis,
    endVis,
    maxVol,
    hoveredIdx,
    activeIndicators,
    rsiInd,
    macdInd,
    indexToX,
    priceToY,
    yToPrice,
    timeToX,
    xToTime,
  ]);

  // Hit-testing drawings in cursor mode
  const hitTestDrawing = (
    x: number,
    y: number
  ): { drawingId: string; pointIdx?: number } | null => {
    const symbolDrawings = drawings.filter((d) => d.symbol === symbol);
    for (let i = symbolDrawings.length - 1; i >= 0; i--) {
      const d = symbolDrawings[i];
      const pts = d.points.map((p) => ({ x: timeToX(p.time), y: priceToY(p.price) }));
      for (let pIdx = 0; pIdx < pts.length; pIdx++) {
        if (Math.hypot(x - pts[pIdx].x, y - pts[pIdx].y) <= 9) {
          return { drawingId: d.id, pointIdx: pIdx };
        }
      }
      if (d.tool === 'hline' && pts[0] && Math.abs(y - pts[0].y) <= 7) {
        return { drawingId: d.id, pointIdx: 0 };
      }
      if (d.tool === 'trendline' && pts.length >= 2 && distToSegment(x, y, pts[0].x, pts[0].y, pts[1].x, pts[1].y) <= 7) {
        return { drawingId: d.id };
      }
      if (d.tool === 'fib' && pts.length >= 2) {
        const x1 = Math.min(pts[0].x, pts[1].x);
        const x2 = Math.max(pts[0].x, pts[1].x);
        const lineEnd = Math.max(x2, x1 + 90);
        const pStart = d.points[0].price;
        const pEnd = d.points[1].price;
        const diff = pEnd - pStart;
        for (const lvl of FIB_LEVELS) {
          const ly = priceToY(pEnd - diff * lvl);
          if (x >= x1 - 4 && x <= lineEnd + 4 && Math.abs(y - ly) <= 6) {
            return { drawingId: d.id };
          }
        }
      }
      if (d.tool === 'channel' && pts.length >= 2) {
        if (distToSegment(x, y, pts[0].x, pts[0].y, pts[1].x, pts[1].y) <= 7) {
          return { drawingId: d.id };
        }
        if (pts.length >= 3) {
          const dy = pts[2].y - pts[1].y;
          if (distToSegment(x, y, pts[0].x, pts[0].y + dy, pts[1].x, pts[1].y + dy) <= 7) {
            return { drawingId: d.id };
          }
        }
      }
      if (d.tool === 'rect' && pts.length >= 2) {
        const minX = Math.min(pts[0].x, pts[1].x);
        const maxX = Math.max(pts[0].x, pts[1].x);
        const minY = Math.min(pts[0].y, pts[1].y);
        const maxY = Math.max(pts[0].y, pts[1].y);
        if (x >= minX - 4 && x <= maxX + 4 && y >= minY - 4 && y <= maxY + 4) {
          return { drawingId: d.id };
        }
      }
      if (d.tool === 'text' && pts[0]) {
        const ah = d.labelAlignH ?? 'left';
        const av = d.labelAlignV ?? 'top';
        const bw = 140;
        const bh = 24;
        const bx = ah === 'center' ? pts[0].x - bw * 0.5 : ah === 'right' ? pts[0].x - bw : pts[0].x;
        const by = av === 'middle' ? pts[0].y - bh * 0.5 : av === 'bottom' ? pts[0].y + 2 : pts[0].y - bh - 2;
        if (x >= bx - 4 && x <= bx + bw + 4 && y >= by - 4 && y <= by + bh + 4) {
          return { drawingId: d.id, pointIdx: 0 };
        }
      }
    }
    return null;
  };

  const handleMouseDown = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    if (x >= chartW) {
      dragRef.current = {
        mode: 'yscale',
        startX: x,
        startY: y,
        startPan: panOffsetBars,
        startYScale: yScaleFactor,
      };
      return;
    }

    if (activeTool !== 'cursor' && y <= mainH) {
      const pt: DrawingPoint = { time: xToTime(x), price: yToPrice(y) };

      if (activeTool === 'hline') {
        onAddDrawing({
          symbol,
          tool: 'hline',
          points: [pt],
          color: drawingColor,
          lineWidth: 1.8,
        });
        onResetTool();
        return;
      }

      if (activeTool === 'text') {
        setTextModal({ open: true, x, y, point: pt, value: '' });
        return;
      }

      const requiredPts = activeTool === 'channel' ? 3 : 2;
      const nextDraft = [...draftPoints, pt];
      if (nextDraft.length >= requiredPts) {
        onAddDrawing({
          symbol,
          tool: activeTool,
          points: nextDraft,
          color: drawingColor,
          lineWidth: 1.8,
        });
        setDraftPoints([]);
        onResetTool();
      } else {
        setDraftPoints(nextDraft);
      }
      return;
    }

    // Cursor mode: select handle or pan
    const hit = hitTestDrawing(x, y);
    if (hit) {
      onSelectDrawing(hit.drawingId);
      const hitDrawing = drawings.find((dr) => dr.id === hit.drawingId);
      if (hit.pointIdx !== undefined && !hitDrawing?.locked) {
        dragRef.current = {
          mode: 'handle',
          startX: x,
          startY: y,
          startPan: panOffsetBars,
          startYScale: yScaleFactor,
          drawingId: hit.drawingId,
          pointIdx: hit.pointIdx,
        };
        return;
      }
    } else {
      onSelectDrawing(null);
    }

    dragRef.current = {
      mode: 'pan',
      startX: x,
      startY: y,
      startPan: panOffsetBars,
      startYScale: yScaleFactor,
    };
  };

  const handleMouseMove = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;
    setMousePos({ x, y, inside: true });

    const d = dragRef.current;
    if (d.mode === 'pan') {
      const dx = x - d.startX;
      setPanOffsetBars(
        Math.max(-Math.max(0, candles.length - 3), Math.min(80, d.startPan + dx / barWidth))
      );
    } else if (d.mode === 'yscale') {
      const dy = y - d.startY;
      setYScaleFactor(Math.max(0.25, Math.min(4, d.startYScale * Math.exp(dy * 0.006))));
    } else if (d.mode === 'handle' && d.drawingId && d.pointIdx !== undefined) {
      const target = drawings.find((dr) => dr.id === d.drawingId);
      if (target && !target.locked) {
        const nextPts = [...target.points];
        nextPts[d.pointIdx] = {
          time: xToTime(x),
          price: yToPrice(Math.max(4, Math.min(mainH - 4, y))),
        };
        onUpdateDrawing({ ...target, points: nextPts });
      }
    }
  };

  const handleMouseUp = () => {
    dragRef.current.mode = 'none';
  };

  const handleWheel = (e: React.WheelEvent<HTMLCanvasElement>) => {
    e.preventDefault();
    const factor = e.deltaY < 0 ? 1.12 : 0.89;
    setBarWidth((prev) => Math.max(3.5, Math.min(60, prev * factor)));
  };

  const handleDoubleClick = (e: React.MouseEvent<HTMLCanvasElement>) => {
    const rect = e.currentTarget.getBoundingClientRect();
    const x = e.clientX - rect.left;
    const y = e.clientY - rect.top;

    if (x < chartW && y <= mainH) {
      const hit = hitTestDrawing(x, y);
      if (hit) {
        onSelectDrawing(hit.drawingId);
        onEditDrawing(hit.drawingId);
        return;
      }
    }

    setYScaleFactor(1);
    setPanOffsetBars(candles.length <= 20 ? 2 : 4);
    if (candles.length > 0 && candles.length <= 45) {
      setBarWidth(Math.min(48, Math.max(10, Math.floor((chartW * 0.72) / candles.length))));
    } else {
      setBarWidth(10);
    }
  };

  const commitTextAnnotation = (e: React.FormEvent) => {
    e.preventDefault();
    if (textModal.point && textModal.value.trim()) {
      onAddDrawing({
        symbol,
        tool: 'text',
        points: [textModal.point],
        color: drawingColor,
        lineWidth: 1.5,
        text: textModal.value.trim(),
      });
    }
    setTextModal({ open: false, x: 0, y: 0, point: null, value: '' });
    onResetTool();
  };

  return (
    <div ref={containerRef} className="relative flex-1 min-w-0 h-full overflow-hidden">
      <canvas
        ref={canvasRef}
        onMouseDown={handleMouseDown}
        onMouseMove={handleMouseMove}
        onMouseUp={handleMouseUp}
        onMouseLeave={() => {
          dragRef.current.mode = 'none';
          setMousePos((p) => ({ ...p, inside: false }));
        }}
        onWheel={handleWheel}
        onDoubleClick={handleDoubleClick}
        className={`block w-full h-full ${
          activeTool !== 'cursor' ? 'cursor-crosshair' : 'cursor-default'
        }`}
      />

      {/* Top-Left OHLCV + Active Indicator Readout */}
      <div className="pointer-events-none absolute top-2 left-3 right-24 flex flex-col gap-1 text-xs">
        <div className="flex flex-wrap items-center gap-2 text-slate-800 dark:text-[#D1D4DC]">
          <span className="font-display font-bold text-slate-900 dark:text-white">
            {symbol}
          </span>
          <span className="text-slate-400 dark:text-[#787B86]">·</span>
          <span className="text-slate-600 dark:text-[#787B86]">{formatTimeframeLabel(timeframe)}</span>
          <span className="text-slate-400 dark:text-[#787B86]">·</span>
          <span className="text-slate-600 dark:text-[#787B86] capitalize">{primarySource}</span>

          {activeBar && (
            <div className="flex flex-wrap items-center gap-2 font-mono-tabular text-[11px] ml-1">
              <span>
                O{' '}
                <strong
                  className={
                    activeBar.close >= activeBar.open ? 'text-[#089981]' : 'text-[#F23645]'
                  }
                >
                  {formatPrice(activeBar.open)}
                </strong>
              </span>
              <span>
                H{' '}
                <strong
                  className={
                    activeBar.close >= activeBar.open ? 'text-[#089981]' : 'text-[#F23645]'
                  }
                >
                  {formatPrice(activeBar.high)}
                </strong>
              </span>
              <span>
                L{' '}
                <strong
                  className={
                    activeBar.close >= activeBar.open ? 'text-[#089981]' : 'text-[#F23645]'
                  }
                >
                  {formatPrice(activeBar.low)}
                </strong>
              </span>
              <span>
                C{' '}
                <strong
                  className={
                    activeBar.close >= activeBar.open ? 'text-[#089981]' : 'text-[#F23645]'
                  }
                >
                  {formatPrice(activeBar.close)}
                </strong>
              </span>
              <span className="text-slate-500 dark:text-[#787B86]">
                Vol {activeBar.volume.toFixed(1)}
              </span>
            </div>
          )}
        </div>

        {/* Indicator & Multi-source legend */}
        <div className="flex flex-wrap items-center gap-3 text-[11px] font-mono-tabular">
          {activeIndicators
            .filter((i) => i.config.pane === 'main')
            .map((ind) => {
              const v = ind.values?.[hoveredIdx];
              const bbM = ind.bollinger?.middle?.[hoveredIdx];
              return (
                <span key={ind.config.id} style={{ color: ind.config.color }}>
                  {ind.config.label}{' '}
                  {v !== null && v !== undefined
                    ? formatPrice(v)
                    : bbM !== null && bbM !== undefined
                      ? formatPrice(bbM)
                      : ''}
                </span>
              );
            })}

          {overlaySources
            .filter((s) => s !== primarySource)
            .map((src) => {
              const arr = secondarySeries[src];
              const lastPt = arr?.[arr.length - 1];
              return (
                <span key={src} style={{ color: SOURCE_COLORS[src] }}>
                  {src.toUpperCase()} {lastPt ? formatPrice(lastPt.close) : '—'}
                </span>
              );
            })}
        </div>
      </div>

      {/* Inline Drawing Hint */}
      {activeTool !== 'cursor' && (
        <div className="pointer-events-none absolute bottom-9 left-3 px-2.5 py-1 rounded bg-slate-900/85 text-white text-[11px]">
          {activeTool === 'hline' && 'Cliquez sur le graphique pour placer une ligne horizontale'}
          {activeTool === 'text' && 'Cliquez pour placer une annotation texte'}
          {(activeTool === 'trendline' || activeTool === 'fib' || activeTool === 'rect') &&
            `Cliquez le point ${draftPoints.length + 1}/2 sur le graphique (Esc pour annuler)`}
          {activeTool === 'channel' &&
            `Cliquez le point ${draftPoints.length + 1}/3 pour tracer le canal parallèle`}
        </div>
      )}

      {/* Floating Inline Text Input for Text Annotations */}
      {textModal.open && (
        <form
          onSubmit={commitTextAnnotation}
          style={{ left: Math.min(textModal.x, chartW - 220), top: Math.max(12, textModal.y - 20) }}
          className="absolute z-30 flex items-center gap-1.5 p-1.5 rounded bg-white dark:bg-[#1E222D] border border-slate-300 dark:border-[#2A2E39] shadow-xl"
        >
          <input
            autoFocus
            type="text"
            placeholder="Annotation..."
            value={textModal.value}
            onChange={(e) => setTextModal((m) => ({ ...m, value: e.target.value }))}
            className="w-40 px-2 py-1 text-xs bg-slate-50 dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white focus:outline-none focus:border-[#2962FF]"
          />
          <button
            type="submit"
            className="px-2 py-1 text-xs font-semibold rounded bg-[#2962FF] text-white cursor-pointer"
          >
            OK
          </button>
          <button
            type="button"
            onClick={() => {
              setTextModal({ open: false, x: 0, y: 0, point: null, value: '' });
              onResetTool();
            }}
            className="px-1.5 py-1 text-xs text-slate-500 hover:text-slate-900 dark:hover:text-white cursor-pointer"
          >
            ×
          </button>
        </form>
      )}
    </div>
  );
};
