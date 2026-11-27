# OpenView

**OpenView is a native Windows market analysis terminal written in C11.** It brings charts, indicators, drawing tools, alerts, and a watchlist together in a lightweight desktop application, with no OpenView backend or user account required.

## Highlights

- **100% local application and workspace**: OpenView runs on your PC. Charts, indicators, alerts, and drawings are processed locally. Your preferences, tracked tokens, and drawings are saved to files on your computer; OpenView does not sync them to the cloud.
- **Native Windows interface**: Win32, Direct2D, and DirectWrite, with Unicode, DPI, and light/dark theme support.
- **Interactive charts**: OHLC candlesticks, a dotted current-price line, crosshair, zoom, panning, and multiple timeframes.
- **Multiple market data sources**: Binance, Coinbase, Kraken, MEXC, and CoinGecko, with source status monitoring and overlays for available feeds.
- **Technical indicators**: configurable fast/slow EMA, SMA, Bollinger Bands, RSI, and MACD.
- **Drawing tools**: trendline, horizontal line, Fibonacci, channel, rectangle, and text annotation. Drawings are locked by default; right-click the chart to list, edit, or delete them.
- **Organized watchlist**: tokens grouped into sections that can be reordered and scrolled with the mouse wheel, with rename, move, duplicate, and delete options.
- **Multi-condition alerts**: AND/OR logic, crossovers, and spread monitoring, with local notifications.
- **Export**: export market data and workspace data to CSV/JSON.

## About local operation

The **100% local** claim applies to the application and your workspace: no OpenView server, account, or cloud storage is required. Preferences, including the most recently used drawing styles, are stored in `openview_settings.json`; the watchlist is saved in `openview_watchlist.json`, and drawings in `openview_drawings.json`. These files are created in the application's current working directory, with a fallback to its own directory if needed.

**Market data comes from external providers.** An internet connection is therefore required to search markets and retrieve up-to-date prices and historical data. OpenView contacts the APIs of the selected data sources to retrieve this information; this does not mean your workspace is sent to an OpenView server.

## Build

### With MinGW-w64

From the repository root, on Windows or when cross-compiling:

```sh
make
```

The output is `OpenView.exe`.

### With CMake and Visual Studio 2022

```bat
cmake -S C -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Technologies

- C11 and the Win32 API
- Direct2D and DirectWrite for rendering
- WinHTTP and WinSock2 for market data feeds
- CMake or Make with MinGW-w64
