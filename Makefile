# OpenViewWin32 — Makefile (C11 Win32 Unicode / Direct2D / WinSock2)
# Usage:
#   make            (MinGW-w64 native sous Windows ou cross-compilation Linux)
#   make CC=gcc     (MinGW natif sous MSYS2 / Windows)
#   make clean

UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(OS),Windows_NT)
  ifeq ($(origin CC),default)
    CC := gcc
  endif
  WINDRES ?= windres
  MKDIR = if not exist bin mkdir bin
  RM = if exist bin rmdir /s /q bin
else
  ifeq ($(origin CC),default)
    CC := x86_64-w64-mingw32-gcc
  endif
  WINDRES ?= x86_64-w64-mingw32-windres
  MKDIR = mkdir -p bin
  RM = rm -rf bin
endif

TARGET   := OpenView.exe
SRC_DIR  := C/src
INC_DIR  := C/include
RES_OBJ  := bin/OpenView_res.o

SRCS := \
	$(SRC_DIR)/main.c \
	$(SRC_DIR)/d2d_renderer.c \
	$(SRC_DIR)/feed_manager.c \
	$(SRC_DIR)/indicators.c \
	$(SRC_DIR)/drawings.c \
	$(SRC_DIR)/alerts.c \
	$(SRC_DIR)/export.c \
	$(SRC_DIR)/ui_dialogs.c

CFLAGS := -std=c11 -O2 -Wall -Wextra -municode \
	-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DCOBJMACROS \
	-D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -D__USE_MINGW_ANSI_STDIO=1 \
	-I$(INC_DIR)

LDFLAGS := -s -mwindows -municode \
	-ld2d1 -ldwrite -lws2_32 -lwinhttp -lcomdlg32 -ldwmapi -luser32 -lgdi32 -lwinmm -lole32

.PHONY: all clean run

all: $(TARGET)

$(RES_OBJ): OpenView.rc assets/icons/openview.ico
	$(MKDIR)
	$(WINDRES) $< -O coff -o $@

$(TARGET): $(SRCS) $(RES_OBJ)
	$(CC) $(CFLAGS) $(SRCS) $(RES_OBJ) -o $(TARGET) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	$(RM)
