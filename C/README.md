# OpenView Win32 — Terminal Analytique C11 Unicode / Direct2D / WinHTTP & WinSock2

Application native Windows en **C11 Unicode** reproduisant fidèlement l'ergonomie et les fonctionnalités du terminal **OpenView** (barre supérieure avec sélecteur de token, 9 unités de temps `1m..1A`, indicateurs, sources, export CSV/JSON, thème clair/sombre ; barre latérale de dessin vectoriel avec 7 outils + palette de couleurs ; graphique chandeliers avec sous-panneaux RSI & MACD ; panneau droit à 3 onglets `Watchlist` / `Alertes` / `Sources`).

## Architecture Multi-Threadée

1. **Thread UI (Win32 + Direct2D + DirectWrite)** :
   - Boucle de messages Unicode (`wWinMain`, `GetMessageW` / `DispatchMessageW`) avec support DPI (`PER_MONITOR_AWARE_V2`) et double-clic (`CS_DBLCLKS`).
   - Rendu matériel 60+ FPS via `ID2D1HwndRenderTarget` et `IDWriteFactory` (`COBJMACROS` en C11 pur).
   - Boîtes de dialogue modales natives (`ui_dialogs.c`) sans fichier `.rc` : paramètres d'indicateurs, propriétés d'un dessin au double-clic (prix, couleur, plein/tirets/pointillés, épaisseur, étiquette, verrouillage, suppression), saisie d'annotations texte / tokens et constructeur d'alertes multi-conditions.

2. **Gestionnaire de Flux Asynchrones Centralisé (`feed_manager.c`)** :
   - Chargement historique REST HTTPS via **WinHTTP** (`winhttp.lib`) sur `Binance`, `Coinbase` et `Kraken` (incluant les durées `1S`, `1M`, `1A` avec regroupement calendaire).
   - Un thread dédié par source sur **WinHTTP WebSocket TLS** (`WinHttpWebSocketCompleteUpgrade` chargé dynamiquement pour compatibilité MinGW-w64 & MSVC) + repli synchronisé via **WinSock2** (`ws2_32.lib`).
   - Tampon circulaire protégé par `SRWLOCK` et `CONDITION_VARIABLE`, et notification UI via `PostMessageW`.

3. **Moteur Analytique, Dessins Persistants, Alertes & Export** :
   - `indicators.c` : EMA rapide/lente, SMA, Bandes de Bollinger, RSI (Wilder), MACD avec activation individuelle.
   - `drawings.c` : Outils ancrés `(timestamp, prix)` — Trendline, HLine, Fibonacci, Canal, Rectangle, Texte — sauvegardés automatiquement dans `openview_drawings.json`.
   - `alerts.c` : Évaluateur multi-conditions (`AND` / `OR`) incluant `SPREAD_BPS` et croisements (`cross_above`, `cross_below`), journalisation et toast + `MessageBeep`.
   - `export.c` : Exportation UTF-8 via boîte de dialogue `GetSaveFileNameW` (`comdlg32.lib`) en `.csv` (avec BOM) et `.json`.

## Compilation

### Option 1 : Makefile (racine du projet, MinGW-w64)
```sh
make
```

### Option 2 : CMake + MSVC (Visual Studio 2022+)
```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Option 3 : Script MSVC direct
Ouvrir *x64 Native Tools Command Prompt for VS* dans `C/` et exécuter :
```bat
build_msvc.bat
```
