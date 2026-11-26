import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import JSZip from 'jszip';
import { Bell, X } from 'lucide-react';
import {
  AlertRule,
  AlertTriggerLog,
  CandleBar,
  ChartDrawing,
  DataSourceId,
  DrawingToolType,
  IndicatorConfig,
  IndicatorType,
  ServerWsMessage,
  SourceStatus,
  Timeframe,
  WatchlistEntry,
  WatchlistSection,
  WatchlistToken,
} from './types/trading';
import { computeAllIndicators } from './utils/indicators';
import { evaluateAlertRules, playAlertBeep } from './utils/alerts';
import { exportCandlesToCSV, exportWorkspaceToJSON } from './utils/exportData';
import { C_WIN32_PROJECT_FILES } from './c_win32/cProjectFiles';
import { TopToolbar } from './components/TopToolbar';
import { DrawingSidebar } from './components/DrawingSidebar';
import { TradingCanvas } from './components/TradingCanvas';
import { RightPanelTab, RightSidebar } from './components/RightSidebar';
import { IndicatorModal } from './components/IndicatorModal';
import { DrawingModal } from './components/DrawingModal';
import { CSourceExplorer } from './components/CSourceExplorer';

const DEFAULT_INDICATORS: IndicatorConfig[] = [
  {
    id: 'ema_12',
    type: 'EMA',
    label: 'EMA (12)',
    enabled: true,
    pane: 'main',
    params: { period: 12 },
    color: '#F59E0B',
  },
  {
    id: 'ema_26',
    type: 'EMA',
    label: 'EMA (26)',
    enabled: true,
    pane: 'main',
    params: { period: 26 },
    color: '#2962FF',
  },
  {
    id: 'sma_50',
    type: 'SMA',
    label: 'SMA (50)',
    enabled: false,
    pane: 'main',
    params: { period: 50 },
    color: '#38BDF8',
  },
  {
    id: 'bb_20',
    type: 'BB',
    label: 'Bollinger (20, 2)',
    enabled: false,
    pane: 'main',
    params: { period: 20, stdDev: 2 },
    color: '#A855F7',
    secondaryColor: '#94A3B8',
  },
  {
    id: 'rsi_14',
    type: 'RSI',
    label: 'RSI (14)',
    enabled: true,
    pane: 'rsi',
    params: { period: 14 },
    color: '#A855F7',
  },
  {
    id: 'macd_std',
    type: 'MACD',
    label: 'MACD (12, 26, 9)',
    enabled: false,
    pane: 'macd',
    params: { fast: 12, slow: 26, signal: 9 },
    color: '#2962FF',
    secondaryColor: '#F97316',
  },
];

const DEFAULT_ALERTS: AlertRule[] = [
  {
    id: 'rule_default_1',
    name: 'Croisement EMA Haussier + RSI sain',
    symbol: 'BTC/USDT',
    logic: 'AND',
    conditions: [
      {
        id: 'cond_1',
        leftMetric: 'ema',
        leftParam: 12,
        operator: 'cross_above',
        rightType: 'metric',
        rightMetric: 'ema',
        rightParam: 26,
      },
      {
        id: 'cond_2',
        leftMetric: 'rsi',
        leftParam: 14,
        operator: '<',
        rightType: 'value',
        rightValue: 70,
      },
    ],
    enabled: true,
    sound: true,
    cooldownSec: 60,
    triggerCount: 0,
  },
];

function loadJson<T>(key: string, fallback: T): T {
  try {
    const raw = localStorage.getItem(key);
    if (!raw) return fallback;
    return JSON.parse(raw) as T;
  } catch {
    return fallback;
  }
}

const DEFAULT_SECTIONS: WatchlistSection[] = [
  { id: 'sec_default', name: 'Favoris', collapsed: false },
];

export default function App() {
  const [theme, setTheme] = useState<'dark' | 'light'>(() =>
    loadJson<'dark' | 'light'>('kronochart_theme', 'dark')
  );
  const [viewMode, setViewMode] = useState<'terminal' | 'c_code'>('terminal');
  const [activeSymbol, setActiveSymbol] = useState<string>('BTC/USDT');
  const [activeEntryId, setActiveEntryId] = useState<string | null>(null);
  const [timeframe, setTimeframe] = useState<Timeframe>('15m');
  const [primarySource, setPrimarySource] = useState<DataSourceId>('binance');
  const [overlaySources, setOverlaySources] = useState<DataSourceId[]>([]);

  const [candles, setCandles] = useState<CandleBar[]>([]);
  const [secondarySeries, setSecondarySeries] = useState<
    Partial<Record<DataSourceId, { time: number; close: number }[]>>
  >({});
  const [watchlist, setWatchlist] = useState<WatchlistToken[]>([]);
  const [removedSymbols, setRemovedSymbols] = useState<string[]>(() =>
    loadJson<string[]>('kronochart_removed_symbols', [])
  );
  const [watchlistSections, setWatchlistSections] = useState<WatchlistSection[]>(() => {
    const loaded = loadJson<WatchlistSection[]>('openview_watchlist_sections', DEFAULT_SECTIONS);
    return loaded.length > 0 ? loaded : DEFAULT_SECTIONS;
  });
  const [watchlistEntries, setWatchlistEntries] = useState<WatchlistEntry[]>(() =>
    loadJson<WatchlistEntry[]>('openview_watchlist_entries', [])
  );
  const [sources, setSources] = useState<SourceStatus[]>([]);

  const [indicators, setIndicators] = useState<IndicatorConfig[]>(() =>
    loadJson<IndicatorConfig[]>('kronochart_indicators', DEFAULT_INDICATORS)
  );
  const [indicatorModalOpen, setIndicatorModalOpen] = useState(false);

  const [activeTool, setActiveTool] = useState<DrawingToolType>('cursor');
  const [drawingColor, setDrawingColor] = useState<string>('#2962FF');
  const [drawings, setDrawings] = useState<ChartDrawing[]>(() =>
    loadJson<ChartDrawing[]>('kronochart_drawings', [])
  );
  const [selectedDrawingId, setSelectedDrawingId] = useState<string | null>(null);
  const [editingDrawingId, setEditingDrawingId] = useState<string | null>(null);
  const drawingsHydratedRef = useRef(false);

  const [rightTab, setRightTab] = useState<RightPanelTab>('watchlist');
  const [alerts, setAlerts] = useState<AlertRule[]>(() =>
    loadJson<AlertRule[]>('kronochart_alerts', DEFAULT_ALERTS)
  );
  const [alertLogs, setAlertLogs] = useState<AlertTriggerLog[]>(() =>
    loadJson<AlertTriggerLog[]>('kronochart_alert_logs', [])
  );
  const [toastAlert, setToastAlert] = useState<AlertTriggerLog | null>(null);

  const wsRef = useRef<WebSocket | null>(null);
  const alertsRef = useRef<AlertRule[]>(alerts);
  alertsRef.current = alerts;
  const watchlistRef = useRef<WatchlistToken[]>(watchlist);
  watchlistRef.current = watchlist;

  // Sync theme on <html>
  useEffect(() => {
    document.documentElement.classList.toggle('dark', theme === 'dark');
    localStorage.setItem('kronochart_theme', JSON.stringify(theme));
  }, [theme]);

  // Persist user preferences locally
  useEffect(() => {
    localStorage.setItem('kronochart_indicators', JSON.stringify(indicators));
  }, [indicators]);

  // Hydrate drawings from backend store on mount and merge with localStorage
  useEffect(() => {
    let active = true;
    fetch('/api/drawings')
      .then((r) => (r.ok ? r.json() : null))
      .then((data: { drawings?: ChartDrawing[] } | null) => {
        if (!active) return;
        const serverList = Array.isArray(data?.drawings) ? data.drawings : [];
        setDrawings((localList) => {
          const byId = new Map<string, ChartDrawing>();
          for (const d of serverList) byId.set(d.id, d);
          for (const d of localList) byId.set(d.id, d);
          return Array.from(byId.values());
        });
      })
      .catch(() => {
        // Fallback to localStorage only if server endpoint unavailable
      })
      .finally(() => {
        if (active) drawingsHydratedRef.current = true;
      });
    return () => {
      active = false;
    };
  }, []);

  useEffect(() => {
    localStorage.setItem('kronochart_drawings', JSON.stringify(drawings));
    if (!drawingsHydratedRef.current) return;
    fetch('/api/drawings', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ drawings }),
    }).catch(() => {
      // ignore offline save error
    });
  }, [drawings]);

  useEffect(() => {
    localStorage.setItem('kronochart_alerts', JSON.stringify(alerts));
  }, [alerts]);

  useEffect(() => {
    localStorage.setItem('kronochart_alert_logs', JSON.stringify(alertLogs.slice(0, 50)));
  }, [alertLogs]);

  useEffect(() => {
    localStorage.setItem('kronochart_removed_symbols', JSON.stringify(removedSymbols));
  }, [removedSymbols]);

  useEffect(() => {
    localStorage.setItem('openview_watchlist_sections', JSON.stringify(watchlistSections));
  }, [watchlistSections]);

  useEffect(() => {
    localStorage.setItem('openview_watchlist_entries', JSON.stringify(watchlistEntries));
  }, [watchlistEntries]);

  // Seed default watchlist entries from server watchlist on initial load if empty
  useEffect(() => {
    if (watchlist.length === 0) return;
    setWatchlistEntries((prev) => {
      if (prev.length > 0) return prev;
      const defaultSecId = watchlistSections[0]?.id ?? 'sec_default';
      return watchlist
        .filter((t) => !removedSymbols.includes(t.symbol))
        .map((t, idx) => ({
          id: `wl_${t.symbol.replace(/[^A-Z0-9]/gi, '')}_${idx}`,
          symbol: t.symbol,
          sectionId: defaultSecId,
        }));
    });
  }, [watchlist, watchlistSections, removedSymbols]);

  // Keyboard shortcuts (Escape / Delete)
  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      const target = e.target as HTMLElement | null;
      const isInput =
        target?.tagName === 'INPUT' ||
        target?.tagName === 'TEXTAREA' ||
        target?.tagName === 'SELECT';
      if (isInput) return;

      if (e.key === 'Escape') {
        setActiveTool('cursor');
        setSelectedDrawingId(null);
      } else if ((e.key === 'Delete' || e.key === 'Backspace') && selectedDrawingId) {
        setDrawings((prev) => prev.filter((d) => d.id !== selectedDrawingId));
        setSelectedDrawingId(null);
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [selectedDrawingId]);

  const sendSubscribe = useCallback(
    (sym: string, tf: Timeframe, src: DataSourceId) => {
      const ws = wsRef.current;
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(
          JSON.stringify({
            type: 'subscribe',
            symbol: sym,
            timeframe: tf,
            primarySource: src,
          })
        );
      }
    },
    []
  );

  // Centralized WebSocket client connection
  useEffect(() => {
    let disposed = false;
    let reconnectTimer: number | null = null;

    const connect = () => {
      if (disposed) return;
      const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
      const ws = new WebSocket(`${proto}//${window.location.host}/ws/feeds`);
      wsRef.current = ws;

      ws.onopen = () => {
        sendSubscribe(activeSymbol, timeframe, primarySource);
      };

      ws.onmessage = (evt) => {
        try {
          const msg = JSON.parse(evt.data as string) as ServerWsMessage;

          if (msg.type === 'snapshot') {
            setCandles(msg.candles);
            setSecondarySeries(msg.secondarySeries);
            setWatchlist(msg.watchlist);
            setSources(msg.sources);
          } else if (msg.type === 'watchlist') {
            setWatchlist(msg.watchlist);
            setSources(msg.sources);
          } else if (msg.type === 'tick') {
            if (msg.sources) setSources(msg.sources);
            if (msg.watchlistItem) {
              setWatchlist((prev) =>
                prev.map((w) => (w.symbol === msg.watchlistItem!.symbol ? msg.watchlistItem! : w))
              );
            }

            if (msg.symbol === activeSymbol && msg.timeframe === timeframe) {
              // Update secondary series
              setSecondarySeries((prev) => {
                const existing = prev[msg.source] ?? [];
                const next = [...existing];
                const last = next[next.length - 1];
                if (last && last.time === msg.candle.time) {
                  next[next.length - 1] = { time: msg.candle.time, close: msg.candle.close };
                } else {
                  next.push({ time: msg.candle.time, close: msg.candle.close });
                  if (next.length > 500) next.shift();
                }
                return { ...prev, [msg.source]: next };
              });

              if (msg.source === primarySource) {
                setCandles((prev) => {
                  const next = [...prev];
                  const last = next[next.length - 1];
                  if (last && last.time === msg.candle.time) {
                    next[next.length - 1] = msg.candle;
                  } else {
                    next.push(msg.candle);
                    if (next.length > 500) next.shift();
                  }

                  // Evaluate multi-condition alerts on updated candles
                  const tok = watchlistRef.current.find((w) => w.symbol === activeSymbol);
                  const { triggeredLogs, updatedRules } = evaluateAlertRules(
                    alertsRef.current,
                    activeSymbol,
                    next,
                    tok
                  );
                  if (triggeredLogs.length > 0) {
                    setAlerts(updatedRules);
                    setAlertLogs((logsPrev) => [...triggeredLogs, ...logsPrev].slice(0, 50));
                    const latestLog = triggeredLogs[0];
                    setToastAlert(latestLog);
                    const matchedRule = updatedRules.find((r) => r.id === latestLog.ruleId);
                    if (matchedRule?.sound) playAlertBeep();
                  }

                  return next;
                });
              }
            }
          }
        } catch {
          // ignore malformed messages
        }
      };

      ws.onclose = () => {
        if (!disposed) {
          reconnectTimer = window.setTimeout(connect, 1800);
        }
      };
    };

    connect();
    return () => {
      disposed = true;
      if (reconnectTimer) clearTimeout(reconnectTimer);
      wsRef.current?.close();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Re-subscribe when symbol, timeframe, or primarySource changes
  useEffect(() => {
    sendSubscribe(activeSymbol, timeframe, primarySource);
  }, [activeSymbol, timeframe, primarySource, sendSubscribe]);

  // Auto-hide toast notification after 5s
  useEffect(() => {
    if (!toastAlert) return;
    const t = window.setTimeout(() => setToastAlert(null), 5000);
    return () => clearTimeout(t);
  }, [toastAlert]);

  const visibleWatchlist = useMemo(() => {
    const activeSet = new Set(watchlistEntries.map((e) => e.symbol));
    const filtered = watchlist.filter((t) => activeSet.has(t.symbol));
    return filtered.length > 0
      ? filtered
      : watchlist.filter((t) => !removedSymbols.includes(t.symbol));
  }, [watchlist, watchlistEntries, removedSymbols]);

  const computedIndicators = useMemo(
    () => computeAllIndicators(candles, indicators),
    [candles, indicators]
  );

  const handleAddToken = async (symbol: string, targetSectionId?: string) => {
    const secId = targetSectionId || watchlistSections[0]?.id || 'sec_default';
    const existingCount = watchlistEntries.filter((e) => e.symbol === symbol).length;

    setRemovedSymbols((prev) => prev.filter((s) => s !== symbol));

    if (existingCount > 0) {
      // Token already present -> clone it as a distinct watchlist entry
      const cloneId = `wl_${Date.now()}_${Math.random().toString(36).slice(2, 6)}`;
      const base = symbol.split('/')[0] || symbol;
      setWatchlistEntries((prev) => [
        ...prev,
        {
          id: cloneId,
          symbol,
          displayName: `${base} (copie ${existingCount + 1})`,
          sectionId: secId,
        },
      ]);
      setActiveEntryId(cloneId);
      setActiveSymbol(symbol);
      return;
    }

    const newId = `wl_${Date.now()}_${Math.random().toString(36).slice(2, 6)}`;
    setWatchlistEntries((prev) => [...prev, { id: newId, symbol, sectionId: secId }]);
    setActiveEntryId(newId);
    setActiveSymbol(symbol);

    try {
      const resp = await fetch('/api/watchlist', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ symbol }),
      });
      if (resp.ok) {
        const data = (await resp.json()) as { watchlist: WatchlistToken[] };
        setWatchlist(data.watchlist);
      }
    } catch {
      // ignore network error
    }
  };

  const handleCloneEntry = (entryId: string) => {
    const src = watchlistEntries.find((e) => e.id === entryId);
    if (!src) return;
    const existingCount = watchlistEntries.filter((e) => e.symbol === src.symbol).length;
    const cloneId = `wl_${Date.now()}_${Math.random().toString(36).slice(2, 6)}`;
    const labelBase = src.displayName?.trim() || src.symbol.split('/')[0] || src.symbol;
    setWatchlistEntries((prev) => [
      ...prev,
      {
        id: cloneId,
        symbol: src.symbol,
        displayName: `${labelBase} (copie ${existingCount + 1})`,
        sectionId: src.sectionId,
      },
    ]);
    setActiveEntryId(cloneId);
    setActiveSymbol(src.symbol);
  };

  const handleRenameEntry = (entryId: string, displayName: string) => {
    setWatchlistEntries((prev) =>
      prev.map((e) =>
        e.id === entryId ? { ...e, displayName: displayName || undefined } : e
      )
    );
  };

  const handleMoveEntry = (entryId: string, targetSectionId: string) => {
    setWatchlistEntries((prev) =>
      prev.map((e) => (e.id === entryId ? { ...e, sectionId: targetSectionId } : e))
    );
  };

  const handleReorderEntry = (
    entryId: string,
    targetSectionId: string,
    targetEntryId?: string,
    position: 'before' | 'after' = 'after'
  ) => {
    setWatchlistEntries((prev) => {
      const srcIdx = prev.findIndex((e) => e.id === entryId);
      if (srcIdx === -1) return prev;
      const moving = { ...prev[srcIdx], sectionId: targetSectionId };
      const rest = prev.filter((e) => e.id !== entryId);
      if (targetEntryId && targetEntryId !== entryId) {
        const dstIdx = rest.findIndex((e) => e.id === targetEntryId);
        if (dstIdx !== -1) {
          const insertAt = position === 'after' ? dstIdx + 1 : dstIdx;
          return [...rest.slice(0, insertAt), moving, ...rest.slice(insertAt)];
        }
      }
      let lastInSec = -1;
      for (let i = rest.length - 1; i >= 0; i--) {
        if (rest[i].sectionId === targetSectionId) {
          lastInSec = i;
          break;
        }
      }
      const insertAt = lastInSec !== -1 ? lastInSec + 1 : rest.length;
      return [...rest.slice(0, insertAt), moving, ...rest.slice(insertAt)];
    });
  };

  const handleRemoveEntry = (entryId: string) => {
    setDrawings((prev) => prev.filter((d) => d.entryId !== entryId));
    setWatchlistEntries((prev) => {
      if (prev.length <= 1) return prev;
      const target = prev.find((e) => e.id === entryId);
      const next = prev.filter((e) => e.id !== entryId);
      if (target && (activeEntryId === entryId || !next.some((e) => e.symbol === activeSymbol))) {
        const fallback = next[0];
        if (fallback) {
          setActiveEntryId(fallback.id);
          setActiveSymbol(fallback.symbol);
        }
      }
      if (target && !next.some((e) => e.symbol === target.symbol)) {
        setRemovedSymbols((r) => (r.includes(target.symbol) ? r : [...r, target.symbol]));
      }
      return next;
    });
  };

  const handleAddSection = (name: string) => {
    const id = `sec_${Date.now()}_${Math.random().toString(36).slice(2, 5)}`;
    setWatchlistSections((prev) => [...prev, { id, name, collapsed: false }]);
  };

  const handleRenameSection = (sectionId: string, name: string) => {
    setWatchlistSections((prev) =>
      prev.map((s) => (s.id === sectionId ? { ...s, name } : s))
    );
  };

  const handleToggleSection = (sectionId: string) => {
    setWatchlistSections((prev) =>
      prev.map((s) => (s.id === sectionId ? { ...s, collapsed: !s.collapsed } : s))
    );
  };

  const handleReorderSection = (
    sourceSectionId: string,
    targetSectionId: string,
    position: 'before' | 'after' = 'before'
  ) => {
    if (sourceSectionId === targetSectionId) return;
    setWatchlistSections((prev) => {
      const srcIdx = prev.findIndex((s) => s.id === sourceSectionId);
      if (srcIdx === -1) return prev;
      const moving = prev[srcIdx];
      const rest = prev.filter((s) => s.id !== sourceSectionId);
      const dstIdx = rest.findIndex((s) => s.id === targetSectionId);
      if (dstIdx === -1) return prev;
      const insertAt = position === 'after' ? dstIdx + 1 : dstIdx;
      return [...rest.slice(0, insertAt), moving, ...rest.slice(insertAt)];
    });
  };

  const handleDeleteSection = (sectionId: string) => {
    if (watchlistSections.length <= 1) return;
    const remaining = watchlistSections.filter((s) => s.id !== sectionId);
    const fallbackId = remaining[0].id;
    setWatchlistEntries((prev) =>
      prev.map((e) => (e.sectionId === sectionId ? { ...e, sectionId: fallbackId } : e))
    );
    setWatchlistSections(remaining);
  };

  const handleDownloadCZip = async () => {
    const zip = new JSZip();
    const folder = zip.folder('OpenViewWin32');
    if (folder) {
      for (const f of C_WIN32_PROJECT_FILES) {
        folder.file(f.path, f.content);
      }
    }
    const blob = await zip.generateAsync({ type: 'blob', compression: 'DEFLATE' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'OpenViewWin32_C11_Direct2D.zip';
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  };

  const handleAddIndicator = (type: IndicatorType, period: number) => {
    const palette = ['#10B981', '#EC4899', '#F59E0B', '#38BDF8', '#A855F7'];
    const color = palette[indicators.length % palette.length];
    const id = `${type.toLowerCase()}_${period}_${Date.now().toString(36)}`;
    setIndicators((prev) => [
      ...prev,
      {
        id,
        type,
        label: `${type} (${period})`,
        enabled: true,
        pane: 'main',
        params: { period },
        color,
      },
    ]);
  };

  const handleUpdateIndicatorParam = (id: string, key: string, value: number) => {
    setIndicators((prev) =>
      prev.map((ind) => {
        if (ind.id !== id) return ind;
        const nextParams = { ...ind.params, [key]: value };
        let nextLabel = ind.label;
        if (ind.type === 'EMA' || ind.type === 'SMA' || ind.type === 'RSI') {
          nextLabel = `${ind.type} (${nextParams.period})`;
        } else if (ind.type === 'BB') {
          nextLabel = `Bollinger (${nextParams.period}, ${nextParams.stdDev})`;
        } else if (ind.type === 'MACD') {
          nextLabel = `MACD (${nextParams.fast}, ${nextParams.slow}, ${nextParams.signal})`;
        }
        return { ...ind, params: nextParams, label: nextLabel };
      })
    );
  };

  const effectiveEntryId = useMemo(() => {
    if (activeEntryId && watchlistEntries.some((e) => e.id === activeEntryId)) {
      return activeEntryId;
    }
    return watchlistEntries.find((e) => e.symbol === activeSymbol)?.id ?? null;
  }, [activeEntryId, watchlistEntries, activeSymbol]);

  const isPrimaryEntryForSymbol = useMemo(() => {
    const firstForSym = watchlistEntries.find((e) => e.symbol === activeSymbol);
    if (!firstForSym || !effectiveEntryId) return true;
    return firstForSym.id === effectiveEntryId;
  }, [watchlistEntries, activeSymbol, effectiveEntryId]);

  const activeEntryDrawings = useMemo(
    () =>
      drawings.filter((d) => {
        if (d.entryId) {
          return effectiveEntryId ? d.entryId === effectiveEntryId : d.symbol === activeSymbol;
        }
        return d.symbol === activeSymbol && isPrimaryEntryForSymbol;
      }),
    [drawings, effectiveEntryId, activeSymbol, isPrimaryEntryForSymbol]
  );

  return (
    <div className="h-screen w-screen flex flex-col overflow-hidden bg-white dark:bg-[#131722] text-slate-900 dark:text-[#D1D4DC]">
      <TopToolbar
        activeSymbol={activeSymbol}
        watchlist={visibleWatchlist.length > 0 ? visibleWatchlist : watchlist}
        onSelectSymbol={(sym) => {
          const match = watchlistEntries.find((e) => e.symbol === sym);
          setActiveEntryId(match ? match.id : null);
          setActiveSymbol(sym);
          setSelectedDrawingId(null);
        }}
        timeframe={timeframe}
        onSelectTimeframe={setTimeframe}
        primarySource={primarySource}
        onOpenIndicators={() => setIndicatorModalOpen(true)}
        onOpenSources={() => {
          setViewMode('terminal');
          setRightTab('sources');
        }}
        onExportCSV={() =>
          exportCandlesToCSV(
            activeSymbol,
            timeframe,
            primarySource,
            candles,
            computedIndicators,
            indicators
          )
        }
        onExportJSON={() =>
          exportWorkspaceToJSON({
            symbol: activeSymbol,
            timeframe,
            source: primarySource,
            candles,
            indicators,
            computed: computedIndicators,
            drawings: activeEntryDrawings,
            alerts,
            alertLogs,
          })
        }
        viewMode={viewMode}
        onChangeViewMode={setViewMode}
        theme={theme}
        onToggleTheme={() => setTheme((t) => (t === 'dark' ? 'light' : 'dark'))}
        onDownloadCZip={() => void handleDownloadCZip()}
        activeIndicatorCount={indicators.filter((i) => i.enabled).length}
        overlaySourceCount={overlaySources.filter((s) => s !== primarySource).length}
      />

      {viewMode === 'terminal' ? (
        <div className="flex-1 flex min-h-0 overflow-hidden">
          <DrawingSidebar
            activeTool={activeTool}
            onSelectTool={setActiveTool}
            drawingColor={drawingColor}
            onChangeColor={setDrawingColor}
            drawingCount={activeEntryDrawings.length}
            selectedDrawingId={selectedDrawingId}
            onDeleteSelected={() => {
              if (!selectedDrawingId) return;
              setDrawings((prev) => prev.filter((d) => d.id !== selectedDrawingId));
              setSelectedDrawingId(null);
            }}
            onUndoDrawing={() => {
              const lastForEntry = [...activeEntryDrawings].pop();
              if (lastForEntry) {
                setDrawings((prev) => prev.filter((d) => d.id !== lastForEntry.id));
              }
            }}
            onClearDrawings={() => {
              const idsToDelete = new Set(activeEntryDrawings.map((d) => d.id));
              setDrawings((prev) => prev.filter((d) => !idsToDelete.has(d.id)));
              setSelectedDrawingId(null);
            }}
          />

          <main className="flex-1 flex flex-col min-w-0 min-h-0 relative">
            <TradingCanvas
              symbol={activeSymbol}
              entryId={effectiveEntryId}
              timeframe={timeframe}
              primarySource={primarySource}
              candles={candles}
              secondarySeries={secondarySeries}
              overlaySources={overlaySources}
              computed={computedIndicators}
              activeTool={activeTool}
              drawingColor={drawingColor}
              drawings={activeEntryDrawings}
              selectedDrawingId={selectedDrawingId}
              onSelectDrawing={setSelectedDrawingId}
              onAddDrawing={(d) =>
                setDrawings((prev) => [
                  ...prev,
                  {
                    ...d,
                    entryId: effectiveEntryId ?? undefined,
                    id: `drw_${Date.now()}_${Math.random().toString(36).slice(2, 6)}`,
                  },
                ])
              }
              onUpdateDrawing={(updated) =>
                setDrawings((prev) => prev.map((d) => (d.id === updated.id ? updated : d)))
              }
              onEditDrawing={(id) => setEditingDrawingId(id)}
              onResetTool={() => setActiveTool('cursor')}
              theme={theme}
            />

            {toastAlert && (
              <div className="absolute top-3 right-24 z-40 max-w-xs p-3 rounded-lg border border-slate-200 dark:border-[#2A2E39] bg-white dark:bg-[#1E222D] shadow-xl">
                <div className="flex items-start justify-between gap-2">
                  <div className="flex items-center gap-1.5 text-xs font-semibold text-[#089981]">
                    <Bell className="w-3.5 h-3.5" />
                    <span>Alerte déclenchée</span>
                  </div>
                  <button
                    type="button"
                    onClick={() => setToastAlert(null)}
                    className="text-slate-400 hover:text-slate-900 dark:hover:text-white cursor-pointer"
                  >
                    <X className="w-3.5 h-3.5" />
                  </button>
                </div>
                <div className="mt-1 text-xs font-semibold text-slate-900 dark:text-white">
                  {toastAlert.ruleName}
                </div>
                <div className="mt-0.5 text-[11px] font-mono-tabular text-slate-500 dark:text-[#787B86]">
                  {toastAlert.symbol} · {toastAlert.price.toFixed(2)}
                </div>
              </div>
            )}
          </main>

          <RightSidebar
            activeTab={rightTab}
            onChangeTab={setRightTab}
            watchlist={watchlist}
            watchlistSections={watchlistSections}
            watchlistEntries={watchlistEntries}
            activeSymbol={activeSymbol}
            activeEntryId={effectiveEntryId}
            onSelectEntry={(entryId, sym) => {
              setActiveEntryId(entryId);
              setActiveSymbol(sym);
              setSelectedDrawingId(null);
            }}
            onAddToken={(sym, secId) => void handleAddToken(sym, secId)}
            onCloneEntry={(id) => {
              handleCloneEntry(id);
              setSelectedDrawingId(null);
            }}
            onRenameEntry={handleRenameEntry}
            onMoveEntry={handleMoveEntry}
            onReorderEntry={handleReorderEntry}
            onRemoveEntry={handleRemoveEntry}
            onAddSection={handleAddSection}
            onRenameSection={handleRenameSection}
            onToggleSection={handleToggleSection}
            onReorderSection={handleReorderSection}
            onDeleteSection={handleDeleteSection}
            sources={sources}
            primarySource={primarySource}
            onChangePrimarySource={setPrimarySource}
            overlaySources={overlaySources}
            onToggleOverlaySource={(src) =>
              setOverlaySources((prev) =>
                prev.includes(src) ? prev.filter((s) => s !== src) : [...prev, src]
              )
            }
            alerts={alerts}
            alertLogs={alertLogs}
            onAddAlertRule={(rule) =>
              setAlerts((prev) => [
                { ...rule, id: `rule_${Date.now()}`, triggerCount: 0 },
                ...prev,
              ])
            }
            onToggleAlertRule={(id) =>
              setAlerts((prev) =>
                prev.map((r) => (r.id === id ? { ...r, enabled: !r.enabled } : r))
              )
            }
            onDeleteAlertRule={(id) =>
              setAlerts((prev) => prev.filter((r) => r.id !== id))
            }
            onClearAlertLogs={() => setAlertLogs([])}
          />
        </div>
      ) : (
        <CSourceExplorer onDownloadZip={() => void handleDownloadCZip()} />
      )}

      <IndicatorModal
        open={indicatorModalOpen}
        onClose={() => setIndicatorModalOpen(false)}
        indicators={indicators}
        onToggleIndicator={(id) =>
          setIndicators((prev) =>
            prev.map((ind) => (ind.id === id ? { ...ind, enabled: !ind.enabled } : ind))
          )
        }
        onUpdateParam={handleUpdateIndicatorParam}
        onUpdateColor={(id, color) =>
          setIndicators((prev) =>
            prev.map((ind) => (ind.id === id ? { ...ind, color } : ind))
          )
        }
        onAddIndicator={handleAddIndicator}
        onRemoveIndicator={(id) =>
          setIndicators((prev) => prev.filter((ind) => ind.id !== id))
        }
      />

      <DrawingModal
        drawing={drawings.find((d) => d.id === editingDrawingId) ?? null}
        onClose={() => setEditingDrawingId(null)}
        onSave={(updated) =>
          setDrawings((prev) => prev.map((d) => (d.id === updated.id ? updated : d)))
        }
        onDelete={(id) => {
          setDrawings((prev) => prev.filter((d) => d.id !== id));
          if (selectedDrawingId === id) setSelectedDrawingId(null);
        }}
      />
    </div>
  );
}
