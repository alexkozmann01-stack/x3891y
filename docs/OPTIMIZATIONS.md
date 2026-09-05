# Optimization framework

Every setting Nasaki can change goes through one interface
(`src/optim/Optimization.h`) so they all get the same guarantees:

1. **Original state is captured before the first write** and stored in
   `%APPDATA%\Nasaki\backups.json`. The backup records whether the value
   *existed*, its type, and its bytes.
2. **Applying is verified by reading back.** A successful `RegSetValueEx` is
   not proof — policy can override a value — so the card only shows
   "Použité" after a re-read matched.
3. **Restoring puts back exactly what was there.** If a value did not exist
   before, restore *deletes* it rather than writing a zero.
4. **Repeated applies never overwrite the original.** The first captured
   value is the one kept, so rollback still works after applying twice.
5. **Nothing is recommended just because it exists.** Every entry is
   classified against the detected machine, and the reason is shown.

## Layout

| File | Role |
| --- | --- |
| `optim/Optimization.h` | The interface: id, category, benefit, evidence, classification, tradeoffs, admin/restart flags, `Read` / `Apply` / `Restore`, structured `Error`. |
| `optim/SystemInventory.*` | What this machine is: OS, CPU, memory, GPU, display, power source, drives. |
| `optim/RegistryValue.*` | Registry access that preserves existence, type and raw bytes. |
| `optim/RegistryOptimization.*` | Registry DWORDs, registry strings, and `SystemParametersInfo` booleans. |
| `optim/ManualGuide.*` | Settings we read but deliberately never write. |
| `optim/StartupEntries.*` | Programs that launch with Windows. |
| `optim/PowerPlans.*` | Power plan enumeration and switching. |
| `optim/StorageCleanup.*` | Disk measurement and previewed cleanup. |
| `optim/Profiles.*` | Named bundles of the settings above. Nothing more. |
| `optim/BackupStore.*` | Persistent backup + change journal. |
| `optim/Catalog.cpp` | The shipped set of settings, their classification, and the documented reasons for what is excluded. |
| `optim/OptimizationService.*` | Runs everything on the shared worker thread; the UI only reads a cached snapshot. |

## Detection drives the advice

`SystemInventory` reads, all through documented APIs and read-only registry
values:

- OS product name, display version, build and UBR from `CurrentVersion`
  (`GetVersionEx` lies to unmanifested processes).
- CPU brand via `__cpuid`, logical processors via `GetSystemInfo`, physical
  cores by counting `RelationProcessorCore` entries.
- Physical memory via `GlobalMemoryStatusEx`, plus commit total/limit via
  `GetPerformanceInfo` — commit is what actually predicts out-of-memory.
- Primary adapter name and dedicated VRAM via DXGI.
- Current display mode *and* the highest refresh the panel advertises at that
  resolution, via `EnumDisplaySettingsW`.
- Battery presence and AC state via `GetSystemPowerStatus`.
- Per-drive SSD vs HDD via `IOCTL_STORAGE_QUERY_PROPERTY` with
  `StorageDeviceSeekPenaltyProperty`.

Anything a device declines to answer stays unknown and is displayed as
unknown. The dashboard shows the whole inventory so a wrong reading is
visible rather than silently feeding bad advice.

## Classification

| Class | Meaning |
| --- | --- |
| Odporúčané pre tento počítač | The detected hardware/OS/current state says this machine benefits. |
| Podľa situácie | Real effect, but only under conditions the card spells out. |
| Pokročilé | Narrow or experimental. Never pre-selected. |
| Návod | A real setting we deliberately do not write; the card links to where Windows exposes it. |

Every entry carries a `classificationReason` shown verbatim — "8 GB RAM
detected", "integrated graphics", "measured 60 Hz of 144 Hz" — so a
recommendation always states the fact behind it.

## Shipped settings

All are per-user (`HKCU` or `SystemParametersInfo`), need no elevation, and
are reversible.

| Setting | Mechanism | Benefit |
| --- | --- | --- |
| Windows Game Mode | `HKCU\Software\Microsoft\GameBar\AutoGameModeEnabled` | Gaming |
| Disable background recording (Game DVR) | `GameConfigStore\GameDVR_Enabled` + `GameDVR\AppCaptureEnabled` | Gaming |
| Disable window animations | `SPI_SETCLIENTAREAANIMATION` | Perceived responsiveness |
| Disable UI effects | `SPI_SETUIEFFECTS` | Perceived responsiveness |
| Disable transparency | `Themes\Personalize\EnableTransparency` | Perceived responsiveness |
| Faster menus | `Control Panel\Desktop\MenuShowDelay` (REG_SZ) | Perceived responsiveness |
| Remove taskbar Widgets | `Explorer\Advanced\TaskbarDa` (Windows 11 only) | Perceived responsiveness |
| Limit Store background apps | `BackgroundAccessApplications\GlobalUserDisabled` | Throughput |
| Enable Storage Sense | `StorageSense\Parameters\StoragePolicy\01` | Storage |
| Disable tips and suggestions | `ContentDeliveryManager\SubscribedContent-338389Enabled` | Convenience |
| Disable Start suggestions | `ContentDeliveryManager\SubscribedContent-338388Enabled` | Convenience |
| Disable advertising ID | `AdvertisingInfo\Enabled` | Privacy |

### Informational (read, never written)

| Setting | Why we don't write it |
| --- | --- |
| Hardware-accelerated GPU scheduling | HKLM, needs elevation and a reboot, and the effect depends on the GPU driver. We read `HwSchMode` and link to `ms-settings:display-advancedgraphics`. |
| Per-game GPU preference | Per-application, stored in an undocumented format. Linked instead. |
| Display refresh rate | Only shown when the panel measurably runs below its maximum. Changing display modes belongs to Windows. |

## Startup programs

`HKCU\...\Run` values are enumerated and reversibly removable: the value's
exact name, type and bytes go into the backup journal, the value is deleted,
and the delete is verified. Restore writes the snapshot back and verifies it
matches — command line, arguments and quoting included.

`HKLM\...\Run` and both Startup folders are **read-only**. Task Manager's own
enable/disable writes an undocumented `StartupApproved` blob, which we will
not write blind, so those rows say who manages them and link to
`ms-settings:startupapps`.

Nothing is ever disabled in bulk. Every action names one entry.

## Power plans

Enumerated with `PowerEnumerate`, named with `PowerReadFriendlyName`,
switched with `PowerSetActiveScheme` and then re-read to confirm. The plan
that was active before Nasaki first switched is captured so it can be put
back. Guidance is battery-aware: on battery the page says the higher plan
costs runtime and heat, and to wait for AC.

**Only whole plans are switched.** Individual plan settings — core parking,
processor performance thresholds — are left to Windows, because a single
value applied across every machine is exactly the tweak this project
rejects.

## Storage

Measured: user `%TEMP%`, the Recycle Bin, and Downloads. Only the first two
are deletable; Downloads is reported with no button, because there is no
action we would take on a personal folder.

- Temp files younger than 7 days are never touched — a running installer is
  still using them.
- Locked files are skipped and counted, and the count is reported next to
  what was freed.
- **The delete button only appears after the exact file list has been shown**
  for that specific target.
- The Recycle Bin goes through `SHQueryRecycleBin`/`SHEmptyRecycleBin` and
  reports the difference the OS reports. Its contents are not enumerable
  through a documented API, so we say what will happen rather than faking a
  list.
- System caches needing elevation are left to `cleanmgr`, linked from the
  page. No registry cleaning, no junk-file heuristics.

## Profiles

A profile is a list of catalog ids and nothing else. Selecting one shows
every setting it would change, with each one's current state; applying runs
each through the normal capture/write/verify path. The result message states
how many of how many landed, what was unsupported, and what failed by name.
A profile is never reported as applied when part of it did not.

The battery profile is only offered on machines with a battery.

## Installed programs

Read from the three Uninstall keys Windows itself uses (HKLM 64-bit, HKLM
32-bit, HKCU), with the same filters Windows applies: no display name,
`SystemComponent`, or a patch hanging off a parent product.

This is an inventory, not a remover. Nasaki deletes no files, runs no silent
or mass uninstall, and does not "clean up leftovers". The only action is
handing one user-selected program to the publisher's own uninstaller —
the interactive `UninstallString` is preferred over `QuietUninstallString`
so there is still a chance to cancel.

Runtime dependencies and driver packages (Visual C++ redistributables, .NET,
WebView2, Edge, DirectX, GPU and chipset software, Microsoft Store
infrastructure) are flagged as protected and get no uninstall button, with
the reason shown. Reported sizes are whatever the installer wrote to
`EstimatedSize` and are labelled as estimates; missing sizes are shown as
missing rather than as zero.

## Deliberately excluded

The reasoning is also in `optim/Catalog.cpp` so it stays next to the code.

- **`timeBeginPeriod` ("high resolution timer").** Microsoft documents that
  [since Windows 10 version 2004 it no longer affects global timer
  resolution](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod)
  — only processes that call it themselves. Nasaki calling it therefore
  cannot help a game in another process. The same page notes it "can reduce
  overall system performance" and "prevent the CPU power management system
  from entering power-saving modes". **This shipped in an earlier build and
  has been removed.**
- **Working-set trimming ("free up RAM").** Evicts pages the application
  faults straight back in; costs more than it saves. **Also shipped earlier
  and removed.**
- **Real-time process priority.** Starves the input and audio threads the
  user needs. The session boost uses above-normal only.
- **Universal core-parking and CPU-scheduling tweaks.** Machine-specific by
  nature; a single value applied everywhere is not an optimization.
- **Blanket service or scheduled-task disabling, page-file changes, registry
  cleaning, debloat scripts.**
- **Network stack tweaks** — MTU, TCP parameters, offloads, Nagle, QoS, DNS
  changes claimed to reduce in-game latency. We have no measurement that
  would justify any of them.
- **Driver installation, firmware flashing, overclocking.**
- **Anything that weakens security** — antivirus, firewall, Windows Update,
  exploit mitigations.
- **Fabricated numbers** — FPS estimates, health scores, "percent optimized".

## Tests

`tests/OptimTests.cpp`, built as `NasakiTests` and run in CI. They use a
scratch key under `HKCU\Software\NasakiTests` and a temp-file journal, so
they exercise the real registry paths unelevated without touching the
user's data.

Covered: backup capture, apply, verification, rollback, restore of a value
that never existed, repeated-apply backup integrity, restore with no backup,
unsupported settings, partial application, exact type/byte round-trip for a
non-DWORD value, REG_SZ apply/verify/rollback, text mismatch not being
reported as applied, a build-gated entry refusing to write, startup entry
removal and exact command-line restore, removed entries staying listed, and
foreign startup entries being refused.

## Not built yet

Marked plainly rather than stubbed: network diagnostics, driver and hardware
reporting, and before-and-after measurement of a change. The backup journal and history
records those screens would need are already being written.
