cmake_minimum_required(VERSION 3.20)
project(OpenViewWin32 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(OpenViewWin32 WIN32
    src/main.c
    src/d2d_renderer.c
    src/feed_manager.c
    src/indicators.c
    src/drawings.c
    src/alerts.c
    src/export.c
    src/ui_dialogs.c
)

target_include_directories(OpenViewWin32 PRIVATE include)

target_compile_definitions(OpenViewWin32 PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    COBJMACROS
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
)

if(MSVC)
    target_compile_options(OpenViewWin32 PRIVATE /W4 /utf-8 /O2)
else()
    target_compile_options(OpenViewWin32 PRIVATE -Wall -Wextra -O2 -municode -D__USE_MINGW_ANSI_STDIO=1)
endif()

target_link_libraries(OpenViewWin32 PRIVATE
    d2d1
    dwrite
    ws2_32
    winhttp
    comdlg32
    dwmapi
    user32
    gdi32
    winmm
    ole32
)
