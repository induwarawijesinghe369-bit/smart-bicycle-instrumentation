# Web Dashboards

Four standalone HTML pages provide live and historical views of the bicycle telemetry. They read directly from **Firebase Realtime Database** using the Firebase JavaScript SDK — no backend server, no build step.

> **Status:** The dashboards were built and worked against a live Firebase project during the project. The HTML files are not currently included in this repository — they'll be added in a later commit if/when they are recovered. This document describes what each dashboard does and how it was structured.

---

## Overview
ESP32 #2 ── Wi-Fi ──► Firebase RTDB ──► HTML dashboards (JS SDK)
(live + history)


Each page:

- Loads from a static HTML file (no server needed — can be opened locally or hosted on Firebase Hosting).
- Opens a live listener on a specific RTDB path with `.on('value', ...)`.
- Updates the UI whenever the underlying data changes.
- Includes a "Load History" button that pulls the `history` branch and renders a chart or map trail.

Pages share:

- A single Firebase config block at the top of each file.
- A common stylesheet (dark theme, large numeric readouts, high contrast for outdoor viewing).
- A consistent top bar (connection status, last-updated time).

---

## The four dashboards

### 1. Health Monitor — `health.html`

Reads from `/max30102`.

| Element | Bound to |
|---|---|
| Current HR | `latest/hr` |
| Current SpO₂ | `latest/spo2` |
| Signal quality badge | `latest/signal_quality` |
| Finger-present indicator | `latest/finger` |
| Min / max HR | computed from `history` |
| Min / max SpO₂ | computed from `history` |
| Live chart | `history/{timestamp}` series |

Behaviour:
- Updates whenever `/max30102/latest` changes.
- Chart toggles between HR-only, SpO₂-only, and dual-axis view.
- "Refresh Chart" pulls the full `history` branch once.

---

### 2. Motion Sensor — `motion.html`

Reads from `/mpu6050`.

| Element | Bound to |
|---|---|
| Ax, Ay, Az (g) | `latest/ax`, `latest/ay`, `latest/az` |
| Roll / Pitch (°) | `latest/roll`, `latest/pitch` |
| Linear acceleration | `latest/linear_acc` |
| Live bar graph | accel magnitude over time |
| Roll/pitch readout | numeric, updated live |

Behaviour:
- 3-axis acceleration shown as a live bar chart with red/green/blue channels.
- Roll and pitch displayed as numeric gauges.
- Optional time series of linear acceleration.

---

### 3. GPS Tracker — `gps.html`

Reads from `/gps`.

| Element | Bound to |
|---|---|
| Live marker | `latest/lat`, `latest/lon` |
| Speed | `latest/speed_kmh` |
| Altitude | `latest/alt` |
| Satellites | `latest/sats` |
| HDOP | `latest/hdop` |
| Path history | `history/{timestamp}` series |

Behaviour:
- Uses **Leaflet** with **OpenStreetMap** tiles for the base map.
- Live marker updated on every `latest` change.
- "Load History" button draws the full trail as a polyline.
- "Clear Display" wipes the map without clearing Firebase data.
- Connection status badge in the top-left showing Firebase connectivity.

**Design note:** the plotted path uses **raw NMEA coordinates**, not Kalman-filtered ones — see `../../docs/architecture.md` for the trade-off discussion.

---

### 4. Velocity Sensor — `velocity.html`

Reads from `/velocity`.

| Element | Bound to |
|---|---|
| Current speed (m/s) | `latest/speed_ms` |
| Kalman-filtered speed | `latest/speed_kalman` |
| Stability badge | `latest/stable` |
| Consecutive-count | `latest/consecutive` |
| Average speed | computed from `history` |
| Min / max speed | computed from `history` |
| Data point count | length of `history` |
| Speed vs. time graph | `history/{timestamp}` series |

Behaviour:
- Large numeric readout of current speed.
- Stability state shown with a green/red badge (`stable` boolean from firmware).
- "Refresh Chart" pulls the full history and redraws.

---

Page loads → Firebase SDK initialises with config block.

Attach .on('value', cb) listener to <node>/latest.

Callback fires on every DB update → DOM values replaced.

"Load History" → one-shot .once('value') on <node>/history.

History array is transformed and rendered (chart or map polyline).



No polling. Firebase pushes updates.

---

## Firebase schema consumed
/max30102/latest { hr, spo2, signal_quality, finger, ts }
/max30102/history { {ts}: { hr, spo2 }, ... }

/mpu6050/latest { ax, ay, az, roll, pitch, linear_acc, ts }
/mpu6050/history { {ts}: { ... }, ... }

/gps/latest { lat, lon, speed_kmh, alt, sats, hdop, ts }
/gps/history { {ts}: { ... }, ... }

/velocity/latest { speed_ms, speed_kalman, stable, consecutive, ts }
/velocity/history { {ts}: { ... }, ... }


Full schema in [`../../docs/architecture.md`](../../docs/architecture.md).

---

## Shared styling

- **Dark background** — better for outdoor reading in bright sunlight.
- **Large numeric readouts** — primary values readable at arm's length.
- **High contrast** — text and chart strokes remain legible on phones.
- **Consistent top bar** across all four pages:
  - Firebase connection status (green dot / red dot).
  - "Last updated" timestamp.
  - Page-specific title.

---

## Hosting

Three options, in order of preference:

1. **Firebase Hosting** — free tier, integrates cleanly with the RTDB project, custom domain possible. `firebase deploy` from a local folder.
2. **GitHub Pages** — free, static files hosted straight from this repo if placed in `/docs` or a `gh-pages` branch.
3. **Local file** — open the HTML file directly in a browser. Works for quick testing but has no HTTPS and Firebase may block some auth flows.

---

## Status

| Item | Status |
|---|---|
| Dashboard design (this document) | ✅ Complete |
| Firebase schema | ✅ Defined in `../../docs/architecture.md` |
| HTML files | ⏳ To be added if recovered |
| Hosting | Not currently deployed |

## Data flow (per dashboard)
