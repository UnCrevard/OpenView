import React, { useState } from 'react';
import { Check, Copy, Download, FileCode2, Search } from 'lucide-react';
import { C_WIN32_PROJECT_FILES, CProjectFile } from '../c_win32/cProjectFiles';

interface CSourceExplorerProps {
  onDownloadZip: () => void;
}

export const CSourceExplorer: React.FC<CSourceExplorerProps> = ({ onDownloadZip }) => {
  const [selectedPath, setSelectedPath] = useState<string>('src/main.c');
  const [filter, setFilter] = useState('');
  const [copied, setCopied] = useState(false);

  const visibleFiles = C_WIN32_PROJECT_FILES.filter(
    (f) =>
      f.path.toLowerCase().includes(filter.toLowerCase()) ||
      f.summary.toLowerCase().includes(filter.toLowerCase())
  );

  const activeFile: CProjectFile =
    C_WIN32_PROJECT_FILES.find((f) => f.path === selectedPath) ?? C_WIN32_PROJECT_FILES[0];

  const handleCopy = async () => {
    try {
      await navigator.clipboard.writeText(activeFile.content);
      setCopied(true);
      setTimeout(() => setCopied(false), 1800);
    } catch {
      // ignore clipboard errors
    }
  };

  const categories: Array<{ id: CProjectFile['category']; label: string }> = [
    { id: 'build', label: 'Build & CMake' },
    { id: 'header', label: 'En-têtes (include/)' },
    { id: 'source', label: 'Sources C11 (src/)' },
    { id: 'docs', label: 'Documentation' },
  ];

  return (
    <div className="flex-1 flex min-h-0 bg-white dark:bg-[#131722]">
      {/* Left file explorer */}
      <aside className="w-72 shrink-0 flex flex-col border-r border-slate-200 dark:border-[#2A2E39] bg-slate-50 dark:bg-[#1E222D]">
        <div className="p-3 border-b border-slate-200 dark:border-[#2A2E39] space-y-2.5">
          <div className="flex items-center justify-between">
            <h2 className="font-display text-xs font-semibold text-slate-900 dark:text-white">
              Projet C11 Win32 Unicode
            </h2>
            <span className="text-[11px] font-mono-tabular text-slate-500 dark:text-[#787B86]">
              {C_WIN32_PROJECT_FILES.length} fichiers
            </span>
          </div>

          <div className="relative">
            <Search className="w-3.5 h-3.5 absolute left-2.5 top-1/2 -translate-y-1/2 text-slate-400" />
            <input
              type="text"
              placeholder="Rechercher un module..."
              value={filter}
              onChange={(e) => setFilter(e.target.value)}
              className="w-full pl-8 pr-2.5 py-1.5 text-xs bg-white dark:bg-[#131722] border border-slate-300 dark:border-[#2A2E39] rounded text-slate-900 dark:text-white focus:outline-none focus:border-[#2962FF]"
            />
          </div>

          <button
            type="button"
            onClick={onDownloadZip}
            className="w-full flex items-center justify-center gap-1.5 py-1.5 px-3 text-xs font-semibold rounded bg-[#2962FF] hover:bg-[#1E53E5] text-white cursor-pointer"
          >
            <Download className="w-3.5 h-3.5" />
            <span>Télécharger le projet complet (.ZIP)</span>
          </button>
        </div>

        <div className="flex-1 overflow-y-auto p-2 space-y-3">
          {categories.map((cat) => {
            const files = visibleFiles.filter((f) => f.category === cat.id);
            if (files.length === 0) return null;
            return (
              <div key={cat.id} className="space-y-0.5">
                <div className="px-2 py-1 text-[11px] font-semibold text-slate-500 dark:text-[#787B86]">
                  {cat.label}
                </div>
                {files.map((f) => {
                  const isActive = f.path === activeFile.path;
                  return (
                    <button
                      key={f.path}
                      type="button"
                      onClick={() => setSelectedPath(f.path)}
                      className={`w-full flex items-center gap-2 px-2.5 py-1.5 rounded text-left text-xs transition-colors cursor-pointer ${
                        isActive
                          ? 'bg-[#2962FF]/15 text-[#2962FF] dark:text-white font-semibold'
                          : 'text-slate-700 dark:text-[#D1D4DC] hover:bg-slate-200/70 dark:hover:bg-[#2A2E39]'
                      }`}
                    >
                      <FileCode2 className="w-3.5 h-3.5 shrink-0 opacity-70" />
                      <span className="font-mono-tabular truncate">{f.path}</span>
                    </button>
                  );
                })}
              </div>
            );
          })}
        </div>
      </aside>

      {/* Main Code Viewer */}
      <main className="flex-1 flex flex-col min-w-0 min-h-0">
        <div className="flex items-center justify-between gap-4 px-5 py-3 border-b border-slate-200 dark:border-[#2A2E39] bg-slate-50/50 dark:bg-[#1E222D]/60">
          <div className="min-w-0">
            <div className="font-mono-tabular text-xs font-semibold text-slate-900 dark:text-white truncate">
              OpenViewWin32 / {activeFile.path}
            </div>
            <div className="text-xs text-slate-500 dark:text-[#787B86] truncate">
              {activeFile.summary}
            </div>
          </div>

          <button
            type="button"
            onClick={handleCopy}
            className="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium rounded border border-slate-300 dark:border-[#2A2E39] bg-white dark:bg-[#131722] text-slate-700 dark:text-[#D1D4DC] hover:bg-slate-100 dark:hover:bg-[#2A2E39] shrink-0 cursor-pointer"
          >
            {copied ? <Check className="w-3.5 h-3.5 text-[#089981]" /> : <Copy className="w-3.5 h-3.5" />}
            <span>{copied ? 'Copié' : 'Copier'}</span>
          </button>
        </div>

        <div className="flex-1 overflow-auto p-5 bg-white dark:bg-[#0D1117]">
          <pre className="font-mono-tabular text-xs leading-relaxed text-slate-800 dark:text-[#E6EDF3] whitespace-pre">
            {activeFile.content}
          </pre>
        </div>
      </main>
    </div>
  );
};
