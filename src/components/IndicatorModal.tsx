import React, { useState } from 'react';
import { Eye, EyeOff, Plus, Trash2, X } from 'lucide-react';
import { IndicatorConfig, IndicatorType } from '../types/trading';

interface IndicatorModalProps {
  open: boolean;
  onClose: () => void;
  indicators: IndicatorConfig[];
  onToggleIndicator: (id: string) => void;
  onUpdateParam: (id: string, paramKey: string, value: number) => void;
  onUpdateColor: (id: string, color: string) => void;
  onAddIndicator: (type: IndicatorType, period: number) => void;
  onRemoveIndicator: (id: string) => void;
}

export const IndicatorModal: React.FC<IndicatorModalProps> = ({
  open,
  onClose,
  indicators,
  onToggleIndicator,
  onUpdateParam,
  onUpdateColor,
  onAddIndicator,
  onRemoveIndicator,
}) => {
  const [newType, setNewType] = useState<IndicatorType>('EMA');
  const [newPeriod, setNewPeriod] = useState<number>(50);

  if (!open) return null;

  return (
    <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/55 p-4">
      <div className="w-full max-w-xl bg-white dark:bg-[#1E222D] border border-slate-200 dark:border-[#2A2E39] rounded-lg shadow-2xl overflow-hidden">
        <div className="flex items-center justify-between px-5 py-3.5 border-b border-slate-200 dark:border-[#2A2E39]">
          <h2 className="font-display text-sm font-semibold text-slate-900 dark:text-white">
            Indicateurs Techniques Personnalisables
          </h2>
          <button
            type="button"
            onClick={onClose}
            aria-label="Fermer"
            className="p-1 rounded text-slate-500 hover:text-slate-900 dark:hover:text-white cursor-pointer"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        <div className="p-5 max-h-[65vh] overflow-y-auto space-y-3">
          {indicators.map((ind) => (
            <div
              key={ind.id}
              className="flex flex-col sm:flex-row sm:items-center justify-between gap-3 py-2.5 px-3 rounded border border-slate-200 dark:border-[#2A2E39] bg-slate-50/70 dark:bg-[#131722]"
            >
              <div className="flex items-center gap-3">
                <button
                  type="button"
                  onClick={() => onToggleIndicator(ind.id)}
                  className={`p-1.5 rounded cursor-pointer ${
                    ind.enabled
                      ? 'text-[#2962FF] bg-[#2962FF]/10'
                      : 'text-slate-400 hover:text-slate-600'
                  }`}
                  title={ind.enabled ? 'Masquer' : 'Afficher'}
                >
                  {ind.enabled ? <Eye className="w-4 h-4" /> : <EyeOff className="w-4 h-4" />}
                </button>

                <input
                  type="color"
                  value={ind.color}
                  onChange={(e) => onUpdateColor(ind.id, e.target.value)}
                  className="w-5 h-5 rounded cursor-pointer bg-transparent border-0 p-0"
                  title="Couleur"
                />

                <div>
                  <div className="text-xs font-semibold text-slate-900 dark:text-white">
                    {ind.label}
                  </div>
                  <div className="text-[11px] text-slate-500 dark:text-[#787B86]">
                    {ind.pane === 'main' ? 'Graphique principal' : `Sous-panneau ${ind.pane.toUpperCase()}`}
                  </div>
                </div>
              </div>

              <div className="flex items-center gap-2 flex-wrap">
                {Object.entries(ind.params).map(([key, val]) => (
                  <label key={key} className="flex items-center gap-1.5 text-[11px] text-slate-600 dark:text-[#787B86]">
                    <span>{key}</span>
                    <input
                      type="number"
                      step={key === 'stdDev' ? '0.5' : '1'}
                      min={key === 'stdDev' ? 0.5 : 2}
                      max={300}
                      value={val}
                      onChange={(e) => {
                        const num = Number(e.target.value);
                        if (Number.isFinite(num) && num > 0) {
                          onUpdateParam(ind.id, key, num);
                        }
                      }}
                      className="w-14 px-1.5 py-1 text-xs font-mono-tabular bg-white dark:bg-[#1E222D] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
                    />
                  </label>
                ))}

                {!['ema_12', 'ema_26', 'sma_50', 'bb_20', 'rsi_14', 'macd_std'].includes(ind.id) && (
                  <button
                    type="button"
                    onClick={() => onRemoveIndicator(ind.id)}
                    className="p-1 text-slate-400 hover:text-rose-500 cursor-pointer"
                    title="Supprimer"
                  >
                    <Trash2 className="w-3.5 h-3.5" />
                  </button>
                )}
              </div>
            </div>
          ))}

          {/* Add custom moving average */}
          <div className="pt-3 border-t border-slate-200 dark:border-[#2A2E39] flex flex-wrap items-center gap-2">
            <span className="text-xs font-medium text-slate-700 dark:text-[#D1D4DC]">
              Ajouter une courbe :
            </span>
            <select
              value={newType}
              onChange={(e) => setNewType(e.target.value as IndicatorType)}
              className="px-2 py-1 text-xs bg-slate-50 dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
            >
              <option value="EMA">EMA</option>
              <option value="SMA">SMA</option>
            </select>
            <input
              type="number"
              min={2}
              max={300}
              value={newPeriod}
              onChange={(e) => setNewPeriod(Math.max(2, Number(e.target.value) || 20))}
              className="w-16 px-2 py-1 text-xs font-mono-tabular bg-slate-50 dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white"
            />
            <button
              type="button"
              onClick={() => onAddIndicator(newType, newPeriod)}
              className="flex items-center gap-1 px-3 py-1 text-xs font-semibold rounded bg-[#2962FF] hover:bg-[#1E53E5] text-white cursor-pointer"
            >
              <Plus className="w-3.5 h-3.5" />
              <span>Ajouter</span>
            </button>
          </div>
        </div>
      </div>
    </div>
  );
};
