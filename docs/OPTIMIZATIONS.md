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

## Layout

| File | Role |
| --- | --- |
| `optim/Optimization.h` | The interface: id, category, benefit, evidence, tradeoffs, admin/restart flags, `Read` / `Apply` / `Restore`, structured `Error`. |
| `optim/RegistryValue.*` | Registry access that preserves existence, type and raw bytes. |
| `optim/RegistryOptimization.*` | Two concrete kinds: N registry DWORDs, and `SystemParametersInfo` booleans. |
| `optim/BackupStore.*` | Persistent backup + change journal. |
| `optim/Catalog.cpp` | The shipped set of settings, and the documented reasons for what is excluded. |
| `optim/OptimizationService.*` | Runs everything on the shared worker thread; the UI only reads a cached snapshot. |

## Shipped settings

All are per-user (`HKCU` or `SystemParametersInfo`), need no elevation, and
need no restart.

| Setting | Mechanism | Benefit shown as |
| --- | --- | --- |
| Windows Game Mode | `HKCU\Software\Microsoft\GameBar\AutoGameModeEnabled` | Gaming, "depends on the machine" |
| Disable background recording (Game DVR) | `HKCU\System\GameConfigStore\GameDVR_Enabled` + `…\GameDVR\AppCaptureEnabled` | Gaming, documented |
| Disable window animations | `SPI_SETCLIENTAREAANIMATION` | Perceived responsiveness |
| Disable UI effects | `SPI_SETUIEFFECTS` | Perceived responsiveness |
| Disable transparency | `…\Themes\Personalize\EnableTransparency` | Perceived responsiveness |
| Disable tips and suggestions | `…\ContentDeliveryManager\SubscribedContent-338389Enabled` | Convenience |
| Disable advertising ID | `…\AdvertisingInfo\Enabled` | Privacy |

Cards state their benefit category explicitly so a cosmetic or privacy
preference is never presented as a throughput win.

## Deliberately excluded

These were considered and rejected; the reasoning is also in
`optim/Catalog.cpp` so it stays next to the code.

- **`timeBeginPeriod` ("high resolution timer").** Microsoft documents that
  [since Windows 10 version 2004 it no longer affects global timer
  resolution](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod)
  — only processes that call it themselves. Nasaki calling it therefore
  cannot help a game in another process. The same page notes it "can reduce
  overall system performance" and "prevent the CPU power management system
  from entering power-saving modes". **This shipped in an earlier build of
  Nasaki and has been removed.**
- **Working-set trimming ("free up RAM").** Evicts pages the application
  faults straight back in; costs more than it saves. **Also shipped
  earlier and removed.**
- **Blanket service disabling, page-file changes, registry cleaning,
  network stack tweaks, HAGS.** Either unsupported, system-wide with poor
  failure modes, requiring admin/reboot, or unsubstantiated.
- **Anything that weakens security** — antivirus, firewall, Windows Update,
  exploit mitigations.

## Tests

`tests/OptimTests.cpp`, built as `NasakiTests` and run in CI. They use a
scratch key under `HKCU\Software\NasakiTests` and a temp-file journal, so
they exercise the real registry paths unelevated without touching the
user's data.

Covered: backup capture, apply, verification, rollback, restore of a value
that never existed, repeated-apply backup integrity, restore with no backup,
unsupported settings, partial application, and exact type/byte round-trip
for a non-DWORD value.

## Not built yet

Marked plainly rather than stubbed: Startup entries, Power plans UI,
Storage cleanup, Applications, Network/DNS, the Backups & History screen,
profiles (Balanced / Gaming / Low power), and before-and-after measurement.
The backup journal and history records that those screens need are already
being written by the framework.
