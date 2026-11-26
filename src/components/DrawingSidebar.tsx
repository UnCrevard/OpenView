import React from 'react';
import {
  Crosshair,
  GitBranch,
  Minus,
  MousePointer,
  Percent,
  RotateCcw,
  Square,
  Trash2,
  TrendingUp,
  Type,
} from 'lucide-react';
import { DrawingToolType } from '../types/trading';

interface DrawingSidebarProps {
  activeTool: DrawingToolType;
  onSelectTool: (tool: DrawingToolType) => void;
  drawingColor: string;
  onChangeColor: (color: string) => void;
  onUndoDrawing: () => void;
  onClearDrawings: () => void;
  drawingCount: number;
  selectedDrawingId: string | null;
  onDeleteSelected: () => void;
}

const TOOLS: Array<{
  id: DrawingToolType;
  label: string;
  icon: React.FC<{ className?: string }>;
}> = [
  { id: 'cursor', label: 'Réticule & Sélection (Esc)', icon: Crosshair },
  { id: 'trendline', label: 'Ligne de tendance', icon: TrendingUp },
  { id: 'hline', label: 'Ligne horizontale', icon: Minus },
  { id: 'fib', label: 'Retracement Fibonacci', icon: Percent },
  { id: 'channel', label: 'Canal parallèle', icon: GitBranch },
  { id: 'rect', label: 'Rectangle de zone', icon: Square },
  { id: 'text', label: 'Annotation texte', icon: Type },
];

const PALETTE = ['#2962FF', '#F59E0B', '#089981', '#F23645', '#A855F7'];

export const DrawingSidebar: React.FC<DrawingSidebarProps> = ({
  activeTool,
  onSelectTool,
  drawingColor,
  onChangeColor,
  onUndoDrawing,
  onClearDrawings,
  drawingCount,
  selectedDrawingId,
  onDeleteSelected,
}) => {
  return (
    <aside
      aria-label="Outils de dessin"
      className="w-11 shrink-0 flex flex-col items-center justify-between py-2 border-r border-slate-200 dark:border-[#2A2E39] bg-slate-50 dark:bg-[#1E222D] select-none"
    >
      <div className="flex flex-col items-center gap-1">
        {TOOLS.map((tool) => {
          const Icon = tool.icon;
          const active = activeTool === tool.id;
          return (
            <button
              key={tool.id}
              type="button"
              title={tool.label}
              aria-label={tool.label}
              onClick={() => onSelectTool(tool.id)}
              className={`w-8 h-8 flex items-center justify-center rounded transition-colors cursor-pointer ${
                active
                  ? 'bg-[#2962FF] text-white'
                  : 'text-slate-600 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-[#D1D4DC] hover:bg-slate-200/70 dark:hover:bg-[#2A2E39]'
              }`}
            >
              {tool.id === 'cursor' && selectedDrawingId ? (
                <MousePointer className="w-4 h-4" />
              ) : (
                <Icon className="w-4 h-4" />
              )}
            </button>
          );
        })}

        <div className="w-6 h-px bg-slate-200 dark:bg-[#2A2E39] my-1.5" />

        <div className="flex flex-col items-center gap-1.5 py-1">
          {PALETTE.map((c) => (
            <button
              key={c}
              type="button"
              title={`Couleur ${c}`}
              aria-label={`Couleur de tracé ${c}`}
              onClick={() => onChangeColor(c)}
              className={`w-4 h-4 rounded-full transition-transform cursor-pointer ${
                drawingColor === c ? 'ring-2 ring-offset-1 ring-slate-900 dark:ring-white scale-110' : ''
              }`}
              style={{ backgroundColor: c }}
            />
          ))}
        </div>
      </div>

      <div className="flex flex-col items-center gap-1">
        {selectedDrawingId && (
          <button
            type="button"
            title="Supprimer le dessin sélectionné (Suppr)"
            aria-label="Supprimer le dessin sélectionné"
            onClick={onDeleteSelected}
            className="w-8 h-8 flex items-center justify-center rounded text-rose-500 hover:bg-rose-500/10 transition-colors cursor-pointer"
          >
            <Trash2 className="w-4 h-4" />
          </button>
        )}

        <button
          type="button"
          title="Annuler le dernier dessin"
          aria-label="Annuler le dernier dessin"
          disabled={drawingCount === 0}
          onClick={onUndoDrawing}
          className="w-8 h-8 flex items-center justify-center rounded text-slate-600 dark:text-[#787B86] hover:text-slate-900 dark:hover:text-[#D1D4DC] hover:bg-slate-200/70 dark:hover:bg-[#2A2E39] disabled:opacity-30 cursor-pointer disabled:cursor-not-allowed"
        >
          <RotateCcw className="w-4 h-4" />
        </button>

        <button
          type="button"
          title={`Effacer tous les dessins (${drawingCount})`}
          aria-label="Effacer tous les dessins"
          disabled={drawingCount === 0}
          onClick={onClearDrawings}
          className="w-8 h-8 flex items-center justify-center rounded text-slate-600 dark:text-[#787B86] hover:text-rose-500 hover:bg-rose-500/10 disabled:opacity-30 cursor-pointer disabled:cursor-not-allowed"
        >
          <Trash2 className="w-4 h-4" />
        </button>
      </div>
    </aside>
  );
};
