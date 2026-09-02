# Nasaki Client

Windows desktop application for Nasaki. Responsible for:

- Measuring frame-time consistency / FPS / 1% lows during gameplay
- Reading hardware sensors (CPU/GPU utilization, temperatures, RAM)
- Showing a live overlay during play
- Reporting session + telemetry data back to the Nasaki backend (see [docs/API.md](docs/API.md))

Tech stack: **TBD** — not yet decided.

## Status

Skeleton only. No application code yet.

## Backend integration

The website backend (separate repo, not this one) already exposes the API this
client will call once it exists. See [docs/API.md](docs/API.md) for the
current contract: device registration via license key, then session +
telemetry reporting via a bearer token.
