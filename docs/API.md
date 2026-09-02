# Nasaki backend API (client contract)

Base URL: `https://nasaki.eu/api/`

All requests/responses are JSON. This client is a native app, not a browser —
there is no cookie/CSRF auth here, only the tokens described below.

## 1. Register device

Called once, the first time the client runs on a machine (or after a
device's access has been revoked). Exchanges the user's license key for a
long-lived device token.

```
POST /api/register-device.php
Content-Type: application/json

{
  "license_key": "XXXXX-XXXXX-XXXXX-XXXXX",
  "hostname": "DESKTOP-ABC123",
  "os_info": "Windows 11 23H2",
  "cpu_info": "AMD Ryzen 7 7800X3D",
  "gpu_info": "NVIDIA RTX 4070"
}
```

Response:

```json
{ "ok": true, "device_id": 42, "device_token": "…64 hex chars…" }
```

Errors: `403 license_not_active` (key missing/expired/revoked), `422
missing_license_key`.

**The client must store `device_token` locally (e.g. in a per-user app data
folder) and reuse it for every call below.** Re-registering is only needed if
the stored token stops working (401 from telemetry.php) or a fresh install.

## 2. Telemetry ingest

```
POST /api/telemetry.php
Authorization: Bearer <device_token>
Content-Type: application/json
```

If the `Authorization` header ever gets stripped by some proxy, `device_token`
can also be sent as a field in the JSON body instead — the server accepts
either.

Three event types, one per call:

### `session_start`

```json
{ "event": "session_start", "game_name": "Counter-Strike 2" }
```
→ `{ "ok": true, "session_id": 123 }`

### `samples` (send periodically during play, batched — not per-frame)

```json
{
  "event": "samples",
  "session_id": 123,
  "samples": [
    {
      "recorded_at": "2026-09-02 21:14:03",
      "fps": 142.3,
      "frametime_ms": 7.03,
      "cpu_pct": 61,
      "gpu_pct": 78,
      "ram_pct": 54,
      "cpu_temp_c": 66,
      "gpu_temp_c": 71
    }
  ]
}
```
Max 500 samples per call. → `{ "ok": true, "inserted": <n> }`

### `session_end`

```json
{
  "event": "session_end",
  "session_id": 123,
  "avg_fps": 138.4,
  "low1_fps": 95.0,
  "avg_frametime_ms": 7.2,
  "avg_cpu_pct": 60,
  "avg_gpu_pct": 75,
  "avg_ram_pct": 52,
  "avg_cpu_temp_c": 65,
  "avg_gpu_temp_c": 70,
  "stutter_count": 3
}
```
→ `{ "ok": true }`. Server computes session duration itself from
`started_at`/`NOW()` — don't send a duration.

## Notes for whoever builds this client

- A license key only produces a device token while the license is currently
  **active** (not unused, expired, or revoked) — check that during onboarding.
- Sessions/devices show up live on the user's dashboard
  (`https://nasaki.eu/account/performance.php`, `/account/devices.php`) and
  in the admin panel, so this is easy to verify end-to-end without any
  special tooling — just call the endpoints with curl/Postman and refresh
  the dashboard.
- Backend source (PHP, not this repo): `includes/telemetry.php`,
  `api/register-device.php`, `api/telemetry.php`.
