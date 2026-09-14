# Ride Analysis

Python scripts for pulling ride data out of Firebase Realtime Database and producing plots, summaries, and exports.

> **Status:** The analysis scripts were used internally during the project for the report figures. They are not currently included in this repository — they'll be added in a later commit if/when they are recovered. This document describes their intended use and structure.

---

## Purpose

During a ride, the display node pushes two data streams to Firebase:

- A **`latest`** node — overwritten every ~2 s (for live dashboards).
- A **`history`** node — appended every ~30 s (for later analysis).

The `latest` node is lossy by design (only the most recent value at any moment). All post-ride work therefore reads from the **`history`** branch.

This folder contains the tools that:

1. Pull a ride's `history` from Firebase into local CSV.
2. Compute ride summary statistics (distance, average and max speed, HR zones, min/max SpO₂).
3. Plot time series for review and reporting.

---

## Data source

Four history branches are available:
/max30102/history HR and SpO₂ samples
/mpu6050/history accel, roll, pitch, linear acceleration
/gps/history lat, lon, altitude, satellites, HDOP, GPS speed
/velocity/history wheel speed + Kalman-filtered speed


Each `history` entry is keyed by an epoch-style timestamp (the ESP32's `millis()` at the time of writing, offset to the ride start). All four streams can be joined on that key to build a single time-aligned table.

---

## Planned scripts

### `export_ride.py`

Pulls a complete ride from Firebase and writes a CSV.

python export_ride.py --ride-id 2026-03-15-a --out ride.csv


- Reads `firebase-admin` credentials from `.env` (never committed).
- Pulls all four `history` branches for the given ride.
- Merges on timestamp.
- Writes `ride.csv` with one row per sample.

### `plot_ride.py`

Renders a summary figure from a ride CSV.


python plot_ride.py ride.csv --out ride_summary.png



Produces a multi-panel figure:

| Panel | Content |
|---|---|
| 1 | Speed (GPS + wheel + Kalman-filtered) over time |
| 2 | Heart rate over time, with min/max markers |
| 3 | SpO₂ over time |
| 4 | Altitude and roll/pitch over time |
| 5 | Map of the GPS trail |

### `summary.py`

Prints ride statistics as text or JSON.

python plot_ride.py ride.csv --out ride_summary.png


Outputs:

- Ride duration
- Total distance (from GPS trail)
- Average / max / min speed
- Average / min / max heart rate, time in HR zones
- Average / min / max SpO₂
- Number of valid GPS samples, mean HDOP

### `merge_streams.py` (helper)

Low-level merge utility used by the scripts above. Joins the four history branches into a single pandas DataFrame, forward-filling sensor-specific fields where sample rates differ.

---

## Sample workflow

```bash
# 1. Install dependencies
pip install -r ../../requirements.txt

# 2. Set up Firebase credentials
cp .env.example .env
# edit .env with your Firebase service account path and database URL

# 3. Pull the ride
python export_ride.py --ride-id 2026-03-15-a --out ride.csv

# 4. Get summary numbers
python summary.py ride.csv

# 5. Plot
python plot_ride.py ride.csv --out ride_summary.png
