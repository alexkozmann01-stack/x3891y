# Nasaki Client

Windows desktop application for Nasaki. Responsible for:

- Measuring frame-time consistency / FPS / 1% lows during gameplay
- Reading hardware sensors (CPU/GPU utilization, RAM; temperatures not yet — see below)
- Prioritizing the game process and easing off common background apps while playing
- Showing a live overlay during play
- Reporting session + telemetry data back to the Nasaki backend (see [docs/API.md](docs/API.md))

**Tech stack:** C++17, [Dear ImGui](https://github.com/ocornut/imgui) (Win32 + Direct3D 11 backend), WinHTTP, [nlohmann/json](https://github.com/nlohmann/json).

## Status

A real, buildable app skeleton: license activation, a sidebar dashboard with
live CPU/GPU/RAM readouts and charts, and full wiring to the backend API
(device registration, session start/end, batched telemetry samples).

The window is borderless (`WS_POPUP`, no OS title bar) with a custom
minimize/close strip drawn by `App::DrawTitleBar()` — it's meant to read as
a floating ImGui panel, not a standard bordered Windows app. `NasakiClient.exe`
is fully self-contained: the UI font is compiled straight into the binary
(`src/ManropeFont.h`), nothing needs to ship alongside it.

**Not built yet — the two hard parts:**

- **In-game overlay.** This app is currently its own standalone window, not
  an overlay drawn on top of a running game. Real overlay rendering needs
  hooking the game's own DirectX/Vulkan present call (the way RTSS/MSI
  Afterburner do it) — a separate, significantly more involved piece of work
  (injection, per-API hooking, compatibility across engines/anti-cheat).
  Because of this, FPS/frame-time/1% low aren't measured yet either — there's
  no per-game render-loop hook to measure them from. `SessionEndStats`
  already has the fields ready; they're just left unset for now rather than
  faked. See `App::StopSession()` in `src/App.cpp`.
- **CPU/GPU temperatures.** No OS-level API exposes these without a vendor
  SDK (NVAPI/ADLX) or a kernel driver (LibreHardwareMonitor's approach, via
  its bundled WinRing0 driver). `SystemStats` deliberately omits them — see
  the comment in `src/SystemStats.h`.

Everything else is real and should work as built: CPU %, RAM %, GPU engine
utilization via the OS-level PDH `GPU Engine` counter, license activation,
session telemetry, and process prioritization (`src/ProcessBoost.h/.cpp`) —
starting a session runs a 3-second countdown so the player can switch to the
game (there's no render hook to detect it automatically), then boosts
whatever process is in the foreground to `ABOVE_NORMAL_PRIORITY_CLASS` and,
if enabled in Settings, lowers a small allowlist of common consumer
background apps (browsers, Discord, Spotify, ...) to `BELOW_NORMAL`, exactly
restoring every touched priority when the session ends. Laptop vs. desktop
is detected via `GetSystemPowerStatus` (`src/SystemInfo.h`) — on a laptop,
sustained high CPU/GPU load shows an overheating advisory on the dashboard
(a load-based proxy, not a real temperature reading — see the temperature
section above).

## Building

Requires Visual Studio 2022 (Desktop development with C++ workload) and CMake ≥ 3.20.

```
git submodule update --init --recursive   # pulls third_party/imgui
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The built `NasakiClient.exe` (in `build/Release/` or `build/Debug/`) is a
single self-contained file — nothing needs to sit next to it.

CI (`.github/workflows/build.yml`) builds this on a real `windows-latest`
GitHub Actions runner on every push, since the app was written on macOS with
no Windows/MSVC toolchain available locally — check the Actions tab for the
latest build status and to download a built `.exe` from a run's artifacts.

## Backend integration

The website backend (separate repo, not this one) exposes the API this
client calls. See [docs/API.md](docs/API.md) for the current contract:
device registration via license key, then session + telemetry reporting via
a bearer token.

## Branding

`src/Theme.cpp` mirrors the color palette and rounding from the nasaki.eu
website's `:root` CSS variables 1:1, so the app and the site read as the same
product. The UI font is Manrope — the same family the site uses for body text
(via Google Fonts, OFL-licensed — see `assets/fonts/Manrope-OFL.txt`), loaded
with a glyph range that covers Slovak/Czech diacritics, which the default
ImGui font doesn't have.

`assets/fonts/Manrope.ttf` is the source font; `src/ManropeFont.h` is what
actually gets compiled in, generated from it via imgui's own
`misc/fonts/binary_to_compressed_c` tool:

```
c++ -O2 -o bin2c third_party/imgui/misc/fonts/binary_to_compressed_c.cpp
./bin2c -base85 assets/fonts/Manrope.ttf ManropeFont > src/ManropeFont.h
```

Only needed again if the font itself changes — regenerate and commit the
header, `assets/fonts/Manrope.ttf` isn't read at runtime.
