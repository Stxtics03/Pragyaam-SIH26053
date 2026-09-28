# Pragyaam — Foveated 2.5D LiDAR Mapping

**Adaptive Variable-Resolution 2.5D LiDAR Mapping for Dynamic Environment Perception**

[![Smart India Hackathon 2026](https://img.shields.io/badge/Smart%20India%20Hackathon-2026-orange)](https://www.sih.gov.in/)
[![PS SIH26053](https://img.shields.io/badge/PS-SIH26053-blue)](https://www.sih.gov.in/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Demo Video](https://img.shields.io/badge/Demo-Watch%20Video-FF0000?logo=youtube&logoColor=white)](https://www.youtube.com/watch?v=R3EE5J3rBpA)
[![Deck PDF](https://img.shields.io/badge/Deck-Open%20PDF-B30B00?logo=adobeacrobatreader&logoColor=white)]([assets/Pragyaam-SIH26053-deck.pdf](https://canva.link/2p4quk2cxwr24ao))

> **Pragyaam is a foveated 2.5D LiDAR mapping system that sets resolution by range, semantics and direction: fine where the sensor has data, coarse where it does not. It runs inside a memory bound fixed at startup and removes ghost trails of moving objects in real time.**

**Smart India Hackathon 2026 · SIH26053 · DRDO · Smart Vehicles · Team Chronicles.exe (178295)**

<p align="center">
  <a href="assets/pragyaam-dashboard.png"><img width="1395" height="631" alt="pragyaam-dashboard" src="https://github.com/user-attachments/assets/6ed4fb6e-985d-4260-9fab-391e6e60f343" />
</a>
  <br><sub>One run, two windows: the Unreal driving view (left) and the live Rerun dashboard (right), SemanticKITTI frame 24.</sub>
</p>

---

## The problem

A uniform grid assumes the LiDAR can fill every cell. It can't. At **50 m**, consecutive laser rings land about **10.8 m apart** on the road, so **99.87%** of a uniform 5 cm grid out there cannot receive a single return in a frame. It stores empty space.

Accumulated maps also keep **ghost trails**: stale elevation left behind by vehicles that have already moved.

## How Pragyaam solves it

Cell size grows with distance, following how far apart the laser beams land:

| Ring | Range | Cell size |
| --- | --- | ---: |
| Blind cone | 0–3.74 m | below the sensor's view |
| Ring 0 | 0–10 m | 5 cm |
| Ring 1 | 10–25 m | 10 cm |
| Ring 2 | 25–50 m | 20 cm |
| Ring 3 | 50–100 m | 40 cm |

The problem statement's own schedule (**5 / 10 / 50 cm**) is also supported as a config.

- **Fixed memory:** 745,000 cells × 12 B = **8.94 MB**, allocated once at startup. It never grows.
- **Honest coarsening:** merged cells keep their uncertainty (law of total variance). A merged kerb cell reports σ = 6.3 cm, not a false 1 cm.
- **Ghost removal:** a range-image visibility check clears stale cells, and never erases a cell holding a live return.
- **Deep learning in the loop:** FRNet labels every LiDAR point (road, car, pedestrian...).
- **One query API:** planners ask for any world coordinate; they never need to know which ring they are in.

## Results

Measured on SemanticKITTI.

| What | Result |
| --- | --- |
| Map memory | **8.94 MB** (6.24 MB on the PS schedule) |
| vs uniform 5 cm 2.5D grid (192 MB) | **21.5× smaller** |
| vs dense 5 cm 3D voxels (2.56 GB) | **286× smaller** |
| Frame time, GPU | **22.3 ms** typical / 26.7 ms worst 1% (**~45 FPS**) |
| Map + FRNet, half precision | 79.5 ms worst 1%, inside the **10 Hz** budget |
| FRNet accuracy | **90.3%** points, 65.2% mIoU (seq 08, held out) |
| Accuracy vs distance | **95.1 / 91.8 / 92.0%** at 0–10 / 10–25 / 25–50 m (terrain / static / dynamic) |
| Near-field height error | **1.59 cm** median, ring 0, all 11 labelled sequences |
| Ghost cleanup | **0 of 4,071** frames missed (seq 08) |
| Reproducibility | CPU and GPU maps **bit-identical**; same map hash on two different machines |

GPU timings are from an Low end spec GPU & CPU computation;And for pipeline tranin a Tesla T4 with RTX 5050 & 12 core CPU - gives 21.94 / 28.40 ms on the 5/10/50 schedule.

---

## Architecture

<p align="center">
  <a href="assets/pragyaam-architecture.png"><img width="4285" height="508" alt="pragyaam-architecture" src="https://github.com/user-attachments/assets/b39b1fce-fede-435b-bc9c-b495e46891e2" />
</a>
</p>

## Methodology

```mermaid
flowchart LR
  %% Pragyaam — Adaptive 2.5D LiDAR Mapping Architecture
  subgraph IN["01 · INPUT LAYER"]
    direction TB
    LIDAR["LiDAR<br/>Point Cloud"]
    DATA["SemanticKITTI<br/>Labels / Dataset"]
    POSE["Vehicle Pose<br/>& Motion"]
  end

  subgraph PER["02 · PREPROCESSING & PERCEPTION"]
    direction TB
    TF["Coordinate / Frame<br/>Transformation"]
    RANGEIMG["Range Image<br/>Projection"]
    DESKEW["Motion Compensation<br/>/ Deskewing"]
    GROUND["Ground Segmentation<br/>Patchwork++"]
    SEM["Semantic<br/>Classification"]
    DETECT["Dynamic Object<br/>Detection"]
  end

  subgraph RES["03 · ADAPTIVE RESOLUTION DECISION"]
    direction TB
    DIST["Range / Distance<br/>from Sensor"]
    IMPORTANCE["Semantic<br/>Importance"]
    STATE["Vehicle Direction<br/>& Speed"]
    CTRL{{"Adaptive Resolution<br/>Controller"}}
    POLICY["5 cm · High importance<br/>10 cm · Medium-high<br/>20 cm · Normal<br/>40 cm · Low importance"]
  end

  subgraph CORE["04 · PRAGYAAM MAPPING CORE"]
    direction TB
    GRID[["Adaptive / Foveated<br/>2.5D Grid"]]
    HEIGHT["Height & Variance<br/>Estimation"]
    OCC["Occupancy Fusion"]
    SFUSE["Semantic Fusion"]
    KF["Uncertainty-Aware<br/>Kalman Fusion"]
    SPLIT["Adaptive Grid<br/>Split & Merge"]
    MEM["Fixed Memory<br/>Allocation"]
  end

  subgraph DYN["05 · DYNAMIC OBJECT HANDLING"]
    direction TB
    DYNID["Dynamic Object<br/>Identification"]
    TRANSIENT["Transient<br/>Object Layer"]
    VIS["Range-Image<br/>Visibility Check"]
    DECAY["Temporal Decay"]
    GHOST["Ghost Artifact<br/>Removal"]
  end

  subgraph MAP["06 · MAP OUTPUT / QUERY INTERFACE"]
    direction TB
    ADAPTIVEMAP[("Published Adaptive<br/>2.5D Map")]
    QUERY["Planner Query<br/>Interface"]
    WORLD["World-Coordinate<br/>Queries"]
    TRAV["Traversability<br/>Queries"]
    DYNQ["Dynamic-Object<br/>Queries"]
    REGION{"Conservative Region<br/>SAFE / BLOCKED / MIXED"}
  end

  subgraph APP["07 · APPLICATION & OUTPUT LAYER"]
    direction TB
    DASH["Rerun / Visualization<br/>Dashboard"]
    NAV["Autonomous Navigation<br/>/ Planner"]
    PATH["Path Planning &<br/>Traversability"]
    PERF["Performance<br/>Evaluation"]
  end

  subgraph EVAL["08 · EVALUATION"]
    direction TB
    RMSE["Mapping RMSE"]
    IOU["IoU"]
    MEMORY["Memory Usage"]
    LAT["Latency"]
    REGRET["Planner Regret"]
  end

  LIDAR --> TF --> RANGEIMG --> DESKEW --> GROUND --> SEM --> DETECT
  POSE --> TF
  POSE --> DESKEW
  DATA -. labels .-> SEM

  RANGEIMG --> DIST
  SEM --> IMPORTANCE
  POSE --> STATE
  DIST & IMPORTANCE & STATE --> CTRL --> POLICY

  POLICY ==> GRID
  GROUND --> HEIGHT
  GROUND --> OCC
  SEM --> SFUSE
  GRID --> HEIGHT --> OCC --> SFUSE --> KF --> SPLIT --> MEM --> ADAPTIVEMAP

  DETECT --> DYNID --> TRANSIENT --> VIS --> DECAY --> GHOST
  GHOST --> KF
  GHOST --> ADAPTIVEMAP

  ADAPTIVEMAP --> QUERY
  QUERY --> WORLD & TRAV & DYNQ & REGION
  QUERY --> DASH & NAV & PATH & PERF

  KF -. evaluation .-> RMSE
  KF -.-> IOU
  MEM -.-> MEMORY
  QUERY -.-> LAT
  NAV -.-> REGRET

  classDef input fill:#ecfeff,stroke:#22d3ee,stroke-width:1.5px,color:#164e63;
  classDef perception fill:#f0fdfa,stroke:#2dd4bf,stroke-width:1.5px,color:#134e4a;
  classDef decision fill:#fdf4ff,stroke:#e879f9,stroke-width:3px,color:#701a75;
  classDef policy fill:#fff7ed,stroke:#fb923c,stroke-width:1.5px,color:#7c2d12;
  classDef core fill:#f5f3ff,stroke:#a78bfa,stroke-width:2px,color:#4c1d95;
  classDef innovation fill:#eef2ff,stroke:#818cf8,stroke-width:4px,color:#312e81;
  classDef dynamic fill:#fff1f2,stroke:#fb7185,stroke-width:1.5px,color:#881337;
  classDef output fill:#f0fdf4,stroke:#4ade80,stroke-width:1.5px,color:#14532d;
  classDef application fill:#f0f9ff,stroke:#38bdf8,stroke-width:1.5px,color:#0c4a6e;
  classDef metric fill:#fefce8,stroke:#facc15,stroke-width:1.5px,color:#713f12;

  class LIDAR,DATA,POSE input;
  class TF,RANGEIMG,DESKEW,GROUND,SEM,DETECT perception;
  class DIST,IMPORTANCE,STATE,CTRL decision;
  class POLICY policy;
  class HEIGHT,OCC,SFUSE,KF,SPLIT,MEM core;
  class GRID innovation;
  class DYNID,TRANSIENT,VIS,DECAY,GHOST dynamic;
  class ADAPTIVEMAP,QUERY,WORLD,TRAV,DYNQ,REGION output;
  class DASH,NAV,PATH,PERF application;
  class RMSE,IOU,MEMORY,LAT,REGRET metric;
```

---

## Quick start

```bash
git clone https://github.com/Stxtics03/Pragyaam-SIH26053.git
cd Pragyaam-SIH26053
pip install -e ".[dev,dash,perception]"

# point at the folder holding poses/ and sequences/
export VRGRID_DATA_ROOT=$PWD/data/dataset

make test                                   # 667 tests
python -m vrgrid.run --seq 08 --schedule 5/10/20/40 --viz
```

The Python package is still named `vrgrid`, from the project's earlier name.

**Demo scenes** (one command each, see `docs/demo-runbook.md`):

```bash
./scripts/demo.sh check        # preflight: venv, data, Rerun, Patchwork++
./scripts/demo.sh foveation    # rings filling in around the car
./scripts/demo.sh ghosts-on    # same frames as ghosts-off, trails removed
./scripts/demo.sh traffic      # dense moving traffic, seq 07
```

**GPU:** install CuPy for your CUDA version (`pip install cupy-cuda12x`) and run with `--device cuda`. See `docs/gpu-lane/`.

**Unreal viewer:** the companion 3D driving view lives in [`unreal/`](unreal/).

## Tech stack

Python 3.11 · NumPy · CuPy + custom CUDA kernels · PyTorch (FRNet) · Patchwork++ · Rerun · Unreal Engine 5.8 · pytest

## Repository layout

```text
src/          grid, gpu, perception, eval, run  (the mapping engine)
dashboard/    Rerun dashboard
unreal/       Unreal Engine companion viewer
configs/      ring schedules and frozen thresholds
scripts/      every script behind a reported number, plus demo.sh
tests/        667 tests, including determinism and partition gates
docs/         architecture, maths, evaluation, limitations, research log
```

## Documentation

| Topic | File |
| --- | --- |
| Architecture and scope | [`docs/master-v4.md`](docs/master-v4.md) |
| Maths and invariants | [`docs/sih-math.md`](docs/sih-math.md) |
| Evaluation metrics | [`docs/eval-metric-specs.md`](docs/eval-metric-specs.md) |
| Related work | [`docs/related-work-final-section.md`](docs/related-work-final-section.md) |
| Research log | [`docs/research-log.md`](docs/research-log.md) |

We build on published prior art (Triebel 2006, Droeschel 2014, Losasso 2004, OctoMap, Psomiadis 2024). Our claim is the combination: range + semantics under a hard memory bound, uncertainty-preserving coarsening, planner-regret evaluation and determinism.

---

## Team Chronicles.exe

**Smart India Hackathon 2026 · SIH26053 · DRDO · Smart Vehicles · Software**

Built on SemanticKITTI, KITTI, Patchwork++, FRNet and Rerun. Licensed under [MIT](LICENSE).

<p align="center"><b>Pragyaam</b> · <i>Less memory. Same near-field detail. Better decisions.</i></p>
