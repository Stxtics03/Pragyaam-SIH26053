# Rerun vs Unreal - every difference in the data

Measured on `scenes/seq00-full` (KITTI seq 00, 4541 frames) against the VRgrid
pipeline checkout, on 2026-09-22.

## Read this first

The demo is **two windows from one run**:

- The **Rerun window** is the system. It is where the VRgrid map is built and
  where every reported number comes from - map size, latency, per-ring RMSE,
  ghost removal. Nothing in this repository can move any of them: the exporter
  taps `PipelineView.log_frame`, which runs strictly *after* `MapEngine.step`.
- The **Unreal window** is a companion visualisation - a street built around
  the drive so an audience can see what the sensor is looking at. It is
  deliberately a rendering, not a measurement.

This document is the audit trail for that split: what is genuinely shared
between the two windows, and what the Unreal side invents. Sections below say
"synthetic" and "authored here" about the Unreal street - that is the point of
the exercise, not an admission. The road, buildings, traffic and pedestrians
are scenery; the **trajectory the car drives is the dataset's own, verified to
0.021 mm** (section 0), and the accuracy rings are drawn from the ring schedule
the engine actually ran.

Where the Unreal side did drift from the data it is listed here and fixed - see
sections 3 and 3b, where the car's speed profile and its yielding behaviour
were both brought back to what the recording does.

Two Unreal modes exist and they differ from Rerun by very different amounts:

| mode | flag | intent |
|---|---|---|
| **map viewer** | `-VrgMode` absent | mirror the Rerun window |
| **driving sim** | `-VrgMode=sim` | the real-world picture beside it |

Unless a row says otherwise, "Unreal" below means the **driving sim**, because
that is what the demo runs. The map viewer is faithful — it draws every layer
unconditionally and at Rerun's point size.

---

## 0. What is already identical — verified, not assumed

| | how it was checked |
|---|---|
| **Trajectory** | rebuilt independently from the KITTI files through the pipeline's own `loader.poses("00") → transforms.vehicle_to_world()` and compared to the `TRAJ` chunk: **max error 0.021 mm**, mean 0.006 mm, zero poses over 0.1 mm. That residual is float32 storage rounding, nothing else. |
| **Pose source** | seq 00 uses SemanticKITTI **SLAM** poses (`sequences/00/poses.txt`), not `poses/00.txt` — `POSE_SOURCE_BY_SEQUENCE = {"00": "slam", "08": "slam"}`. Both checkouts' `loader.py` and `transforms.py` are byte-identical, so the pose is the same whichever one runs. |
| **Route shape** | 3709.5 m, 5158° of cumulative heading change — matches what the sim logs. No invented curves. |
| **Point positions and colours** | the exporter calls **the same function** the Rerun view calls: `get_display_points(frame, ghost_removal, color_by, palette)`. Same for `_height_ramp`, `_confidence_ramp`, `GHOST_RGB`, imported from `vrgrid.dash.pipeline_view`. |
| **Map redraw cadence** | manifest `map_interval: 5` == Rerun's `MAP_INTERVAL = 5`. |
| **Ring schedule** | 5/10/20/40 cm at 10/25/50/100 m, 745,000 cells, 8.94 MB, blind cone 3.74 m — read from the manifest, not hardcoded. |
| **Frame count / indices / timestamps** | 4541 frames, 0–4540, header `time_s = index / 10`. |

---

## 1. Data Rerun has that the export does not

**1.1 — `seq00-full` contains no map data at all.** It was baked `--light`,
which skips `PNTS`, `GHST`, `OCCU`, `FREE`, `UNKN` entirely. Measured over its
first 400 frames: only `TRAJ`, and only every 20th frame.

```
seq00-full (light):   TRAJ present in 20/400 frames, nothing else
ghosts-on   (full):   PNTS 60/60, GHST 60/60, OCCU/FREE/UNKN/TRAJ 13/61
```

So the full-route demo currently shows **zero measured VRgrid data**. Every
coloured thing on screen is synthetic.

**1.2 — curbs, potholes and confidence are written only into `final.vrgf`.**
`sink.log_frame` never emits `CURB`/`POTH`/`CONF`; only `finish()` does, via
`_feature_chunks()`. Rerun redraws them every `FEATURE_INTERVAL = 20` frames
during the run. Both sides need `--features`, which `seq00-full` did not use.

The `.vrgf` format and the C++ reader already support all three
(`VrgFrameReader.cpp` handles `CURB`, `POTH`, `CONF`) — only the writer is
missing them.

---

## 2. Data the export has that the sim does not draw

| | |
|---|---|
| **2.1 ghosts** | `MapGhosts->ClearInstances()` runs unconditionally every map frame. The sim never draws ghosts, though Rerun logs `world/ghosts` every frame. |
| **2.2 sweep points** | `bShowSweep = false` by default |
| **2.3 occupied cells** | `bShowCells = false` by default |
| **2.4 free / unknown** | `bShowFreeUnknown = false` by default |

2.2–2.4 are sim-only; the map viewer draws all of them. Note this is a second,
independent reason the demo shows no data: even a full re-bake would render
nothing until these are on.

**2.5 — point decimation.** The sim draws every 4th point (`PointStride = 4`);
Rerun draws every point.

**2.6 — point size.** Sim `PointSizeM = 0.07` m; Rerun `radii = 0.03`
(0.06 m across) and ghosts at `radii = 0.09`. The map viewer uses 0.06 and
matches.

---

## 3. Timeline — the largest behavioural difference

There are **two independent clocks** in the sim:

| | advances by | full route |
|---|---|---|
| map overlay (`MapFrame`) | `MapScene.PlaybackFps` = 10 Hz — the dataset clock | 454.0 s |
| the car (`DistanceM`) | `EgoSpeedMS × DeltaSeconds` | was ~267 s, **now ~452 s** |

**Mostly closed.** `SpeedMS` was 13.9 m/s; it is now **8.2**, the dataset's own
mean (8.17). 3709.5 m at 8.2 m/s is **452 s against the recording's 454.0 s**.

That was not a cosmetic change — it was forced by §3b. The recorded yield
(slow to 3.4 m/s, never halt) is *physically unreachable* at 13.9 m/s:

```
cruise 13.9 m/s -> braking to 3.4 takes 2.33 s and 20.2 m   detection range is 16 m
cruise  8.2 m/s -> braking to 3.4 takes 1.07 s and  6.2 m   fits
```

At 13.9 the car could only arrive at cruise or slam to a halt, which is
precisely why the yield floor had been zero.

What is still different:

- **3.1** This reproduces the drive's **average** pace, not its instantaneous
  profile. The real vehicle ranged 0 → 13.8 m/s; the sim holds 8.2 except when
  yielding. Driving `DistanceM` straight off the per-frame trajectory would
  reproduce the profile exactly — that remains the one unfinished piece.
- **3.2** Because the map overlay *is* on the dataset clock, map cells are drawn
  at the **dataset's** vehicle position. At ~452 s vs 454 s the two now stay
  within a few metres over a whole lap instead of diverging by hundreds.
  Invisible today regardless, because `seq00-full` carries no cells.
- **3.3** The sim loops the route; Rerun plays through once.

---

## 3b. Ego behaviour — the car stopped constantly, and none of it was in the data

Raised from the Rerun side and confirmed here by measuring the trajectory
directly, independent of that analysis.

**What the recorded drive does** (seq 00, 3709.5 m, 454.0 s):

```
mean speed                8.17 m/s
stop episodes >= 0.5 s    1     frames 536-562, 2.6 s, at 374 m -- empty road
clean lateral crossings   1     frames 4377-4412, at 3581 m (97% round)
ego response to it        7.88 -> 3.44 m/s, never halts
```

That agrees with the Rerun-side scan to 0.06 m/s ("slowed 7.9 -> 3.5") and the
same stop episode at a different speed cut-off ("frames 541-559, 1.9 s").

**What the sim did instead** — both numbers authored here, neither from data:

| | was | why it stopped so much |
|---|---|---|
| `ScriptedCrossPeriodS` | **26 s** | walked a pedestrian into the car's path ~10× per lap |
| ambient pedestrians | 26, crossing on 8-26 s dwells | the car met someone in its lane almost continuously |
| `YieldStopGapM` floor | **0 m/s** | commanded a *full stop* inside 7.5 m |

The zero floor had a real cause — an earlier 3.2 m/s floor let the car arrive
at someone still mid-road — but the fix for that is timing, not stopping.

**Changed to match the data:**

| | now |
|---|---|
| `ScriptedCrossAtM = 3581.0` | one crossing per lap, at the location the recorded one happened |
| `CrossSpeedMS = 2.6` | the recorded pace: 9.6 m of lateral travel in 3.6 s |
| `bAmbientPedsCross = false` | everyone else keeps to the footway |
| `YieldFloorMS = 3.4` | eases to 3.4 m/s and keeps rolling, as the recorded ego did |
| `YieldPanicGapM = 2.5` | last-resort stop; with the lead distance right it never fires |
| `ScriptedLeadM()` | leads by time-to-reach-lane **plus 20 m of braking room** |
| `SpeedMS = 8.2` | the dataset mean — see §3; the yield is unreachable above it |

Measured after the change: **1 crossing per lap, 0 panic stops, 0 ego hits,
0 traffic hits, 0 near passes, `pedgate=1` in 0 of 104 samples.** Before it,
the gate was on almost continuously.

The lead distance is the load-bearing change, and it took two attempts.
Leading by "time to reach the lane" puts a pedestrian in the lane at the exact
moment the car arrives — fine if the car stops, useless if it must not.
Leading by "time to **clear** the lane" overcorrected: they were gone before
the car was close enough to react, so it sailed through at 13.6 m/s and the
yield never engaged at all. What works is time-to-reach-lane **plus enough
distance to shed the speed in** — the car is already at the yield speed when it
arrives, and because it slows on approach it always takes longer than the
arithmetic promises, so the crosser gets more room than predicted, never less.

## 4. Rerun entities with no Unreal counterpart

| entity | note |
|---|---|
| `world/vehicle/marker` | the vehicle glyph |
| `world/follow` | camera-follow anchor; Unreal has its own chase camera |
| `panel/*` — legend, details, KPI tiles, status, header, alert feed, frame feed | by design: that is the Rerun window's job |
| `stats/*` — frame_ms, moving/cleared, memory_mb, gpu_pct | same |

The blind cone is **not** in this list — Rerun draws it under the vehicle
transform and the sim draws it too.

---

## 5. Drawn in Unreal with no Rerun counterpart

All synthetic, none of it measured:

road · kerbs · footways · verges · ground aprons · lane markings · buildings and
facades · street furniture and lamps · 83 traffic cars · 26 pedestrians ·
sky, sun, fog and shadows

Plus one that looks like data but is not:

**5.1 — the accuracy bands.** Drawn from the *ring schedule in the manifest*
(a 23×23 lattice over the Chebyshev rings), not from measured cells. They show
what the sensor's resolution *would* be at each range, not what it observed.

---

## 6. Representation differences (deliberate)

| | |
|---|---|
| **6.1 cells** | Unreal draws real instanced **cubes at true edge length**; Rerun draws **sized points**, because it processes box instances one at a time on the CPU. This is the one thing the Unreal window shows better. |
| **6.2 heading** | The export stores the pipeline's per-frame yaw, but the sim **re-derives** heading from the trajectory over ±3 samples. |
| **6.3 duplicate poses** | `LoadPath` drops poses within 5 mm of the previous (`P.Equals(last, 0.5)` in cm): 4541 → 4533 samples. Only removes frames where the vehicle was stationary; the path shape is unchanged. |
| **6.4 coordinates** | metres / y-left / right-handed → centimetres / y-right / left-handed. Verified at load against test vectors carried in `scene.json`. |
| **6.5 height uncertainty** | `sigma_cm` is quantised into the 24-byte cell record; Rerun reads `height_variance` directly. |

---

## 7. Closing the gaps

Ordered by how much they change what is on screen.

1. **Turn the sim's map layers on** (2.1–2.4). Without this nothing else matters.
2. **Lock the car to the dataset clock** (3.1, 3.2). This is what makes the map
   sit under the car. It costs the car's ability to brake for pedestrians —
   resolved by having pedestrians time their crossings to gaps instead, so the
   crossings stay and the collisions do not come back.
3. **Re-bake `seq00-full` with real data** (1.1). Costs disk — see below.
4. **Write feature chunks per frame** (1.2), if the demo ever uses `--features`.
5. Match `PointStride` and `PointSizeM` to Rerun (2.5, 2.6) — cosmetic.

### The disk cost of 7.3

Measured from `ghosts-on` (60 frames, full fidelity):

```
PNTS   1.951 MB/frame, every frame   -> 8.9 GB over 4541 frames
OCCU   0.772 MB/frame avg (every 5th) -> 3.5 GB
FREE   0.097 MB/frame avg             -> 0.4 GB
GHST/TRAJ/UNKN                        -> negligible
                                    TOTAL ~12.8 GB
```

**29 GB free on C:.** A full-fidelity bake would take nearly half of it.
Points dominate and the sim already draws only every 4th one, so exporting at
stride 4 cuts `PNTS` to ~2.2 GB for no visible difference — about **6.2 GB**
total. A shorter full-fidelity window (frames 0–1200, ~2 minutes, ~1 km with
several turns) is ~3.4 GB.
