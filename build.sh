#!/bin/bash
set -e

MINGW="D:/Dump_Dishornored1/1/ESP_DLL/mingw32/bin"
export PATH="$MINGW:$PATH"
CC="$MINGW/i686-w64-mingw32-g++.exe"

CFLAGS="-O2 -std=c++17 -DWIN32 -D_WIN32 -DUNICODE -D_UNICODE -I imgui -I imgui/backends"
LDFLAGS="-O2 -static -ld3d9 -ldwmapi -lgdi32 -luser32 -lpsapi -limm32 -Wl,--enable-stdcall-fixup"

IMGUI_OBJS="imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o imgui_impl_dx9.o imgui_impl_win32.o"
MINHOOK_OBJS="minhook_hook.o minhook_buffer.o minhook_trampoline.o minhook_hde32.o"

echo "=== Building Dishonored ESP ==="

echo "[1/4] Compiling ImGui..."
"$CC" -c $CFLAGS \
    imgui/imgui.cpp \
    imgui/imgui_draw.cpp \
    imgui/imgui_tables.cpp \
    imgui/imgui_widgets.cpp \
    imgui/backends/imgui_impl_dx9.cpp \
    imgui/backends/imgui_impl_win32.cpp

echo "[2/4] Compiling MinHook..."
"$CC" -c $CFLAGS \
    -o minhook_hook.o       minhook/src/hook.c \
    || "$CC" -c -O2 -I minhook/include minhook/src/hook.c -o minhook_hook.o
"$CC" -c -O2 -I minhook/include minhook/src/buffer.c -o minhook_buffer.o
"$CC" -c -O2 -I minhook/include minhook/src/trampoline.c -o minhook_trampoline.o
"$CC" -c -O2 -I minhook/include minhook/src/hde/hde32.c -o minhook_hde32.o

echo "[3/4] Compiling DLL overlay..."
"$CC" -c $CFLAGS dll_main.cpp -o dll_main.o

echo "[4/4] Linking DLL..."
"$CC" -shared -o DishonoredESP.dll $LDFLAGS \
    dll_main.o $IMGUI_OBJS $MINHOOK_OBJS

echo "=== SUCCESS: DishonoredESP.dll ==="
ls -la DishonoredESP.dll
