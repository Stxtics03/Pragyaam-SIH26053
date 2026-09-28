"""Bake a demo scene for the Unreal viewer -- and optionally the `.rrd` too.

    python exporter/export_scene.py --scene foveation --out ../scenes/foveation
    python exporter/export_scene.py --scene ghosts-on --frames 20 --rrd

Scene names and their arguments are the ones `scripts/demo.sh` already uses, so
the two demos speak the same vocabulary: ask for `ghosts-on` here and you get
the same sequence, frame range and colour layer the Rerun scene has.

`--rrd` writes the Rerun recording in the SAME pass, through `tee.Tee`. Use it
whenever both windows will be shown together: one pass, one MapEngine, one
Patchwork++ lifetime, one frame index. Two separate runs would be two
neighbouring truths rather than the same one.

Nothing here changes a reported number. The engine runs exactly as
`vrgrid.dash` runs it; this only adds a consumer of what it produced.
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from vrgrid_unreal.sink import UnrealSink          # noqa: E402
from vrgrid_unreal.tee import Tee                  # noqa: E402

# scene -> the same arguments `scripts/demo.sh: scene_args()` gives vrgrid.dash.
# Kept as data, and checked against that file when either changes: a scene that
# means one thing in Rerun and another in Unreal is worse than no scene.
SCENES = {
    "foveation":    dict(seq="00", start_frame=0,    frames=160, color_by="class"),
    "ghosts-off":   dict(seq="00", start_frame=0,    frames=60,  color_by="motion",
                         show_ghosts=True),
    "ghosts-on":    dict(seq="00", start_frame=0,    frames=60,  color_by="motion"),
    "traffic":      dict(seq="07", start_frame=650,  frames=50,  color_by="motion"),
    "reflectivity": dict(seq="00", start_frame=4420, frames=40,  color_by="reflectivity"),
    "features":     dict(seq="00", start_frame=0,    frames=40,  color_by="class",
                         features=True),
}


def main(argv=None):
    p = argparse.ArgumentParser(prog="export_scene")
    p.add_argument("--scene", choices=sorted(SCENES), default=None,
                   help="a named demo scene; overridden by the flags below")
    p.add_argument("--seq", default=None)
    p.add_argument("--frames", type=int, default=None)
    p.add_argument("--start-frame", type=int, default=None)
    p.add_argument("--color-by", default=None,
                   choices=["intensity", "class", "motion", "ground", "reflectivity"])
    p.add_argument("--palette", default="semantickitti",
                   choices=["semantickitti", "groups"])
    p.add_argument("--schedule", default="5/10/20/40")
    p.add_argument("--show-ghosts", action="store_true",
                   help="keep moving points in the cloud AND stop the map's "
                        "visibility cleanup, so the trails stay in the cells")
    p.add_argument("--features", action="store_true",
                   help="also export the curb/pothole (7.4) and confidence (7.5) layers")
    p.add_argument("--map-interval", type=int, default=None,
                   help="frames between map redraws (default: pipeline_view.MAP_INTERVAL)")
    p.add_argument("--out", default=None, help="output directory (default: scenes/<scene>)")
    p.add_argument("--rrd", nargs="?", const=True, default=None,
                   help="also write the Rerun recording, in the same pass")
    p.add_argument("--light", action="store_true",
                   help="header + trajectory only, no sweep or cell layers. "
                        "~100 bytes a frame instead of ~2.9 MB, which is what "
                        "makes a full-length drive affordable. The Unreal "
                        "accuracy bands and blind spot do not need the heavy "
                        "layers; Rerun and bShowCells do.")
    p.add_argument("--no-patchworkpp", action="store_true")
    args = p.parse_args(argv)

    preset = dict(SCENES.get(args.scene, {})) if args.scene else {}
    seq = args.seq or preset.get("seq")
    if seq is None:
        p.error("need --scene or --seq")
    frames = args.frames if args.frames is not None else preset.get("frames")
    start_frame = (args.start_frame if args.start_frame is not None
                   else preset.get("start_frame", 0))
    color_by = args.color_by or preset.get("color_by", "class")
    show_ghosts = args.show_ghosts or preset.get("show_ghosts", False)
    features = args.features or preset.get("features", False)

    out = args.out or os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
        "scenes", args.scene or f"seq{seq}")

    if not os.environ.get("VRGRID_DATA_ROOT"):
        # The same trap `scripts/demo.sh` resolves and never lets you type: the
        # loader wants the directory HOLDING poses/ and sequences/, which in
        # the working clone is data/dataset, not data.
        p.error("VRGRID_DATA_ROOT is not set. Point it at the directory that "
                "holds poses/ and sequences/ (in the vrgrid clone that is "
                "data/dataset, NOT data).")

    from vrgrid.grid import schedule as schedule_mod
    from vrgrid.run.__main__ import iter_pipeline
    from vrgrid.run.engine import MapEngine

    sched = schedule_mod.load(args.schedule)
    engine = MapEngine(sched, ghost_removal=not show_ghosts)

    views = []
    rrd_path = None
    if args.rrd:
        from vrgrid.dash.pipeline_view import PipelineView
        rrd_path = (os.path.join(out, f"{args.scene or seq}.rrd")
                    if args.rrd is True else args.rrd)
        os.makedirs(os.path.dirname(os.path.abspath(rrd_path)), exist_ok=True)
        views.append(PipelineView(sched, spawn=False, save_path=rrd_path,
                                  color_by=color_by, ghost_removal=not show_ghosts,
                                  palette=args.palette, engine=engine,
                                  features=features))

    sink_kwargs = dict(engine=engine, color_by=color_by, palette=args.palette,
                       ghost_removal=not show_ghosts, features=features,
                       seq=seq, scene=args.scene, light=args.light)
    if args.map_interval is not None:
        sink_kwargs["map_interval"] = args.map_interval
    sink = UnrealSink(sched, out, **sink_kwargs)
    views.append(sink)

    view = Tee(*views)

    n = 0
    t0 = time.perf_counter()
    t_pull = time.perf_counter()
    for frame in iter_pipeline(seq, frames, use_patchworkpp=not args.no_patchworkpp,
                               start_frame=start_frame):
        t_frame = time.perf_counter()
        counters = engine.step(frame)
        t_step = time.perf_counter()
        # timing_ms is computed BEFORE the views run, exactly as
        # dashboard/__main__.py does it -- the export stays outside the window
        # any latency number is read from.
        view.log_frame(frame, counters=counters,
                       timing_ms={"perception": (t_frame - t_pull) * 1e3,
                                  "engine": (t_step - t_frame) * 1e3})
        n += 1
        if n % 20 == 0:
            print(f"  {n} frames ...", flush=True)
        t_pull = time.perf_counter()
    view.finish()

    total_mb = sum(
        os.path.getsize(os.path.join(sink.frames_dir, f))
        for f in os.listdir(sink.frames_dir)) / 1e6
    print(f"\n{n} frames from sequence {seq} (from {start_frame}) "
          f"in {time.perf_counter() - t0:.1f}s")
    print(f"  {out}")
    print(f"  frames/   {n + 1} files, {total_mb:.1f} MB")
    print(f"  scene.json, stats.jsonl")
    if rrd_path:
        print(f"  {rrd_path}  ({os.path.getsize(rrd_path) / 1e6:.1f} MB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
