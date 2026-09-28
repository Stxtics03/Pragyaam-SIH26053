# VRgrid — Unreal viewer

A second renderer for the VRgrid demo, fed from the **same per-frame call**
the Rerun window is fed from. Both windows show one run of one `MapEngine` on
one frame index — they are not two renderings of similar data.

Nothing here touches the `vrgrid` repo, and nothing here can move a reported
number: everything that produces one (map hash, latency, per-ring RMSE, ρ)
happens upstream in `MapEngine.step`, and this runs strictly after it.

---

## Status -- working end to end

| | |
|---|---|
| Exporter (`exporter/`) | working, verified on live KITTI seq 00 |
| Binary frame format + tests | working, 10/10 |
| Unreal C++ module (game + editor) | builds, UE 5.8 / MSVC 14.44 |
| Per-instance colour materials | generated, `used_with_ISM = True` |
| Viewer | **renders** -- points, cells, ring squares, blind cone |

### Toolchain note

UnrealBuildTool needs a `NETFXSDK` directory, and the Visual Studio component
`Microsoft.Net.Component.4.6.2.SDK` does **not** provide it on this Build Tools
channel (only the targeting pack lands, which is not enough). What works:

```powershell
winget install --id Microsoft.DotNet.Framework.DeveloperPack_4 --silent `
  --accept-package-agreements --accept-source-agreements
```

That creates `C:\Program Files (x86)\Windows Kits\NETFXSDK\4.8.1` and the
editor target builds. Without the editor target there are no materials and no
uncooked content, so the standalone `VRgridViewer.exe` asserts on startup --
run the viewer through `UnrealEditor.exe -game`, which `demo.ps1 play` does.

---

## Quick start

```powershell
.\scripts\demo.ps1 check                 # toolchain, data, vrgrid, format self-test
.\scripts\demo.ps1 bake ghosts-on -Rrd   # export the scene AND the .rrd, one pass
.\scripts\demo.ps1 both ghosts-on        # Rerun and Unreal, side by side
```

Scene names are the ones `scripts/demo.sh` already uses in the vrgrid repo —
`foveation`, `ghosts-off`, `ghosts-on`, `traffic`, `reflectivity`, `features` —
so asking for `ghosts-on` here means the same sequence, frame range and colour
layer it means there.

`-Rrd` is what makes the side-by-side honest: one pass, one `MapEngine`, one
Patchwork++ lifetime. Two separate runs would be two neighbouring truths.

For the **driving simulation** rather than the map viewer, add `-VrgMode=sim`
— see [The driving simulation](#the-driving-simulation--vrgmodesim) below.

---

## How it hangs together

```
vrgrid.run.iter_pipeline ──> MapEngine.step ──> Tee.log_frame
                                                   │
                                   ┌───────────────┴───────────────┐
                                   ▼                               ▼
                            PipelineView                     UnrealSink
                           (Rerun, .rrd)                 (scene.json,
                                                          frames/*.vrgf,
                                                          stats.jsonl)
                                                               │
                                                               ▼
                                                     AVrgSceneActor (UE 5.8)
                                                     instanced cells + points
```

`dashboard/__main__.py` drives its view through exactly two methods —
`log_frame(frame, counters, timing_ms)` and `finish()` — so `UnrealSink` is a
drop-in, and `Tee` fans one run out to both.

### Files

```
exporter/
  vrgrid_unreal/convert.py   VRgrid world -> Unreal world. The mirror lives here.
  vrgrid_unreal/format.py    the .vrgf container: chunked, little-endian
  vrgrid_unreal/sink.py      UnrealSink -- same interface as PipelineView
  vrgrid_unreal/tee.py       one run, both windows
  export_scene.py            CLI, scene presets matching scripts/demo.sh
  tests/test_format.py       round-trip + frame convention (no pytest needed)

VRgridViewer/                UE 5.8 C++ project, no .uasset of its own
  Source/.../VrgFrameReader  reads .vrgf, converts coordinates
  Source/.../VrgScene        reads scene.json, VALIDATES the conversion
  Source/.../VrgSceneActor   ISM layers, playback clock, chase camera
  Source/.../VrgGameMode     spawns the viewer onto an engine map
  Content/Python/setup_assets.py   generates the colour materials (run once)

scripts/demo.ps1             check | bake | build | materials | play | both | list
scenes/<name>/               a baked export
```

---

## The coordinate conversion

```
VRgrid world   x FORWARD, y LEFT,  z UP, RIGHT-handed, METRES
Unreal world   x FORWARD, y RIGHT, z UP, LEFT-handed,  CENTIMETRES

(x, y, z)_m  ->  (100x, -100y, 100z)_cm
```

The y negation is the whole of the handedness change. Drop it and every scene
is its own mirror image — and a mirrored map looks entirely plausible, which is
the failure `docs/frames.md` opens by warning about.

So it is written once (`convert.py`), mirrored once (`VrgFrameReader::ToUnreal`),
tested (`test_left_is_negative_y_in_unreal`), **and checked again at runtime**:
the exporter writes four test vectors into `scene.json`, and `FVrgScene::Load`
runs the C++ conversion over them and refuses to load on a mismatch. A sign
error is a startup error, not something a panelist finds.

The vectors are chosen so that flipping any single axis breaks at least one of
them — `test_conversion_vectors_would_catch_a_sign_error_on_any_axis` checks
that property directly rather than trusting the choice.

---

## The `.vrgf` format

One file per frame. 64-byte header, then self-describing chunks; see
`exporter/vrgrid_unreal/format.py` for the layout and
`VRgridViewer/Source/VRgridViewer/Private/VrgFrameReader.cpp` for the reader.

Two rules a reader must honour:

- **Unknown chunk ids are skipped by their byte length.** That is what lets the
  exporter add a layer without a C++ rebuild.
- **Present-but-empty ≠ absent.** A chunk with count 0 means "this layer is
  empty now — clear it". A missing chunk means "no new data — keep what is on
  screen". The map layers are only recomputed every `MAP_INTERVAL = 5` frames,
  so on the four frames between they are *absent*. Conflate the two and a ghost
  trail outlives the cleanup that removed it.

Why not read the `.rrd` directly: Rerun's recording format is Arrow-based and
coupled to the SDK version (`pyproject.toml` pins only `rerun-sdk>=0.16`; this
machine resolves 0.37.2). Tapping `log_frame` is less work and cannot break on
an SDK bump.

Size, measured on seq 00: ~2 MB per frame for the sweep, ~4.8 MB on a map
frame. A 160-frame `foveation` export lands around 400–500 MB, comparable to
the 278 MB `foveation.rrd` beside it.

---

## What the viewer does

- Cells as **real instanced cubes at their true edge length** — 5 cm near the
  car stepping to 40 cm at 100 m. Rerun draws them as sized points instead,
  because it processes box instances one at a time on the CPU
  (`pipeline_view.py:137`); Unreal's instanced path is built for exactly this,
  so the foveation reads far better here. That is the one thing this window can
  show that the other structurally cannot.
- Ring boundary squares and the 3.74 m blind cone tracking the vehicle.
- Chase camera that follows the vehicle's **position but not its heading** —
  copied from the Rerun blueprint, which learned it the hard way.
- Fixed-timestep playback at the manifest's `playback_fps` (10 Hz on KITTI),
  not wall clock, so it does not drift against the `.rrd` over a long scene.

Keys: `Space` pause · `←`/`→` scrub · `Home` restart · `G` ghosts · `P` points
· `F` free · `U` unknown · `C` confidence.

### The black screen, and the three bugs behind it

Worth recording, because each one reads as healthy from the outside and they
stack: fixing one changes nothing visible until all three are fixed.

1. **`bAutoManageActiveCameraTarget`** defaults to true on the player
   controller, so it re-points the view at its possessed pawn every tick and
   silently undoes `SetViewTarget`.
2. **`USpringArmComponent` rewrites its child's transform every tick.** With
   `TargetArmLength = 0` it parked the camera exactly on the vehicle -- inside
   the 3.74 m blind cone, the one region that holds no returns by
   construction. The camera is now driven directly in world space, and
   `CalcCamera` is overridden so the engine cannot fall back to the actor's
   own (never-moving, at-the-origin) transform.
3. **The material's `bUsedWithInstancedStaticMeshes` flag was never set.**
   Without it a material cannot compile for the instanced vertex factory, and
   at runtime -- where nothing recompiles -- UE substitutes the DEFAULT LIT
   material. On a map with no lights that is pure black. The editor sets this
   automatically when you drop a material on an ISM by hand, which is exactly
   why a script misses it.

What made this slow was diagnostics that logged the value we had just *asked
for* rather than the one the engine *settled on*. `VRgrid: SETTLED engine view`
is logged at screenshot time for that reason, and `-VrgShot=N` grabs a frame
and writes it to `Saved/Screenshots/` so a bake can be checked without a human
watching. `Content/Python/verify_assets.py` prints what the materials actually
contain, including `used_with_ISM`.

---

## The driving simulation (`-VrgMode=sim`)

Everything above describes the **map viewer** — the window that shows the same
cells Rerun shows. The *other* window is the driving simulation, and it is a
different thing on purpose.

> The simulation has **no bearing on any reported number**. It is the
> real-world picture that runs beside the Rerun map so an audience can see what
> the sensor is looking at. Accuracy lives entirely in the Rerun window.

What it *is* faithful to is the **route**. It follows the exact exported KITTI
trajectory — every pose from the `TRAJ` chunk of `final.vrgf`, no resampling,
no invented turns — so the bends the car takes are the bends the dataset took.
4,533 samples, 3,709 m on seq 00.

```powershell
.\scripts\demo.ps1 bake seq00-full --light      # 4,541 frames -> ~20 MB
& "$UE\Engine\Binaries\Win64\UnrealEditor.exe" .\VRgridViewer\VRgridViewer.uproject `
    -game -windowed -ResX=1600 -ResY=900 `
    -VrgMode=sim -VrgScene=.\scenes\seq00-full
```

`--light` matters: without it a full-sequence bake writes the point cloud and
the occupancy layers for every frame and lands at **13 GB**. Light mode skips
`PNTS`/`GHST`/`OCCU`/`FREE`/`UNKN` and keeps the trajectory and the ring
schedule, which is all the simulation reads.

### Keys

| | |
|---|---|
| `B` | blind spot only ⟷ all accuracy bands |
| `N` | trigger a scripted pedestrian crossing ahead of the car |

### The two things that are one continuous mesh, and why

Both started as rows of rotated boxes, and a row of rotated boxes cannot be a
smooth surface. Butt-joined they leave a wedge on the outside of every bend;
overlapped they z-fight; staggered in Z to stop the fighting they show a step
at every joint. All three read as *rectangle planks*. The fix in both cases was
to stop placing boxes.

**`RoadMesh`** — a `UProceduralMeshComponent` with eleven sections
(`BuildRoadRibbons`): carriageway, two kerbs, two footways, two edge lines, two
verges and two 90 m ground aprons. Each is a strip with one pair of vertices per route sample, so it
bends exactly where the drive bent and has no joints to mitre. A section can
`bStopAtJunctions`, which collapses it to zero width where another part of the
route passes close — a footway has to stop at a junction rather than run across
it. Each section's tint comes from a `UMaterialInstanceDynamic` of `M_VrgPed`
(`Tint`, `Rough`).

> A strip wider than the local turn radius **folds through itself** — the outer
> edge crosses the centre of curvature and the surface stands up as a crumpled
> black wall across the street. `AddRibbon` clamps each sample's lateral offset
> to `0.75 / curvature`, so a wide apron narrows through a bend instead.

> The verge strips are load-bearing. There is also one flat ground plane under
> the whole drive at `minZ − 1.2 m`, and it exists only so gaps in the frontage
> show ground instead of sky. It cannot follow the route, which climbs ~15 m —
> so without the verges the footway would end in a cliff for most of the drive.

**`BandMesh`** — the accuracy bands, rebuilt every frame by `UpdateBands` as a
single watertight 23×23 lattice in the **vehicle frame**. Two properties do the
work:

- **Shared corners.** Each band used to be an instanced box taking its height
  from the terrain under its own centre, so neighbours sat at different heights
  and the road showed through the step. One grid cannot step.
- **No overlap.** Two translucent surfaces on top of each other double the
  alpha and draw a bright seam, so the old "just overlap them" fix traded one
  artefact for another. Cells share edges instead.

Every ring boundary is an exact stop on the axis, so no cell straddles two
accuracy bands and the colour steps cleanly. Colour and opacity arrive as
**vertex colour** (`M_VrgBand`, unlit translucent), because a procedural mesh
has no instances to carry `PerInstanceCustomData`.

> **The vehicle frame is not an implementation detail.** An earlier version
> placed the bands at `PathAt(DistanceM + offset)` so they would follow the
> road's elevation — and that made the scan bend into a turn *before the car
> did*, as though the sensor knew what was coming. It does not. The bands are a
> property of the sensor relative to the vehicle: a square on the Chebyshev
> lattice, aligned to where the car is pointing now. Height still comes from
> the ground, but via `GroundZAt` — a terrain lookup under each vertex, not a
> reading of the route ahead.

### Traffic and pedestrians

Measured, not assumed: **0 ego collisions, 0 traffic collisions, 0 near passes
over 140 s**, and 32 crossings in 190 s. Getting there took four separate
fixes, each of which looked fine in isolation:

- The ego yield test covered the **whole carriageway**, so any pedestrian
  anywhere stalled the car. It is the ego lane only now.
- A hard 2.0 m/s speed floor meant the car could never clear a 1.5 m/s walker,
  so it crawled behind them forever. The floor eases to **zero inside 7.5 m**,
  and braking (4.5 m/s²) is deliberately harder than acceleration (1.8 m/s²).
- **Traffic had no pedestrian check at all** — only the ego did.
- Crossers ping-ponged across the road forever; they now dwell 8–26 s on the
  pavement between crossings, cross **perpendicular** to the kerb (drifting
  along the road meant up to 20 m of travel during a 14 s crossing), and step
  off between parked cars.

The UE mannequin is authored facing **+Y**, which is why `ACharacter` rotates
its mesh −90°. `PedMeshYawOffset = -90` is that, not a fudge: without it the
people walk sideways.

### Diagnostics

`-VrgDiag` turns on per-3-second ego telemetry (`VRgrid ego:` — speed, target,
lead vehicle, gap, pedestrian gate). It is **off by default** because the log is
noisy during a demo. The collision detectors (`VRgrid HIT:`, `VRgrid NEAR:`) are
always on and silent unless something actually goes wrong, which is the point.

`-VrgShot=N` writes a screenshot once `Elapsed > N/10` seconds, so a change can
be checked without anyone watching the window. It lands in
`VRgridViewer/Saved/Screenshots/WindowsEditor/`.

### If a surface renders in flat grey

Run `.\scripts\demo.ps1 materials`. The materials are generated by
`Content/Python/setup_assets.py` because a material graph is an asset, not
code, and cannot be compiled at runtime in a packaged game. Two traps that both
fail **silently**:

- A material used on an ISM needs `used_with_instanced_static_meshes`, or UE
  substitutes the default lit material at runtime.
- `MaterialEditingLibrary.connect_material_property` returns `False` and only
  *warns* when the named output pin does not exist. `MaterialExpressionVertexColor`
  has pins `""`, `R`, `G`, `B`, `A` — there is no `"RGB"`, and asking for one
  shipped `M_VrgBand` completely invisible the first time. The script now raises
  on a failed connection.

### Scope

Only `Downloads\Unreal-Vrgrid` is written to. The `vrgrid` clone and the GitHub
repo are read-only — no commits, no branches, no PRs.

### Known limits

- The two windows are **loop-synced, not linked**. Scrubbing Rerun does not
  move Unreal. Fine for a looping hero window; wire a frame-index link if you
  want them to follow each other.
- Stepping backward re-reads from the nearest map frame, so a back-step over a
  map boundary costs up to `MAP_INTERVAL` file loads.
- Nothing measured in this window is a result. It is the same data the Rerun
  window is showing, rendered differently.
