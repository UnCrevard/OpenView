import React, { useState } from 'react';
import {
  ArrowDownRight,
  ArrowRightLeft,
  ArrowUpRight,
  Bell,
  ChevronDown,
  ChevronRight,
  Copy,
  FolderPlus,
  GripVertical,
  Layers,
  List,
  Pencil,
  Plus,
  Search,
  Trash2,
  Volume2,
  VolumeX,
  X,
} from 'lucide-react';
import {
  AlertCondition,
  AlertRule,
  AlertTriggerLog,
  ConditionMetric,
  ConditionOperator,
  DataSourceId,
  SourceStatus,
  WatchlistEntry,
  WatchlistSection,
  WatchlistToken,
} from '../types/trading';
import { computeSpreadBps, formatConditionSummary } from '../utils/alerts';

export type RightPanelTab = 'watchlist' | 'alerts' | 'sources';

interface RightSidebarProps {
  activeTab: RightPanelTab;
  onChangeTab: (tab: RightPanelTab) => void;
  watchlist: WatchlistToken[];
  watchlistSections: WatchlistSection[];
  watchlistEntries: WatchlistEntry[];
  activeSymbol: string;
  activeEntryId: string | null;
  onSelectEntry: (entryId: string, symbol: string) => void;
  onAddToken: (symbol: string, sectionId?: string) => void;
  onCloneEntry: (entryId: string) => void;
  onRenameEntry: (entryId: string, displayName: string) => void;
  onMoveEntry: (entryId: string, targetSectionId: string) => void;
  onReorderEntry: (
    entryId: string,
    targetSectionId: string,
    targetEntryId?: string,
    position?: 'before' | 'after'
  ) => void;
  onRemoveEntry: (entryId: string) => void;
  onAddSection: (name: string) => void;
  onRenameSection: (sectionId: string, name: string) => void;
  onToggleSection: (sectionId: string) => void;
  onReorderSection: (
    sourceSectionId: string,
    targetSectionId: string,
    position?: 'before' | 'after'
  ) => void;
  onDeleteSection: (sectionId: string) => void;
  sources: SourceStatus[];
  primarySource: DataSourceId;
  onChangePrimarySource: (src: DataSourceId) => void;
  overlaySources: DataSourceId[];
  onToggleOverlaySource: (src: DataSourceId) => void;
  alerts: AlertRule[];
  alertLogs: AlertTriggerLog[];
  onAddAlertRule: (rule: Omit<AlertRule, 'id' | 'triggerCount'>) => void;
  onToggleAlertRule: (id: string) => void;
  onDeleteAlertRule: (id: string) => void;
  onClearAlertLogs: () => void;
}

const METRIC_OPTIONS: Array<{ value: ConditionMetric; label: string; hasParam: boolean; defaultParam: number }> = [
  { value: 'price', label: 'Prix (Close)', hasParam: false, defaultParam: 0 },
  { value: 'ema', label: 'EMA', hasParam: true, defaultParam: 12 },
  { value: 'sma', label: 'SMA', hasParam: true, defaultParam: 50 },
  { value: 'rsi', label: 'RSI', hasParam: true, defaultParam: 14 },
  { value: 'macd', label: 'MACD', hasParam: false, defaultParam: 0 },
  { value: 'macd_signal', label: 'MACD Signal', hasParam: false, defaultParam: 0 },
  { value: 'bb_upper', label: 'Bollinger Sup.', hasParam: true, defaultParam: 20 },
  { value: 'bb_lower', label: 'Bollinger Inf.', hasParam: true, defaultParam: 20 },
  { value: 'spread_bps', label: 'Spread Inter-Sources (bps)', hasParam: false, defaultParam: 0 },
];

const OPERATOR_OPTIONS: Array<{ value: ConditionOperator; label: string }> = [
  { value: '>', label: '>' },
  { value: '<', label: '<' },
  { value: '>=', label: '>=' },
  { value: '<=', label: '<=' },
  { value: 'cross_above', label: 'Croise ↑' },
  { value: 'cross_below', label: 'Croise ↓' },
];

function formatQuote(p: number): string {
  if (!Number.isFinite(p)) return '0.00';
  if (p >= 1000) return p.toLocaleString('fr-FR', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  if (p >= 10) return p.toFixed(3);
  if (p >= 1) return p.toFixed(4);
  return p.toFixed(5);
}

export const RightSidebar: React.FC<RightSidebarProps> = ({
  activeTab,
  onChangeTab,
  watchlist,
  watchlistSections,
  watchlistEntries,
  activeSymbol,
  activeEntryId,
  onSelectEntry,
  onAddToken,
  onCloneEntry,
  onRenameEntry,
  onMoveEntry,
  onReorderEntry,
  onRemoveEntry,
  onAddSection,
  onRenameSection,
  onToggleSection,
  onReorderSection,
  onDeleteSection,
  sources,
  primarySource,
  onChangePrimarySource,
  overlaySources,
  onToggleOverlaySource,
  alerts,
  alertLogs,
  onAddAlertRule,
  onToggleAlertRule,
  onDeleteAlertRule,
  onClearAlertLogs,
}) => {
  const [search, setSearch] = useState('');
  const [newTokenInput, setNewTokenInput] = useState('');
  const [targetAddSection, setTargetAddSection] = useState<string>('');
  const [dragItem, setDragItem] = useState<{ kind: 'entry' | 'section'; id: string } | null>(null);
  const [dropHint, setDropHint] = useState<
    | { kind: 'entry'; id: string; pos: 'before' | 'after' }
    | { kind: 'section'; id: string; pos: 'before' | 'after' | 'inside' }
    | null
  >(null);

  const clearDragState = () => {
    setDragItem(null);
    setDropHint(null);
  };

  // Themed modals for watchlist actions
  const [renameTarget, setRenameTarget] = useState<{
    kind: 'entry' | 'section';
    id: string;
    subtitle: string;
    value: string;
  } | null>(null);
  const [newSectionModalOpen, setNewSectionModalOpen] = useState(false);
  const [newSectionName, setNewSectionName] = useState('');
  const [moveTargetEntry, setMoveTargetEntry] = useState<WatchlistEntry | null>(null);

  // Complex alert builder state
  const [ruleName, setRuleName] = useState('Breakout Momentum');
  const [ruleLogic, setRuleLogic] = useState<'AND' | 'OR'>('AND');
  const [ruleSound, setRuleSound] = useState(true);
  const [ruleCooldown, setRuleCooldown] = useState(30);
  const [draftConditions, setDraftConditions] = useState<AlertCondition[]>([
    {
      id: 'c1',
      leftMetric: 'price',
      operator: '>',
      rightType: 'metric',
      rightMetric: 'ema',
      rightParam: 12,
    },
    {
      id: 'c2',
      leftMetric: 'rsi',
      leftParam: 14,
      operator: '<',
      rightType: 'value',
      rightValue: 70,
    },
  ]);

  const tokenBySymbol = new Map<string, WatchlistToken>();
  for (const t of watchlist) tokenBySymbol.set(t.symbol, t);

  const activeToken = tokenBySymbol.get(activeSymbol);
  const q = search.trim().toLowerCase();

  const handleCreateToken = (e: React.FormEvent) => {
    e.preventDefault();
    const trimmed = newTokenInput.trim().toUpperCase();
    if (!trimmed) return;
    const formatted = trimmed.includes('/') ? trimmed : `${trimmed}/USDT`;
    const secId =
      targetAddSection && watchlistSections.some((s) => s.id === targetAddSection)
        ? targetAddSection
        : watchlistSections[0]?.id;
    onAddToken(formatted, secId);
    setNewTokenInput('');
  };

  const handleAddConditionRow = () => {
    setDraftConditions((prev) => [
      ...prev,
      {
        id: `c_${Date.now()}`,
        leftMetric: 'price',
        operator: '>',
        rightType: 'value',
        rightValue: activeToken ? Math.round(activeToken.price * 1.005 * 100) / 100 : 100,
      },
    ]);
  };

  const handleCreateRule = (e: React.FormEvent) => {
    e.preventDefault();
    if (!ruleName.trim() || draftConditions.length === 0) return;
    onAddAlertRule({
      name: ruleName.trim(),
      symbol: activeSymbol,
      logic: ruleLogic,
      conditions: draftConditions,
      enabled: true,
      sound: ruleSound,
      cooldownSec: Math.max(5, ruleCooldown),
    });
  };

  const spreadBps = computeSpreadBps(activeToken);

  return (
    <aside className="w-80 lg:w-[340px] shrink-0 flex flex-col border-l border-slate-200 dark:border-[#2A2E39] bg-slate-50 dark:bg-[#1E222D] h-full overflow-hidden">
      {/* Tab Navigation */}
      <div className="flex items-center border-b border-slate-200 dark:border-[#2A2E39] px-2 pt-1.5 gap-1">
        <button
          type="button"
          onClick={() => onChangeTab('watchlist')}
          className={`flex items-center gap-1.5 px-3 py-2 text-xs font-semibold border-b-2 transition-colors whitespace-nowrap shrink-0 cursor-pointer ${
            activeTab === 'watchlist'
              ? 'border-[#2962FF] text-slate-900 dark:text-white'
              : 'border-transparent text-slate-500 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-[#D1D4DC]'
          }`}
        >
          <List className="w-3.5 h-3.5" />
          <span>Watchlist ({watchlistEntries.length})</span>
        </button>

        <button
          type="button"
          onClick={() => onChangeTab('alerts')}
          className={`flex items-center gap-1.5 px-3 py-2 text-xs font-semibold border-b-2 transition-colors whitespace-nowrap shrink-0 cursor-pointer ${
            activeTab === 'alerts'
              ? 'border-[#2962FF] text-slate-900 dark:text-white'
              : 'border-transparent text-slate-500 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-[#D1D4DC]'
          }`}
        >
          <Bell className="w-3.5 h-3.5" />
          <span>Alertes ({alerts.filter((a) => a.enabled).length})</span>
        </button>

        <button
          type="button"
          onClick={() => onChangeTab('sources')}
          className={`flex items-center gap-1.5 px-3 py-2 text-xs font-semibold border-b-2 transition-colors whitespace-nowrap shrink-0 cursor-pointer ${
            activeTab === 'sources'
              ? 'border-[#2962FF] text-slate-900 dark:text-white'
              : 'border-transparent text-slate-500 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-[#D1D4DC]'
          }`}
        >
          <Layers className="w-3.5 h-3.5" />
          <span>Sources</span>
        </button>
      </div>

      {/* WATCHLIST TAB */}
      {activeTab === 'watchlist' && (
        <div className="flex-1 flex flex-col min-h-0">
          <div className="p-2.5 border-b border-slate-200 dark:border-[#2A2E39] space-y-2">
            <div className="flex items-center gap-1.5">
              <div className="relative flex-1">
                <Search className="w-3.5 h-3.5 absolute left-2.5 top-1/2 -translate-y-1/2 text-slate-400" />
                <input
                  type="text"
                  placeholder="Filtrer les tokens..."
                  value={search}
                  onChange={(e) => setSearch(e.target.value)}
                  className="w-full pl-8 pr-2.5 py-1.5 text-xs bg-white dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white focus:outline-none focus:border-[#2962FF]"
                />
              </div>
              <button
                type="button"
                onClick={() => {
                  setNewSectionName('');
                  setNewSectionModalOpen(true);
                }}
                title="Créer une nouvelle section"
                className="flex items-center gap-1 px-2 py-1.5 text-xs font-medium rounded border border-slate-300 dark:border-[#2A2E39] bg-white dark:bg-[#131722] text-slate-700 dark:text-[#D1D4DC] hover:border-[#2962FF] whitespace-nowrap cursor-pointer"
              >
                <FolderPlus className="w-3.5 h-3.5" />
                <span>Section</span>
              </button>
            </div>

            <form onSubmit={handleCreateToken} className="flex items-center gap-1.5">
              <input
                type="text"
                placeholder="Ajouter / Cloner (ex. BTC/USDT)"
                value={newTokenInput}
                onChange={(e) => setNewTokenInput(e.target.value)}
                className="flex-1 min-w-0 px-2.5 py-1.5 text-xs bg-white dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white focus:outline-none focus:border-[#2962FF]"
              />
              {watchlistSections.length > 1 && (
                <select
                  value={targetAddSection || watchlistSections[0]?.id || ''}
                  onChange={(e) => setTargetAddSection(e.target.value)}
                  title="Section cible"
                  className="max-w-[92px] px-1.5 py-1.5 text-[11px] bg-white dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-700 dark:text-[#D1D4DC]"
                >
                  {watchlistSections.map((sec) => (
                    <option key={sec.id} value={sec.id}>
                      {sec.name}
                    </option>
                  ))}
                </select>
              )}
              <button
                type="submit"
                className="flex items-center gap-1 px-2.5 py-1.5 text-xs font-semibold rounded bg-[#2962FF] hover:bg-[#1E53E5] text-white whitespace-nowrap cursor-pointer"
              >
                <Plus className="w-3.5 h-3.5" />
                <span>Suivre</span>
              </button>
            </form>
          </div>

          {/* Column header */}
          <div className="grid grid-cols-[1fr_auto_auto] gap-2 px-3 py-1.5 text-[11px] font-medium text-slate-500 dark:text-[#787B86] border-b border-slate-200 dark:border-[#2A2E39]">
            <span>Symbole / Nom</span>
            <span className="text-right">Dernier</span>
            <span className="text-right w-16">24h %</span>
          </div>

          {/* Sections + Token list */}
          <div
            onDragLeave={(e) => {
              if (!e.currentTarget.contains(e.relatedTarget as Node | null)) {
                setDropHint(null);
              }
            }}
            className="flex-1 overflow-y-auto divide-y divide-slate-200/60 dark:divide-[#2A2E39]/60"
          >
            {watchlistSections.map((sec) => {
              const secEntries = watchlistEntries.filter((e) => {
                if (e.sectionId !== sec.id) return false;
                if (!q) return true;
                const tok = tokenBySymbol.get(e.symbol);
                return (
                  e.symbol.toLowerCase().includes(q) ||
                  (e.displayName ?? '').toLowerCase().includes(q) ||
                  (tok?.name ?? '').toLowerCase().includes(q)
                );
              });
              const isDraggingThisSec = dragItem?.kind === 'section' && dragItem.id === sec.id;
              const isSecDropInside =
                dropHint?.kind === 'section' && dropHint.id === sec.id && dropHint.pos === 'inside';
              const isSecDropBefore =
                dropHint?.kind === 'section' && dropHint.id === sec.id && dropHint.pos === 'before';
              const isSecDropAfter =
                dropHint?.kind === 'section' && dropHint.id === sec.id && dropHint.pos === 'after';

              return (
                <div
                  key={sec.id}
                  onDragOver={(e) => {
                    e.preventDefault();
                    e.stopPropagation();
                    if (!dragItem) return;
                    if (dragItem.kind === 'section') {
                      if (dragItem.id === sec.id) return;
                      const rect = e.currentTarget.getBoundingClientRect();
                      const pos: 'before' | 'after' =
                        e.clientY < rect.top + rect.height * 0.5 ? 'before' : 'after';
                      if (
                        dropHint?.kind !== 'section' ||
                        dropHint.id !== sec.id ||
                        dropHint.pos !== pos
                      ) {
                        setDropHint({ kind: 'section', id: sec.id, pos });
                      }
                    } else if (dragItem.kind === 'entry') {
                      if (
                        dropHint?.kind !== 'section' ||
                        dropHint.id !== sec.id ||
                        dropHint.pos !== 'inside'
                      ) {
                        setDropHint({ kind: 'section', id: sec.id, pos: 'inside' });
                      }
                    }
                  }}
                  onDrop={(e) => {
                    e.preventDefault();
                    e.stopPropagation();
                    const payload = e.dataTransfer.getData('text/plain');
                    if (dragItem?.kind === 'section' || payload.startsWith('section:')) {
                      const srcId = payload.startsWith('section:')
                        ? payload.slice('section:'.length)
                        : dragItem?.id;
                      if (srcId && srcId !== sec.id) {
                        const pos =
                          dropHint?.kind === 'section' &&
                          dropHint.id === sec.id &&
                          dropHint.pos === 'after'
                            ? 'after'
                            : 'before';
                        onReorderSection(srcId, sec.id, pos);
                      }
                    } else if (dragItem?.kind === 'entry' || payload.startsWith('entry:') || payload) {
                      const entryId = payload.startsWith('entry:')
                        ? payload.slice('entry:'.length)
                        : dragItem?.kind === 'entry'
                        ? dragItem.id
                        : payload;
                      if (entryId) {
                        onReorderEntry(entryId, sec.id);
                      }
                    }
                    clearDragState();
                  }}
                  className={`transition-colors ${
                    isDraggingThisSec ? 'opacity-50' : ''
                  } ${isSecDropInside ? 'bg-[#2962FF]/10 ring-1 ring-inset ring-[#2962FF]/40' : ''} ${
                    isSecDropBefore ? 'border-t-2 border-t-[#2962FF]' : ''
                  } ${isSecDropAfter ? 'border-b-2 border-b-[#2962FF]' : ''}`}
                >
                  {/* Section header */}
                  <div
                    draggable
                    onDragStart={(e) => {
                      e.stopPropagation();
                      setDragItem({ kind: 'section', id: sec.id });
                      e.dataTransfer.effectAllowed = 'move';
                      e.dataTransfer.setData('text/plain', `section:${sec.id}`);
                    }}
                    onDragEnd={clearDragState}
                    onDragOver={(e) => {
                      e.preventDefault();
                      e.stopPropagation();
                      if (!dragItem) return;
                      if (dragItem.kind === 'section') {
                        if (dragItem.id === sec.id) return;
                        const rect = e.currentTarget.getBoundingClientRect();
                        const pos: 'before' | 'after' =
                          e.clientY < rect.top + rect.height * 0.5 ? 'before' : 'after';
                        setDropHint({ kind: 'section', id: sec.id, pos });
                      } else {
                        setDropHint({ kind: 'section', id: sec.id, pos: 'inside' });
                      }
                    }}
                    onDrop={(e) => {
                      e.preventDefault();
                      e.stopPropagation();
                      const payload = e.dataTransfer.getData('text/plain');
                      if (dragItem?.kind === 'section' || payload.startsWith('section:')) {
                        const srcId = payload.startsWith('section:')
                          ? payload.slice('section:'.length)
                          : dragItem?.id;
                        if (srcId && srcId !== sec.id) {
                          const rect = e.currentTarget.getBoundingClientRect();
                          const pos: 'before' | 'after' =
                            e.clientY < rect.top + rect.height * 0.5 ? 'before' : 'after';
                          onReorderSection(srcId, sec.id, pos);
                        }
                      } else {
                        const entryId = payload.startsWith('entry:')
                          ? payload.slice('entry:'.length)
                          : dragItem?.id;
                        if (entryId) onReorderEntry(entryId, sec.id);
                      }
                      clearDragState();
                    }}
                    className="group/sec flex items-center justify-between px-2 py-1.5 bg-slate-100/80 dark:bg-[#171B26] border-b border-slate-200/60 dark:border-[#2A2E39]/60 cursor-grab active:cursor-grabbing select-none"
                  >
                    <div className="flex items-center gap-1 min-w-0">
                      <GripVertical className="w-3 h-3 shrink-0 text-slate-400/70 dark:text-[#787B86]/70 group-hover/sec:text-slate-600 dark:group-hover/sec:text-[#D1D4DC]" />
                      <button
                        type="button"
                        onClick={() => onToggleSection(sec.id)}
                        className="flex items-center gap-1 text-[11px] font-semibold uppercase tracking-wide text-slate-600 dark:text-[#9CA3AF] hover:text-slate-900 dark:hover:text-white cursor-pointer min-w-0"
                      >
                        {sec.collapsed ? (
                          <ChevronRight className="w-3.5 h-3.5 shrink-0" />
                        ) : (
                          <ChevronDown className="w-3.5 h-3.5 shrink-0" />
                        )}
                        <span className="truncate">{sec.name}</span>
                        <span className="text-[10px] font-mono-tabular text-slate-400 dark:text-[#787B86]">
                          ({secEntries.length})
                        </span>
                      </button>
                    </div>

                    <div className="flex items-center gap-1 opacity-0 group-hover/sec:opacity-100 transition-opacity">
                      <button
                        type="button"
                        onClick={(e) => {
                          e.stopPropagation();
                          setRenameTarget({
                            kind: 'section',
                            id: sec.id,
                            subtitle: 'Section de la watchlist',
                            value: sec.name,
                          });
                        }}
                        title="Renommer la section"
                        className="p-0.5 text-slate-400 hover:text-slate-900 dark:hover:text-white cursor-pointer"
                      >
                        <Pencil className="w-3 h-3" />
                      </button>
                      {watchlistSections.length > 1 && (
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation();
                            onDeleteSection(sec.id);
                          }}
                          title="Supprimer la section (les tokens seront déplacés)"
                          className="p-0.5 text-slate-400 hover:text-rose-500 cursor-pointer"
                        >
                          <Trash2 className="w-3 h-3" />
                        </button>
                      )}
                    </div>
                  </div>

                  {/* Section entries */}
                  {!sec.collapsed &&
                    (secEntries.length === 0 ? (
                      <div className="px-3 py-2 text-[11px] italic text-slate-400 dark:text-[#787B86]">
                        Glissez un token ici ou utilisez ⇄ pour déplacer
                      </div>
                    ) : (
                      secEntries.map((entry) => {
                        const tok = tokenBySymbol.get(entry.symbol);
                        const isSelected = activeEntryId
                          ? entry.id === activeEntryId
                          : entry.symbol === activeSymbol;
                        const price = tok?.price ?? 0;
                        const chg = tok?.change24h ?? 0;
                        const isPos = chg >= 0;
                        const primaryLabel = entry.displayName?.trim() || entry.symbol;
                        const subLabel = entry.displayName?.trim()
                          ? `${entry.symbol}${tok ? ` · ${tok.name}` : ''}`
                          : tok?.name ?? entry.symbol;
                        const isDraggingThisEntry =
                          dragItem?.kind === 'entry' && dragItem.id === entry.id;
                        const isDropBefore =
                          dropHint?.kind === 'entry' &&
                          dropHint.id === entry.id &&
                          dropHint.pos === 'before';
                        const isDropAfter =
                          dropHint?.kind === 'entry' &&
                          dropHint.id === entry.id &&
                          dropHint.pos === 'after';

                        return (
                          <div
                            key={entry.id}
                            draggable
                            onDragStart={(e) => {
                              e.stopPropagation();
                              setDragItem({ kind: 'entry', id: entry.id });
                              e.dataTransfer.effectAllowed = 'move';
                              e.dataTransfer.setData('text/plain', `entry:${entry.id}`);
                            }}
                            onDragEnd={clearDragState}
                            onDragOver={(e) => {
                              if (!dragItem) return;
                              if (dragItem.kind === 'section') return;
                              e.preventDefault();
                              e.stopPropagation();
                              if (dragItem.id === entry.id) return;
                              const rect = e.currentTarget.getBoundingClientRect();
                              const pos: 'before' | 'after' =
                                e.clientY < rect.top + rect.height * 0.5 ? 'before' : 'after';
                              if (
                                dropHint?.kind !== 'entry' ||
                                dropHint.id !== entry.id ||
                                dropHint.pos !== pos
                              ) {
                                setDropHint({ kind: 'entry', id: entry.id, pos });
                              }
                            }}
                            onDrop={(e) => {
                              const payload = e.dataTransfer.getData('text/plain');
                              if (dragItem?.kind === 'section' || payload.startsWith('section:')) {
                                return;
                              }
                              e.preventDefault();
                              e.stopPropagation();
                              const draggedEntryId = payload.startsWith('entry:')
                                ? payload.slice('entry:'.length)
                                : dragItem?.id || payload;
                              if (draggedEntryId && draggedEntryId !== entry.id) {
                                const rect = e.currentTarget.getBoundingClientRect();
                                const pos: 'before' | 'after' =
                                  e.clientY < rect.top + rect.height * 0.5 ? 'before' : 'after';
                                onReorderEntry(draggedEntryId, sec.id, entry.id, pos);
                              }
                              clearDragState();
                            }}
                            onClick={() => onSelectEntry(entry.id, entry.symbol)}
                            className={`group grid grid-cols-[auto_1fr_auto_auto] items-center gap-1.5 pl-1.5 pr-3 py-2 cursor-pointer transition-colors ${
                              isDraggingThisEntry ? 'opacity-45' : ''
                            } ${isDropBefore ? 'border-t-2 border-t-[#2962FF]' : ''} ${
                              isDropAfter ? 'border-b-2 border-b-[#2962FF]' : ''
                            } ${
                              isSelected
                                ? 'bg-[#2962FF]/12 dark:bg-[#2962FF]/18'
                                : 'hover:bg-slate-200/60 dark:hover:bg-[#2A2E39]/55'
                            }`}
                          >
                            <GripVertical className="w-3 h-3 text-slate-400/50 dark:text-[#787B86]/50 opacity-0 group-hover:opacity-100 cursor-grab active:cursor-grabbing shrink-0" />
                            <div className="min-w-0">
                              <div className="flex items-center gap-1">
                                <span className="text-xs font-semibold text-slate-900 dark:text-white truncate">
                                  {primaryLabel}
                                </span>
                                <div className="flex items-center gap-0.5 opacity-0 group-hover:opacity-100 transition-opacity shrink-0">
                                  <button
                                    type="button"
                                    onClick={(e) => {
                                      e.stopPropagation();
                                      setRenameTarget({
                                        kind: 'entry',
                                        id: entry.id,
                                        subtitle: `Symbole réel conservé : ${entry.symbol}`,
                                        value: entry.displayName ?? '',
                                      });
                                    }}
                                    title="Renommer dans la liste"
                                    className="p-0.5 text-slate-400 hover:text-[#2962FF] cursor-pointer"
                                  >
                                    <Pencil className="w-3 h-3" />
                                  </button>
                                  <button
                                    type="button"
                                    onClick={(e) => {
                                      e.stopPropagation();
                                      setMoveTargetEntry(entry);
                                    }}
                                    title="Déplacer vers une autre section"
                                    className="p-0.5 text-slate-400 hover:text-[#2962FF] cursor-pointer"
                                  >
                                    <ArrowRightLeft className="w-3 h-3" />
                                  </button>
                                  <button
                                    type="button"
                                    onClick={(e) => {
                                      e.stopPropagation();
                                      onCloneEntry(entry.id);
                                    }}
                                    title="Cloner dans la liste"
                                    className="p-0.5 text-slate-400 hover:text-[#089981] cursor-pointer"
                                  >
                                    <Copy className="w-3 h-3" />
                                  </button>
                                  {watchlistEntries.length > 1 && (
                                    <button
                                      type="button"
                                      onClick={(e) => {
                                        e.stopPropagation();
                                        onRemoveEntry(entry.id);
                                      }}
                                      title="Retirer de la watchlist"
                                      className="p-0.5 text-slate-400 hover:text-rose-500 cursor-pointer"
                                    >
                                      <Trash2 className="w-3 h-3" />
                                    </button>
                                  )}
                                </div>
                              </div>
                              <div className="text-[11px] text-slate-500 dark:text-[#787B86] truncate">
                                {subLabel}
                              </div>
                            </div>

                            <div className="text-right font-mono-tabular text-xs font-medium text-slate-900 dark:text-white">
                              {tok ? formatQuote(price) : '—'}
                            </div>

                            <div
                              className={`w-16 flex items-center justify-end gap-0.5 font-mono-tabular text-xs font-medium ${
                                isPos ? 'text-[#089981]' : 'text-[#F23645]'
                              }`}
                            >
                              {isPos ? (
                                <ArrowUpRight className="w-3 h-3 shrink-0" />
                              ) : (
                                <ArrowDownRight className="w-3 h-3 shrink-0" />
                              )}
                              <span>
                                {isPos ? '+' : ''}
                                {chg.toFixed(2)}%
                              </span>
                            </div>
                          </div>
                        );
                      })
                    ))}
                </div>
              );
            })}
          </div>

          {/* Active Token Details Footer */}
          {activeToken && (
            <div className="p-3 border-t border-slate-200 dark:border-[#2A2E39] bg-white/60 dark:bg-[#131722] space-y-2">
              <div className="flex items-center justify-between">
                <span className="text-xs font-semibold text-slate-900 dark:text-white">
                  {activeToken.symbol}
                </span>
                <span className="text-[11px] font-mono-tabular text-slate-500 dark:text-[#787B86]">
                  Spread multi-sources : {spreadBps.toFixed(2)} bps
                </span>
              </div>
              <div className="grid grid-cols-3 gap-2 text-[11px]">
                <div>
                  <div className="text-slate-500 dark:text-[#787B86]">Haut 24h</div>
                  <div className="font-mono-tabular text-slate-900 dark:text-[#D1D4DC]">
                    {formatQuote(activeToken.high24h)}
                  </div>
                </div>
                <div>
                  <div className="text-slate-500 dark:text-[#787B86]">Bas 24h</div>
                  <div className="font-mono-tabular text-slate-900 dark:text-[#D1D4DC]">
                    {formatQuote(activeToken.low24h)}
                  </div>
                </div>
                <div>
                  <div className="text-slate-500 dark:text-[#787B86]">Volume</div>
                  <div className="font-mono-tabular text-slate-900 dark:text-[#D1D4DC]">
                    {Math.round(activeToken.volume24h).toLocaleString('fr-FR')}
                  </div>
                </div>
              </div>
            </div>
          )}

          {/* Rename Token / Section Modal (Theme-aware) */}
          {renameTarget && (
            <div
              className="fixed inset-0 z-50 flex items-center justify-center bg-black/55 p-4"
              onMouseDown={(e) => {
                if (e.target === e.currentTarget) setRenameTarget(null);
              }}
            >
              <form
                onSubmit={(e) => {
                  e.preventDefault();
                  if (renameTarget.kind === 'entry') {
                    onRenameEntry(renameTarget.id, renameTarget.value.trim());
                  } else if (renameTarget.value.trim()) {
                    onRenameSection(renameTarget.id, renameTarget.value.trim());
                  }
                  setRenameTarget(null);
                }}
                className="w-full max-w-sm bg-white dark:bg-[#1E222D] border border-slate-200 dark:border-[#2A2E39] rounded-lg shadow-2xl overflow-hidden"
              >
                <div className="flex items-center justify-between px-4 py-3 border-b border-slate-200 dark:border-[#2A2E39]">
                  <div>
                    <h3 className="font-display text-sm font-semibold text-slate-900 dark:text-white">
                      {renameTarget.kind === 'entry'
                        ? 'Renommer l’affichage du token'
                        : 'Renommer la section'}
                    </h3>
                    <p className="text-[11px] text-slate-500 dark:text-[#787B86]">
                      {renameTarget.subtitle}
                    </p>
                  </div>
                  <button
                    type="button"
                    onClick={() => setRenameTarget(null)}
                    className="p-1 text-slate-500 hover:text-slate-900 dark:hover:text-white cursor-pointer"
                  >
                    <X className="w-4 h-4" />
                  </button>
                </div>
                <div className="p-4 space-y-2">
                  <input
                    type="text"
                    autoFocus
                    placeholder={
                      renameTarget.kind === 'entry'
                        ? 'Laisser vide pour le nom par défaut'
                        : 'Nom de la section'
                    }
                    value={renameTarget.value}
                    onChange={(e) =>
                      setRenameTarget((prev) => (prev ? { ...prev, value: e.target.value } : null))
                    }
                    className="w-full px-3 py-1.5 text-xs bg-slate-50 dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white focus:outline-none focus:border-[#2962FF]"
                  />
                </div>
                <div className="flex items-center justify-end gap-2 px-4 py-2.5 border-t border-slate-200 dark:border-[#2A2E39] bg-slate-50 dark:bg-[#131722]">
                  <button
                    type="button"
                    onClick={() => setRenameTarget(null)}
                    className="px-3 py-1.5 text-xs text-slate-600 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-white cursor-pointer"
                  >
                    Annuler
                  </button>
                  <button
                    type="submit"
                    className="px-3.5 py-1.5 text-xs font-semibold rounded bg-[#2962FF] hover:bg-[#1E53E5] text-white cursor-pointer"
                  >
                    Enregistrer
                  </button>
                </div>
              </form>
            </div>
          )}

          {/* Create Section Modal (Theme-aware) */}
          {newSectionModalOpen && (
            <div
              className="fixed inset-0 z-50 flex items-center justify-center bg-black/55 p-4"
              onMouseDown={(e) => {
                if (e.target === e.currentTarget) setNewSectionModalOpen(false);
              }}
            >
              <form
                onSubmit={(e) => {
                  e.preventDefault();
                  if (!newSectionName.trim()) return;
                  onAddSection(newSectionName.trim());
                  setNewSectionModalOpen(false);
                }}
                className="w-full max-w-sm bg-white dark:bg-[#1E222D] border border-slate-200 dark:border-[#2A2E39] rounded-lg shadow-2xl overflow-hidden"
              >
                <div className="flex items-center justify-between px-4 py-3 border-b border-slate-200 dark:border-[#2A2E39]">
                  <h3 className="font-display text-sm font-semibold text-slate-900 dark:text-white">
                    Nouvelle section de suivi
                  </h3>
                  <button
                    type="button"
                    onClick={() => setNewSectionModalOpen(false)}
                    className="p-1 text-slate-500 hover:text-slate-900 dark:hover:text-white cursor-pointer"
                  >
                    <X className="w-4 h-4" />
                  </button>
                </div>
                <div className="p-4">
                  <input
                    type="text"
                    autoFocus
                    placeholder="Ex: Scalping, Long terme, Layer 1..."
                    value={newSectionName}
                    onChange={(e) => setNewSectionName(e.target.value)}
                    className="w-full px-3 py-1.5 text-xs bg-slate-50 dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white focus:outline-none focus:border-[#2962FF]"
                  />
                </div>
                <div className="flex items-center justify-end gap-2 px-4 py-2.5 border-t border-slate-200 dark:border-[#2A2E39] bg-slate-50 dark:bg-[#131722]">
                  <button
                    type="button"
                    onClick={() => setNewSectionModalOpen(false)}
                    className="px-3 py-1.5 text-xs text-slate-600 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-white cursor-pointer"
                  >
                    Annuler
                  </button>
                  <button
                    type="submit"
                    className="px-3.5 py-1.5 text-xs font-semibold rounded bg-[#2962FF] hover:bg-[#1E53E5] text-white cursor-pointer"
                  >
                    Créer
                  </button>
                </div>
              </form>
            </div>
          )}

          {/* Move Token Entry Modal (Theme-aware) */}
          {moveTargetEntry && (
            <div
              className="fixed inset-0 z-50 flex items-center justify-center bg-black/55 p-4"
              onMouseDown={(e) => {
                if (e.target === e.currentTarget) setMoveTargetEntry(null);
              }}
            >
              <div className="w-full max-w-sm bg-white dark:bg-[#1E222D] border border-slate-200 dark:border-[#2A2E39] rounded-lg shadow-2xl overflow-hidden">
                <div className="flex items-center justify-between px-4 py-3 border-b border-slate-200 dark:border-[#2A2E39]">
                  <div>
                    <h3 className="font-display text-sm font-semibold text-slate-900 dark:text-white">
                      Déplacer vers une section
                    </h3>
                    <p className="text-[11px] text-slate-500 dark:text-[#787B86]">
                      {moveTargetEntry.displayName?.trim() || moveTargetEntry.symbol}
                    </p>
                  </div>
                  <button
                    type="button"
                    onClick={() => setMoveTargetEntry(null)}
                    className="p-1 text-slate-500 hover:text-slate-900 dark:hover:text-white cursor-pointer"
                  >
                    <X className="w-4 h-4" />
                  </button>
                </div>
                <div className="p-3 space-y-1.5">
                  {watchlistSections.map((sec) => {
                    const isCurrent = sec.id === moveTargetEntry.sectionId;
                    return (
                      <button
                        key={sec.id}
                        type="button"
                        onClick={() => {
                          onMoveEntry(moveTargetEntry.id, sec.id);
                          setMoveTargetEntry(null);
                        }}
                        className={`w-full flex items-center justify-between px-3 py-2 text-xs rounded border transition-colors cursor-pointer ${
                          isCurrent
                            ? 'border-[#2962FF] bg-[#2962FF]/12 text-[#2962FF] font-semibold'
                            : 'border-slate-200 dark:border-[#2A2E39] bg-slate-50 dark:bg-[#131722] text-slate-900 dark:text-white hover:border-[#2962FF]'
                        }`}
                      >
                        <span>{sec.name}</span>
                        {isCurrent && <span className="text-[10px]">Actuelle</span>}
                      </button>
                    );
                  })}
                </div>
              </div>
            </div>
          )}
        </div>
      )}

      {/* ALERTS TAB */}
      {activeTab === 'alerts' && (
        <div className="flex-1 flex flex-col min-h-0 overflow-y-auto divide-y divide-slate-200 dark:divide-[#2A2E39]">
          {/* Rule Builder */}
          <form onSubmit={handleCreateRule} className="p-3 space-y-2.5">
            <div className="flex items-center justify-between">
              <h3 className="text-xs font-semibold text-slate-900 dark:text-white">
                Nouvelle Alerte Multi-Conditions ({activeSymbol})
              </h3>
              <button
                type="button"
                onClick={() => setRuleSound((s) => !s)}
                title={ruleSound ? 'Son activé' : 'Son désactivé'}
                className="p-1 text-slate-500 hover:text-slate-900 dark:hover:text-white cursor-pointer"
              >
                {ruleSound ? <Volume2 className="w-3.5 h-3.5" /> : <VolumeX className="w-3.5 h-3.5" />}
              </button>
            </div>

            <div className="flex items-center gap-2">
              <input
                type="text"
                placeholder="Nom de la règle"
                value={ruleName}
                onChange={(e) => setRuleName(e.target.value)}
                className="flex-1 px-2 py-1.5 text-xs bg-white dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
              />
              <select
                value={ruleLogic}
                onChange={(e) => setRuleLogic(e.target.value as 'AND' | 'OR')}
                className="px-2 py-1.5 text-xs font-semibold bg-white dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
              >
                <option value="AND">ET (AND)</option>
                <option value="OR">OU (OR)</option>
              </select>
            </div>

            <div className="space-y-2">
              {draftConditions.map((cond, idx) => {
                const leftMeta = METRIC_OPTIONS.find((m) => m.value === cond.leftMetric);
                const rightMeta = METRIC_OPTIONS.find((m) => m.value === cond.rightMetric);
                return (
                  <div
                    key={cond.id}
                    className="p-2 rounded border border-slate-200 dark:border-[#2A2E39] bg-white dark:bg-[#131722] space-y-1.5"
                  >
                    <div className="flex items-center justify-between text-[11px] text-slate-500 dark:text-[#787B86]">
                      <span>Condition {idx + 1}</span>
                      {draftConditions.length > 1 && (
                        <button
                          type="button"
                          onClick={() =>
                            setDraftConditions((prev) => prev.filter((c) => c.id !== cond.id))
                          }
                          className="text-rose-500 hover:underline cursor-pointer"
                        >
                          Retirer
                        </button>
                      )}
                    </div>

                    <div className="grid grid-cols-[1fr_auto] gap-1.5">
                      <select
                        value={cond.leftMetric}
                        onChange={(e) => {
                          const val = e.target.value as ConditionMetric;
                          const meta = METRIC_OPTIONS.find((m) => m.value === val);
                          setDraftConditions((prev) =>
                            prev.map((c) =>
                              c.id === cond.id
                                ? {
                                    ...c,
                                    leftMetric: val,
                                    leftParam: meta?.hasParam ? meta.defaultParam : undefined,
                                  }
                                : c
                            )
                          );
                        }}
                        className="px-2 py-1 text-xs bg-slate-50 dark:bg-[#1E222D] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
                      >
                        {METRIC_OPTIONS.map((m) => (
                          <option key={m.value} value={m.value}>
                            {m.label}
                          </option>
                        ))}
                      </select>

                      {leftMeta?.hasParam && (
                        <input
                          type="number"
                          min={2}
                          max={200}
                          value={cond.leftParam ?? leftMeta.defaultParam}
                          onChange={(e) =>
                            setDraftConditions((prev) =>
                              prev.map((c) =>
                                c.id === cond.id
                                  ? { ...c, leftParam: Number(e.target.value) || leftMeta.defaultParam }
                                  : c
                              )
                            )
                          }
                          className="w-14 px-1.5 py-1 text-xs font-mono-tabular bg-slate-50 dark:bg-[#1E222D] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
                        />
                      )}
                    </div>

                    <div className="grid grid-cols-[auto_auto_1fr] gap-1.5 items-center">
                      <select
                        value={cond.operator}
                        onChange={(e) =>
                          setDraftConditions((prev) =>
                            prev.map((c) =>
                              c.id === cond.id
                                ? { ...c, operator: e.target.value as ConditionOperator }
                                : c
                            )
                          )
                        }
                        className="px-1.5 py-1 text-xs bg-slate-50 dark:bg-[#1E222D] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
                      >
                        {OPERATOR_OPTIONS.map((op) => (
                          <option key={op.value} value={op.value}>
                            {op.label}
                          </option>
                        ))}
                      </select>

                      <select
                        value={cond.rightType}
                        onChange={(e) =>
                          setDraftConditions((prev) =>
                            prev.map((c) =>
                              c.id === cond.id
                                ? {
                                    ...c,
                                    rightType: e.target.value as 'value' | 'metric',
                                    rightMetric: c.rightMetric ?? 'ema',
                                    rightParam: c.rightParam ?? 20,
                                    rightValue: c.rightValue ?? 50,
                                  }
                                : c
                            )
                          )
                        }
                        className="px-1.5 py-1 text-xs bg-slate-50 dark:bg-[#1E222D] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
                      >
                        <option value="value">Valeur</option>
                        <option value="metric">Indicateur</option>
                      </select>

                      {cond.rightType === 'value' ? (
                        <input
                          type="number"
                          step="any"
                          value={cond.rightValue ?? 0}
                          onChange={(e) =>
                            setDraftConditions((prev) =>
                              prev.map((c) =>
                                c.id === cond.id
                                  ? { ...c, rightValue: Number(e.target.value) }
                                  : c
                              )
                            )
                          }
                          className="w-full px-2 py-1 text-xs font-mono-tabular bg-slate-50 dark:bg-[#1E222D] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
                        />
                      ) : (
                        <div className="flex items-center gap-1">
                          <select
                            value={cond.rightMetric ?? 'ema'}
                            onChange={(e) => {
                              const val = e.target.value as ConditionMetric;
                              const meta = METRIC_OPTIONS.find((m) => m.value === val);
                              setDraftConditions((prev) =>
                                prev.map((c) =>
                                  c.id === cond.id
                                    ? {
                                        ...c,
                                        rightMetric: val,
                                        rightParam: meta?.hasParam ? meta.defaultParam : undefined,
                                      }
                                    : c
                                )
                              );
                            }}
                            className="flex-1 min-w-0 px-1.5 py-1 text-xs bg-slate-50 dark:bg-[#1E222D] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
                          >
                            {METRIC_OPTIONS.map((m) => (
                              <option key={m.value} value={m.value}>
                                {m.label}
                              </option>
                            ))}
                          </select>
                          {rightMeta?.hasParam && (
                            <input
                              type="number"
                              min={2}
                              max={200}
                              value={cond.rightParam ?? rightMeta.defaultParam}
                              onChange={(e) =>
                                setDraftConditions((prev) =>
                                  prev.map((c) =>
                                    c.id === cond.id
                                      ? {
                                          ...c,
                                          rightParam: Number(e.target.value) || rightMeta.defaultParam,
                                        }
                                      : c
                                  )
                                )
                              }
                              className="w-12 px-1 py-1 text-xs font-mono-tabular bg-slate-50 dark:bg-[#1E222D] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
                            />
                          )}
                        </div>
                      )}
                    </div>
                  </div>
                );
              })}
            </div>

            <div className="flex items-center justify-between gap-2">
              <button
                type="button"
                onClick={handleAddConditionRow}
                className="text-xs font-medium text-[#2962FF] hover:underline cursor-pointer"
              >
                + Ajouter une condition
              </button>
              <label className="flex items-center gap-1 text-[11px] text-slate-500 dark:text-[#787B86]">
                <span>Cooldown (s)</span>
                <input
                  type="number"
                  min={5}
                  max={3600}
                  value={ruleCooldown}
                  onChange={(e) => setRuleCooldown(Number(e.target.value) || 30)}
                  className="w-12 px-1.5 py-0.5 text-xs font-mono-tabular bg-white dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
                />
              </label>
            </div>

            <button
              type="submit"
              className="w-full py-1.5 text-xs font-semibold rounded bg-[#2962FF] hover:bg-[#1E53E5] text-white cursor-pointer"
            >
              Activer l’alerte composée
            </button>
          </form>

          {/* Active Rules */}
          <div className="p-3 space-y-2">
            <div className="text-xs font-semibold text-slate-900 dark:text-white">
              Règles actives ({alerts.length})
            </div>
            {alerts.length === 0 ? (
              <p className="text-xs text-slate-500 dark:text-[#787B86]">
                Aucune alerte configurée. Créez une règle ci-dessus.
              </p>
            ) : (
              <div className="space-y-2">
                {alerts.map((r) => (
                  <div
                    key={r.id}
                    className="p-2.5 rounded border border-slate-200 dark:border-[#2A2E39] bg-white dark:bg-[#131722] space-y-1"
                  >
                    <div className="flex items-center justify-between gap-2">
                      <span className="text-xs font-semibold text-slate-900 dark:text-white truncate">
                        {r.name}
                      </span>
                      <div className="flex items-center gap-2 shrink-0">
                        <input
                          type="checkbox"
                          checked={r.enabled}
                          onChange={() => onToggleAlertRule(r.id)}
                          aria-label={`Activer ${r.name}`}
                          className="accent-[#2962FF] cursor-pointer"
                        />
                        <button
                          type="button"
                          onClick={() => onDeleteAlertRule(r.id)}
                          className="text-slate-400 hover:text-rose-500 cursor-pointer"
                          title="Supprimer"
                        >
                          <Trash2 className="w-3.5 h-3.5" />
                        </button>
                      </div>
                    </div>
                    <div className="text-[11px] text-slate-500 dark:text-[#787B86]">
                      {r.symbol} · {r.triggerCount} déclenchement(s)
                    </div>
                    <div className="text-[11px] font-mono-tabular text-slate-700 dark:text-[#D1D4DC] break-words">
                      {r.conditions.map(formatConditionSummary).join(` ${r.logic} `)}
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>

          {/* Alert Trigger History */}
          <div className="p-3 space-y-2">
            <div className="flex items-center justify-between">
              <span className="text-xs font-semibold text-slate-900 dark:text-white">
                Journal des déclenchements ({alertLogs.length})
              </span>
              {alertLogs.length > 0 && (
                <button
                  type="button"
                  onClick={onClearAlertLogs}
                  className="text-[11px] text-slate-500 hover:text-slate-900 dark:hover:text-white cursor-pointer"
                >
                  Vider
                </button>
              )}
            </div>
            {alertLogs.length === 0 ? (
              <p className="text-xs text-slate-500 dark:text-[#787B86]">
                Aucun signal déclenché pour le moment.
              </p>
            ) : (
              <div className="space-y-1.5">
                {alertLogs.slice(0, 20).map((log) => (
                  <div
                    key={log.id}
                    className="py-1.5 px-2 rounded bg-white dark:bg-[#131722] border border-slate-200 dark:border-[#2A2E39] text-[11px]"
                  >
                    <div className="flex items-center justify-between font-semibold text-slate-900 dark:text-white">
                      <span>{log.ruleName}</span>
                      <span className="font-mono-tabular text-[#089981]">
                        {formatQuote(log.price)}
                      </span>
                    </div>
                    <div className="text-slate-500 dark:text-[#787B86] font-mono-tabular">
                      {log.symbol} · {new Date(log.timestamp).toLocaleTimeString('fr-FR')}
                    </div>
                  </div>
                ))}
              </div>
            )}
          </div>
        </div>
      )}

      {/* MULTI-SOURCE SYNC TAB */}
      {activeTab === 'sources' && (
        <div className="flex-1 overflow-y-auto p-3 space-y-4">
          <div>
            <h3 className="text-xs font-semibold text-slate-900 dark:text-white mb-1">
              Gestionnaire de Flux Asynchrones Centralisé
            </h3>
            <p className="text-xs text-slate-500 dark:text-[#787B86] leading-relaxed">
              Synchronisation temps réel des flux Binance, Coinbase Exchange et Kraken sur une grille UTC commune.
            </p>
          </div>

          <div className="space-y-2.5">
            {sources.map((src) => {
              const isPrimary = primarySource === src.id;
              const isOverlay = overlaySources.includes(src.id);
              const quote = activeToken?.sourcePrices[src.id];

              return (
                <div
                  key={src.id}
                  className="p-3 rounded border border-slate-200 dark:border-[#2A2E39] bg-white dark:bg-[#131722] space-y-2"
                >
                  <div className="flex items-center justify-between">
                    <div className="flex items-center gap-2">
                      <span
                        className={`w-2 h-2 rounded-full ${
                          src.state === 'live'
                            ? 'bg-[#089981]'
                            : src.state === 'fallback'
                              ? 'bg-amber-500'
                              : 'bg-slate-400'
                        }`}
                      />
                      <span className="text-xs font-semibold text-slate-900 dark:text-white">
                        {src.label}
                      </span>
                    </div>
                    <span className="text-[11px] font-mono-tabular text-slate-500 dark:text-[#787B86]">
                      {src.modeLabel} · {src.latencyMs} ms
                    </span>
                  </div>

                  <div className="flex items-center justify-between text-xs font-mono-tabular">
                    <span className="text-slate-500 dark:text-[#787B86]">{activeSymbol}</span>
                    <span className="font-semibold text-slate-900 dark:text-white">
                      {quote ? formatQuote(quote) : '—'}
                    </span>
                  </div>

                  <div className="flex items-center justify-between pt-1 border-t border-slate-200/70 dark:border-[#2A2E39] gap-2">
                    <button
                      type="button"
                      onClick={() => onChangePrimarySource(src.id)}
                      className={`px-2.5 py-1 text-[11px] font-semibold rounded transition-colors cursor-pointer ${
                        isPrimary
                          ? 'bg-[#2962FF] text-white'
                          : 'bg-slate-100 dark:bg-[#1E222D] text-slate-700 dark:text-[#D1D4DC] hover:bg-slate-200 dark:hover:bg-[#2A2E39]'
                      }`}
                    >
                      {isPrimary ? 'Source principale' : 'Définir principale'}
                    </button>

                    {!isPrimary && (
                      <label className="flex items-center gap-1.5 text-[11px] text-slate-600 dark:text-[#D1D4DC] cursor-pointer">
                        <input
                          type="checkbox"
                          checked={isOverlay}
                          onChange={() => onToggleOverlaySource(src.id)}
                          className="accent-[#2962FF]"
                        />
                        <span>Superposer</span>
                      </label>
                    )}
                  </div>
                </div>
              );
            })}
          </div>
        </div>
      )}
    </aside>
  );
};
