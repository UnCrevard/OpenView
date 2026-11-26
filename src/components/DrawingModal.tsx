import React, { useEffect, useState } from 'react';
import { Lock, Trash2, Unlock, X } from 'lucide-react';
import {
  ChartDrawing,
  DrawingLabelAlignH,
  DrawingLabelAlignV,
  DrawingLineStyle,
} from '../types/trading';

interface DrawingModalProps {
  drawing: ChartDrawing | null;
  onClose: () => void;
  onSave: (updated: ChartDrawing) => void;
  onDelete: (id: string) => void;
}

const TOOL_LABELS: Record<ChartDrawing['tool'], string> = {
  hline: 'Ligne horizontale',
  trendline: 'Ligne de tendance',
  fib: 'Retracement Fibonacci',
  rect: 'Rectangle de zone',
  channel: 'Canal parallèle',
  text: 'Annotation texte',
};

const LINE_STYLES: Array<{ value: DrawingLineStyle; label: string }> = [
  { value: 'solid', label: 'Plein' },
  { value: 'dashed', label: 'Tirets' },
  { value: 'dotted', label: 'Pointillés' },
];

const ALIGN_H_OPTIONS: Array<{ value: DrawingLabelAlignH; label: string }> = [
  { value: 'left', label: 'Gauche' },
  { value: 'center', label: 'Centre' },
  { value: 'right', label: 'Droite' },
];

const ALIGN_V_OPTIONS: Array<{ value: DrawingLabelAlignV; label: string }> = [
  { value: 'top', label: 'Au-dessus' },
  { value: 'middle', label: 'Milieu' },
  { value: 'bottom', label: 'En dessous' },
];

const COLOR_SWATCHES: Array<{ value: string; name: string }> = [
  { value: '#2962FF', name: 'Bleu' },
  { value: '#F59E0B', name: 'Ambre' },
  { value: '#089981', name: 'Vert' },
  { value: '#F23645', name: 'Rouge' },
  { value: '#A855F7', name: 'Violet' },
  { value: '#38BDF8', name: 'Cyan' },
  { value: '#E2E8F0', name: 'Ardoise' },
];

export const DrawingModal: React.FC<DrawingModalProps> = ({
  drawing,
  onClose,
  onSave,
  onDelete,
}) => {
  const [color, setColor] = useState('#2962FF');
  const [lineWidth, setLineWidth] = useState(2);
  const [lineStyle, setLineStyle] = useState<DrawingLineStyle>('solid');
  const [text, setText] = useState('');
  const [labelAlignH, setLabelAlignH] = useState<DrawingLabelAlignH>('left');
  const [labelAlignV, setLabelAlignV] = useState<DrawingLabelAlignV>('top');
  const [locked, setLocked] = useState(false);
  const [prices, setPrices] = useState<string[]>([]);

  useEffect(() => {
    if (!drawing) return;
    setColor(drawing.color);
    setLineWidth(drawing.lineWidth || 2);
    setLineStyle(drawing.lineStyle ?? 'solid');
    setText(drawing.text ?? '');
    setLabelAlignH(
      drawing.labelAlignH ??
        (drawing.tool === 'trendline' || drawing.tool === 'channel' ? 'center' : 'left')
    );
    setLabelAlignV(drawing.labelAlignV ?? 'top');
    setLocked(Boolean(drawing.locked));
    setPrices(drawing.points.map((p) => String(Number(p.price.toFixed(6)))));
  }, [drawing]);

  if (!drawing) return null;

  const handleSubmit = (e: React.FormEvent) => {
    e.preventDefault();
    const updatedPoints = drawing.points.map((pt, idx) => {
      const parsed = Number(prices[idx]);
      return {
        ...pt,
        price: Number.isFinite(parsed) && parsed > 0 ? parsed : pt.price,
      };
    });

    onSave({
      ...drawing,
      color,
      lineWidth: Math.max(1, Math.min(6, lineWidth)),
      lineStyle,
      text: text.trim() || undefined,
      labelAlignH,
      labelAlignV,
      locked,
      points: updatedPoints,
    });
    onClose();
  };

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/55 p-4"
      onMouseDown={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
    >
      <form
        onSubmit={handleSubmit}
        className="w-full max-w-md bg-white dark:bg-[#1E222D] border border-slate-200 dark:border-[#2A2E39] rounded-lg shadow-2xl overflow-hidden"
      >
        <div className="flex items-center justify-between px-5 py-3.5 border-b border-slate-200 dark:border-[#2A2E39]">
          <div>
            <h2 className="font-display text-sm font-semibold text-slate-900 dark:text-white">
              Propriétés du dessin
            </h2>
            <p className="text-[11px] text-slate-500 dark:text-[#787B86]">
              {TOOL_LABELS[drawing.tool]} · {drawing.symbol}
            </p>
          </div>
          <button
            type="button"
            onClick={onClose}
            aria-label="Fermer"
            className="p-1 rounded text-slate-500 hover:text-slate-900 dark:hover:text-white cursor-pointer"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        <div className="p-5 space-y-4">
          {/* Price / Value coordinates */}
          <div className="space-y-2.5">
            <div className="text-xs font-semibold text-slate-900 dark:text-white">
              {drawing.tool === 'hline' ? 'Valeur de prix' : 'Coordonnées (Prix)'}
            </div>
            <div className="grid grid-cols-1 sm:grid-cols-2 gap-2.5">
              {drawing.points.map((pt, idx) => (
                <label key={idx} className="space-y-1">
                  <span className="block text-[11px] text-slate-500 dark:text-[#787B86]">
                    {drawing.tool === 'hline'
                      ? 'Niveau ($)'
                      : drawing.tool === 'text'
                        ? 'Prix ancrage'
                        : `Point ${idx + 1} (${new Date(pt.time * 1000).toLocaleString('fr-FR', { day: '2-digit', month: '2-digit', year: '2-digit', hour: '2-digit', minute: '2-digit' })})`}
                  </span>
                  <input
                    type="number"
                    step="any"
                    min="0"
                    value={prices[idx] ?? ''}
                    onChange={(e) => {
                      const next = [...prices];
                      next[idx] = e.target.value;
                      setPrices(next);
                    }}
                    className="w-full px-2.5 py-1.5 text-xs font-mono-tabular bg-slate-50 dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white focus:outline-none focus:border-[#2962FF]"
                  />
                </label>
              ))}
            </div>
          </div>

          {/* True Color swatches (no RGB codes) */}
          <div className="space-y-2.5">
            <div className="text-xs font-semibold text-slate-900 dark:text-white">
              Couleur
            </div>
            <div className="flex items-center gap-2.5 flex-wrap">
              {COLOR_SWATCHES.map((sw) => (
                <button
                  key={sw.value}
                  type="button"
                  title={sw.name}
                  onClick={() => setColor(sw.value)}
                  className={`w-6 h-6 rounded-full cursor-pointer transition-transform border border-black/15 dark:border-white/20 ${
                    color.toLowerCase() === sw.value.toLowerCase()
                      ? 'ring-2 ring-offset-2 ring-offset-white dark:ring-offset-[#1E222D] ring-[#2962FF] scale-110'
                      : 'hover:scale-105'
                  }`}
                  style={{ backgroundColor: sw.value }}
                  aria-label={sw.name}
                />
              ))}
              <label
                title="Autre couleur"
                className="relative w-6 h-6 rounded-full overflow-hidden border border-slate-300 dark:border-[#3A4152] cursor-pointer flex items-center justify-center"
                style={{ backgroundColor: color }}
              >
                <input
                  type="color"
                  value={color}
                  onChange={(e) => setColor(e.target.value)}
                  aria-label="Autre couleur"
                  className="opacity-0 absolute inset-0 w-full h-full cursor-pointer"
                />
                <span className="text-[11px] font-bold text-white drop-shadow pointer-events-none">
                  +
                </span>
              </label>
            </div>
          </div>

          {drawing.tool !== 'text' && (
            <div className="grid grid-cols-2 gap-3">
              <div className="space-y-1.5">
                <label className="block text-xs font-semibold text-slate-900 dark:text-white">
                  Type de trait
                </label>
                <div className="flex items-center bg-slate-100 dark:bg-[#131722] rounded p-0.5">
                  {LINE_STYLES.map((st) => (
                    <button
                      key={st.value}
                      type="button"
                      onClick={() => setLineStyle(st.value)}
                      className={`flex-1 py-1 text-[11px] font-medium rounded transition-colors cursor-pointer whitespace-nowrap ${
                        lineStyle === st.value
                          ? 'bg-[#2962FF] text-white'
                          : 'text-slate-600 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-white'
                      }`}
                    >
                      {st.label}
                    </button>
                  ))}
                </div>
              </div>

              <div className="space-y-1.5">
                <label className="block text-xs font-semibold text-slate-900 dark:text-white">
                  Épaisseur ({lineWidth} px)
                </label>
                <input
                  type="range"
                  min={1}
                  max={5}
                  step={0.5}
                  value={lineWidth}
                  onChange={(e) => setLineWidth(Number(e.target.value))}
                  className="w-full accent-[#2962FF] cursor-pointer mt-1.5"
                />
              </div>
            </div>
          )}

          {/* Description + Alignment (Gauche / Centre / Droite & Au-dessus / Milieu / En dessous) */}
          <div className="space-y-2.5">
            <label className="block space-y-1.5">
              <span className="block text-xs font-semibold text-slate-900 dark:text-white">
                {drawing.tool === 'text' ? 'Texte de l’annotation' : 'Description (optionnelle)'}
              </span>
              <input
                type="text"
                placeholder={drawing.tool === 'text' ? 'Votre note...' : 'Ex. Support majeur'}
                value={text}
                onChange={(e) => setText(e.target.value)}
                className="w-full px-2.5 py-1.5 text-xs bg-slate-50 dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white focus:outline-none focus:border-[#2962FF]"
              />
            </label>

            <div className="grid grid-cols-2 gap-3">
              <div className="space-y-1">
                <span className="block text-[11px] font-medium text-slate-500 dark:text-[#787B86]">
                  Alignement horizontal
                </span>
                <div className="flex items-center bg-slate-100 dark:bg-[#131722] rounded p-0.5">
                  {ALIGN_H_OPTIONS.map((opt) => (
                    <button
                      key={opt.value}
                      type="button"
                      onClick={() => setLabelAlignH(opt.value)}
                      className={`flex-1 py-1 text-[11px] font-medium rounded transition-colors cursor-pointer whitespace-nowrap ${
                        labelAlignH === opt.value
                          ? 'bg-[#2962FF] text-white'
                          : 'text-slate-600 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-white'
                      }`}
                    >
                      {opt.label}
                    </button>
                  ))}
                </div>
              </div>

              <div className="space-y-1">
                <span className="block text-[11px] font-medium text-slate-500 dark:text-[#787B86]">
                  Position verticale
                </span>
                <div className="flex items-center bg-slate-100 dark:bg-[#131722] rounded p-0.5">
                  {ALIGN_V_OPTIONS.map((opt) => (
                    <button
                      key={opt.value}
                      type="button"
                      onClick={() => setLabelAlignV(opt.value)}
                      className={`flex-1 py-1 text-[11px] font-medium rounded transition-colors cursor-pointer whitespace-nowrap ${
                        labelAlignV === opt.value
                          ? 'bg-[#2962FF] text-white'
                          : 'text-slate-600 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-white'
                      }`}
                    >
                      {opt.label}
                    </button>
                  ))}
                </div>
              </div>
            </div>
          </div>

          {/* Lock toggle */}
          <label className="flex items-center justify-between pt-2 border-t border-slate-200 dark:border-[#2A2E39] cursor-pointer">
            <span className="flex items-center gap-2 text-xs text-slate-700 dark:text-[#D1D4DC]">
              {locked ? <Lock className="w-3.5 h-3.5 text-amber-500" /> : <Unlock className="w-3.5 h-3.5" />}
              <span>Verrouiller la position sur le graphique</span>
            </span>
            <input
              type="checkbox"
              checked={locked}
              onChange={(e) => setLocked(e.target.checked)}
              className="accent-[#2962FF] cursor-pointer"
            />
          </label>
        </div>

        <div className="flex items-center justify-between px-5 py-3 border-t border-slate-200 dark:border-[#2A2E39] bg-slate-50 dark:bg-[#131722]">
          <button
            type="button"
            onClick={() => {
              onDelete(drawing.id);
              onClose();
            }}
            className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium text-rose-500 hover:bg-rose-500/10 rounded cursor-pointer"
          >
            <Trash2 className="w-3.5 h-3.5" />
            <span>Supprimer</span>
          </button>

          <div className="flex items-center gap-2">
            <button
              type="button"
              onClick={onClose}
              className="px-3 py-1.5 text-xs font-medium text-slate-600 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-white cursor-pointer"
            >
              Annuler
            </button>
            <button
              type="submit"
              className="px-4 py-1.5 text-xs font-semibold rounded bg-[#2962FF] hover:bg-[#1E53E5] text-white cursor-pointer"
            >
              Appliquer
            </button>
          </div>
        </div>
      </form>
    </div>
  );
};
