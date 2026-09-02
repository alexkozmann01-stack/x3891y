# Nasaki Client

Windows desktop application for Nasaki. Responsible for:

- Measuring frame-time consistency / FPS / 1% lows during gameplay
- Reading hardware sensors (CPU/GPU utilization, RAM; temperatures not yet — see below)
- Showing a live overlay during play
- Reporting session + telemetry data back to the Nasaki backend (see [docs/API.md](docs/API.md))

**Tech stack:** C++17, [Dear ImGui](https://github.com/ocornut/imgui) (Win32 + Direct3D 11 backend), WinHTTP, [nlohmann/json](https://github.com/nlohmann/json).

## Status

A real, buildable app skeleton: license activation, a sidebar dashboard with
live CPU/GPU/RAM readouts and charts, and full wiring to the backend API
(device registration, session start/end, batched telemetry samples).

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

Everything else (CPU %, RAM %, GPU engine utilization via the OS-level PDH
`GPU Engine` counter, license activation, session telemetry) is real and
should work as built.

## Building

Requires Visual Studio 2022 (Desktop development with C++ workload) and CMake ≥ 3.20.

```
git submodule update --init --recursive   # pulls third_party/imgui
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The built `NasakiClient.exe` (in `build/Release/` or `build/Debug/`) needs
`assets/` next to it — the CMake build copies it there automatically as a
post-build step.

This has been written carefully against Dear ImGui's well-established
Win32+DX11 example structure and the WinHTTP/PDH APIs, but has **not been
compiled or run** — it was written on macOS with no Windows toolchain
available. Expect to fix a handful of small build errors on first compile.

## Backend integration

The website backend (separate repo, not this one) exposes the API this
client calls. See [docs/API.md](docs/API.md) for the current contract:
device registration via license key, then session + telemetry reporting via
a bearer token.

## Branding

`src/Theme.cpp` mirrors the color palette and rounding from the nasaki.eu
website's `:root` CSS variables 1:1, so the app and the site read as the same
product. `assets/fonts/Manrope.ttf` is the same font family the site uses
for body text (via Google Fonts, OFL-licensed — see `Manrope-OFL.txt`),
loaded with a glyph range that covers Slovak/Czech diacritics, which the
default ImGui font doesn't have.
