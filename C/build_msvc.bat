@echo off
setlocal
if not exist bin mkdir bin

cl /nologo /std:c11 /W4 /O2 /utf-8 ^
   /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DCOBJMACROS ^
   /Iinclude ^
   src\main.c ^
   src\d2d_renderer.c ^
   src\feed_manager.c ^
   src\indicators.c ^
   src\drawings.c ^
   src\alerts.c ^
   src\export.c ^
   src\ui_dialogs.c ^
   /Fe:bin\OpenViewWin32.exe ^
   /link /SUBSYSTEM:WINDOWS d2d1.lib dwrite.lib ws2_32.lib winhttp.lib comdlg32.lib dwmapi.lib user32.lib gdi32.lib winmm.lib ole32.lib

if %ERRORLEVEL% NEQ 0 (
    echo [ERREUR] Echec de la compilation.
    exit /b %ERRORLEVEL%
)

echo [OK] Binaire genere : bin\OpenViewWin32.exe
endlocal
