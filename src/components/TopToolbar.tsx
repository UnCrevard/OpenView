import React, { useState, useRef, useEffect } from 'react';
import {
  Activity,
  Code2,
  Download,
  Layers,
  LineChart,
  Moon,
  SlidersHorizontal,
  Sun,
} from 'lucide-react';
import { DataSourceId, Timeframe, WatchlistToken } from '../types/trading';

interface TopToolbarProps {
  activeSymbol: string;
  watchlist: WatchlistToken[];
  onSelectSymbol: (symbol: string) => void;
  timeframe: Timeframe;
  onSelectTimeframe: (tf: Timeframe) => void;
  primarySource: DataSourceId;
  onOpenIndicators: () => void;
  onOpenSources: () => void;
  onExportCSV: () => void;
  onExportJSON: () => void;
  viewMode: 'terminal' | 'c_code';
  onChangeViewMode: (mode: 'terminal' | 'c_code') => void;
  theme: 'dark' | 'light';
  onToggleTheme: () => void;
  onDownloadCZip: () => void;
  activeIndicatorCount: number;
  overlaySourceCount: number;
}

const TIMEFRAMES: Array<{ value: Timeframe; label: string; title: string }> = [
  { value: '1m', label: '1m', title: '1 minute' },
  { value: '5m', label: '5m', title: '5 minutes' },
  { value: '15m', label: '15m', title: '15 minutes' },
  { value: '1h', label: '1h', title: '1 heure' },
  { value: '4h', label: '4h', title: '4 heures' },
  { value: '1d', label: '1d', title: '1 jour' },
  { value: '1w', label: '1S', title: '1 semaine' },
  { value: '1M', label: '1M', title: '1 mois' },
  { value: '1y', label: '1A', title: '1 an' },
];

export const TopToolbar: React.FC<TopToolbarProps> = ({
  activeSymbol,
  watchlist,
  onSelectSymbol,
  timeframe,
  onSelectTimeframe,
  primarySource,
  onOpenIndicators,
  onOpenSources,
  onExportCSV,
  onExportJSON,
  viewMode,
  onChangeViewMode,
  theme,
  onToggleTheme,
  onDownloadCZip,
  activeIndicatorCount,
  overlaySourceCount,
}) => {
  const [exportOpen, setExportOpen] = useState(false);
  const exportRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const onDocClick = (e: MouseEvent) => {
      if (exportRef.current && !exportRef.current.contains(e.target as Node)) {
        setExportOpen(false);
      }
    };
    document.addEventListener('mousedown', onDocClick);
    return () => document.removeEventListener('mousedown', onDocClick);
  }, []);

  return (
    <header className="h-12 shrink-0 flex items-center justify-between gap-4 px-3 border-b border-slate-200 dark:border-[#2A2E39] bg-slate-50 dark:bg-[#1E222D] select-none">
      {/* Zone 1: Wordmark */}
      <div className="flex items-center gap-4 shrink-0">
        <span className="font-display text-base font-bold tracking-tight text-slate-900 dark:text-white whitespace-nowrap shrink-0">
          OpenView
        </span>

        {viewMode === 'terminal' && (
          <div className="flex items-center gap-2 pl-3 border-l border-slate-200 dark:border-[#2A2E39]">
            <select
              aria-label="Sélectionner un token"
              value={activeSymbol}
              onChange={(e) => onSelectSymbol(e.target.value)}
              className="bg-white dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded px-2.5 py-1 text-xs font-semibold text-slate-900 dark:text-white focus:outline-none focus:border-[#2962FF] whitespace-nowrap"
            >
              {watchlist.map((t) => (
                <option key={t.symbol} value={t.symbol}>
                  {t.symbol} · {t.name}
                </option>
              ))}
            </select>

            <div className="hidden sm:flex items-center bg-slate-200/70 dark:bg-[#131722] rounded p-0.5">
              {TIMEFRAMES.map((tf) => (
                <button
                  key={tf.value}
                  type="button"
                  title={tf.title}
                  onClick={() => onSelectTimeframe(tf.value)}
                  className={`px-2 py-1 text-xs font-medium rounded transition-colors whitespace-nowrap shrink-0 cursor-pointer ${
                    timeframe === tf.value
                      ? 'bg-[#2962FF] text-white'
                      : 'text-slate-600 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-[#D1D4DC]'
                  }`}
                >
                  {tf.label}
                </button>
              ))}
            </div>
          </div>
        )}
      </div>

      {/* Zone 2: Analytical tools & view switch */}
      <nav className="flex items-center gap-1.5 overflow-x-auto">
        {viewMode === 'terminal' && (
          <>
            <button
              type="button"
              onClick={onOpenIndicators}
              className="flex items-center gap-1.5 px-2.5 py-1.5 text-xs font-medium rounded text-slate-700 dark:text-[#D1D4DC] hover:bg-slate-200/70 dark:hover:bg-[#2A2E39] transition-colors whitespace-nowrap shrink-0 cursor-pointer"
            >
              <SlidersHorizontal className="w-3.5 h-3.5" />
              <span>Indicateurs ({activeIndicatorCount})</span>
            </button>

            <button
              type="button"
              onClick={onOpenSources}
              className="flex items-center gap-1.5 px-2.5 py-1.5 text-xs font-medium rounded text-slate-700 dark:text-[#D1D4DC] hover:bg-slate-200/70 dark:hover:bg-[#2A2E39] transition-colors whitespace-nowrap shrink-0 cursor-pointer"
            >
              <Layers className="w-3.5 h-3.5" />
              <span>
                Sources : {primarySource.charAt(0).toUpperCase() + primarySource.slice(1)}
                {overlaySourceCount > 0 ? ` +${overlaySourceCount}` : ''}
              </span>
            </button>

            <div className="relative" ref={exportRef}>
              <button
                type="button"
                onClick={() => setExportOpen((v) => !v)}
                className="flex items-center gap-1.5 px-2.5 py-1.5 text-xs font-medium rounded text-slate-700 dark:text-[#D1D4DC] hover:bg-slate-200/70 dark:hover:bg-[#2A2E39] transition-colors whitespace-nowrap shrink-0 cursor-pointer"
              >
                <Download className="w-3.5 h-3.5" />
                <span>Exporter</span>
              </button>
              {exportOpen && (
                <div className="absolute right-0 mt-1 w-48 bg-white dark:bg-[#1E222D] border border-slate-200 dark:border-[#2A2E39] rounded shadow-lg z-50 py-1">
                  <button
                    type="button"
                    onClick={() => {
                      onExportCSV();
                      setExportOpen(false);
                    }}
                    className="w-full text-left px-3 py-2 text-xs text-slate-700 dark:text-[#D1D4DC] hover:bg-slate-100 dark:hover:bg-[#2A2E39] cursor-pointer"
                  >
                    Exporter OHLCV + Indicateurs (.CSV)
                  </button>
                  <button
                    type="button"
                    onClick={() => {
                      onExportJSON();
                      setExportOpen(false);
                    }}
                    className="w-full text-left px-3 py-2 text-xs text-slate-700 dark:text-[#D1D4DC] hover:bg-slate-100 dark:hover:bg-[#2A2E39] cursor-pointer"
                  >
                    Exporter Espace complet (.JSON)
                  </button>
                </div>
              )}
            </div>
          </>
        )}

        <div className="flex items-center bg-slate-200/70 dark:bg-[#131722] rounded p-0.5 ml-1">
          <button
            type="button"
            onClick={() => onChangeViewMode('terminal')}
            className={`flex items-center gap-1.5 px-2.5 py-1 text-xs font-medium rounded transition-colors whitespace-nowrap shrink-0 cursor-pointer ${
              viewMode === 'terminal'
                ? 'bg-white dark:bg-[#2A2E39] text-slate-900 dark:text-white shadow-xs'
                : 'text-slate-600 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-[#D1D4DC]'
            }`}
          >
            <LineChart className="w-3.5 h-3.5" />
            <span>Terminal</span>
          </button>
          <button
            type="button"
            onClick={() => onChangeViewMode('c_code')}
            className={`flex items-center gap-1.5 px-2.5 py-1 text-xs font-medium rounded transition-colors whitespace-nowrap shrink-0 cursor-pointer ${
              viewMode === 'c_code'
                ? 'bg-white dark:bg-[#2A2E39] text-slate-900 dark:text-white shadow-xs'
                : 'text-slate-600 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-[#D1D4DC]'
            }`}
          >
            <Code2 className="w-3.5 h-3.5" />
            <span>Code C Win32</span>
          </button>
        </div>
      </nav>

      {/* Zone 3: Theme & Primary ZIP Action */}
      <div className="flex items-center gap-2 shrink-0">
        <button
          type="button"
          onClick={onToggleTheme}
          title={theme === 'dark' ? 'Passer en thème clair' : 'Passer en thème sombre'}
          aria-label="Changer de thème"
          className="p-1.5 rounded text-slate-600 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-white hover:bg-slate-200/70 dark:hover:bg-[#2A2E39] transition-colors cursor-pointer"
        >
          {theme === 'dark' ? <Sun className="w-4 h-4" /> : <Moon className="w-4 h-4" />}
        </button>

        <button
          type="button"
          onClick={onDownloadCZip}
          className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-semibold rounded bg-[#2962FF] hover:bg-[#1E53E5] text-white transition-colors whitespace-nowrap shrink-0 cursor-pointer"
        >
          <Activity className="w-3.5 h-3.5 hidden sm:inline" />
          <span>Télécharger C11 ZIP</span>
        </button>
      </div>
    </header>
  );
};
