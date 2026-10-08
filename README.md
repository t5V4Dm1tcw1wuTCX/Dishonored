# Dishonored 1 Overlay

External cheat DLL for Dishonored 1 (Unreal Engine 3, D3D9, x86) with D3D9 hook overlay.

## Features

### ESP
- 2D/3D bounding boxes with corner style
- Skeleton rendering (bone connections)
- NPC names, distance, health bars
- Head dot indicator
- Snaplines
- AI state colors (unaware/suspicious/combat)
- Item ESP with category filters
- Interactable object ESP

### Aimbot
- Silent aim (magic bullet) with FOV circle
- Visibility check option
- Crosshair indicator (shown when silent aim is active)

### Player
- God Mode, Infinite Mana/Health/Air/Ammo/Elixirs/Adrenaline
- No Fall Damage, Noclip
- Speed Hack (ground speed + jump height)
- Teleport to crosshair
- Wallhack (wireframe)

### Abilities
- Dark Vision Glow ESP (forced Lv2 with max distance)
- Custom Gravity
- Time Dilation
- Blink Range modifier
- No Spread, Rapid Fire, Instant Reload, No Cooldown

### AI Control
- AI Blind / Deaf / Off
- Auto Dodge, Buddha Mode, No Knockdown

### Actions
- Max Powers, Max Upgrades
- Kill All, Knockout All
- NPC Spawner

## Building

Requires MinGW i686 (32-bit) cross-compiler.

```bash
# Set MINGW path in build.sh, then:
./build.sh
```

Or manually:

```bash
# Compile
i686-w64-mingw32-g++ -c -O2 -std=c++17 -DWIN32 -D_WIN32 -DUNICODE -D_UNICODE \
    -I imgui -I imgui/backends dll_main.cpp -o dll_main.o

# Link
i686-w64-mingw32-g++ -shared -o DishonoredESP.dll -O2 -static \
    dll_main.o imgui.o imgui_draw.o imgui_tables.o imgui_widgets.o \
    imgui_impl_dx9.o imgui_impl_win32.o \
    minhook_hook.o minhook_buffer.o minhook_trampoline.o minhook_hde32.o \
    -ld3d9 -ldwmapi -lgdi32 -luser32 -lpsapi -limm32 -Wl,--enable-stdcall-fixup
```

## Usage

1. Start Dishonored 1
2. Inject `DishonoredESP.dll` into the game process
3. Press **INSERT/HOME** to toggle the menu

## Dependencies

- [Dear ImGui](https://github.com/ocornut/imgui) (included in `imgui/`)
- [MinHook](https://github.com/TsudaKageworua/minhook) (included in `minhook/`)

## Structure

```
dll_main.cpp    - DLL entry, D3D9 hook, overlay UI, render loop
game.h          - Game memory reading, offsets, cheats
injector.cpp    - Simple DLL injector
imgui/          - Dear ImGui library
minhook/        - MinHook library
```
