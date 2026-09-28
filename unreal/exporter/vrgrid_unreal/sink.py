"""`UnrealSink` -- a second renderer fed from the same call as the Rerun one.

`dashboard/__main__.py` drives the view through exactly two methods:

    view.log_frame(frame, counters=counters, timing_ms={...})   # per frame
    view.finish()                                               # once, at the end

So anything with those two is a drop-in, and `tee.Tee` fans one run out to both
`PipelineView` and this. That is the whole reason the two windows cannot drift:
they are not two renderings of similar data, they are two renderings of the
same call, off the same `MapEngine` state, on the same frame index.

WHAT THIS DOES NOT TOUCH. Everything that produces a number -- the map hash,
the latency table, per-ring RMSE, rho -- happens upstream in `MapEngine.step`.
This sink runs strictly after it, and `dashboard/__main__.py` computes
`timing_ms` BEFORE calling `log_frame`, so the export is outside the timed
window. Do not move an export call between `t_frame` and `t_step`: that is the
one way a renderer can change a reported latency.

Colours come from `pipeline_view` rather than being restated here. The CVD
audit (`dashboard/cvd.py`, gated in CI) checks those constants, and a second
copy would drift the two windows apart one palette tweak at a time.

Output layout:

    <out>/scene.json        manifest: rings, blind cone, fps, frame range,
                            and the conversion vectors the C++ reader checks
                            itself against at load
    <out>/frames/%06d.vrgf  one binary frame -- see format.py
    <out>/stats.jsonl       one JSON object per frame, for the HUD

Absent chunk vs empty chunk, because Unreal has to tell them apart:

    chunk present, count 0   this layer IS empty now -- clear the instances
    chunk absent             no new data this frame -- keep what is on screen

The map layers are recomputed every `map_interval` frames (`MAP_INTERVAL = 5`
in `pipeline_view`, for reasons that are about the viewer, not the map), so on
the four frames between they are absent, not empty. Rerun says the same thing
with `rr.Clear`; this is that statement in a file format.
"""

import json
import os

import numpy as np
from vrgrid.cell import CELL_BYTES, OCC_FREE, OCC_UNKNOWN
from vrgrid.grid.quantise import dequantise_variance_cm2

from . import convert
from . import format as fmt

# Private imports, on purpose -- see the module docstring. Guarded so the
# failure names the cause: without `rerun` installed there is no Rerun window
# to run beside, which is a different problem from a missing vrgrid.
try:
    from vrgrid.dash.pipeline_view import (
        MAP_INTERVAL,
        _CURB_RGBA,
        _FREE_RGBA,
        _POTHOLE_RGBA,
        _UNKNOWN_RGBA,
        _confidence_ramp,
        _height_ramp,
        get_display_points,
    )
    from vrgrid.dash.palettes import GHOST_RGB
except ImportError as exc:                                  # pragma: no cover
    raise ImportError(
        "vrgrid.dash.pipeline_view could not be imported, so the Unreal export "
        "cannot share the Rerun view's colours. Install the dashboard extra: "
        "pip install -e '.[dash]' in the vrgrid checkout."
    ) from exc


class UnrealSink:
    """Writes one `.vrgf` per frame plus a manifest and a stats stream."""

    def __init__(self, schedule, out_dir, engine=None, color_by="class",
                 palette="semantickitti", ghost_removal=True, features=False,
                 map_interval=MAP_INTERVAL, seq=None, scene=None,
                 light=False):
        self.sched = schedule
        self.engine = engine
        self.color_by = color_by
        self.palette = palette
        self.ghost_removal = ghost_removal
        self.features = features
        self.map_interval = max(1, int(map_interval))
        self.seq = seq
        self.scene = scene
        # LIGHT: header, trajectory and the manifest only.
        #
        # The Unreal view draws the accuracy bands and the blind spot, both of
        # which come from `scene.json`, plus the per-frame vehicle pose from
        # each header. The sweep and the cell layers are off. Those layers are
        # ~2.9 MB a frame and everything else is ~100 bytes, so a full-length
        # drive is a few megabytes in this mode and 13 GB without it. Bake
        # heavy only when `bShowCells` or `bShowSweep` will be turned on.
        self.light = bool(light)

        self.out_dir = os.path.abspath(out_dir)
        self.frames_dir = os.path.join(self.out_dir, "frames")
        os.makedirs(self.frames_dir, exist_ok=True)

        self._frames_logged = 0
        self._first_index = None
        self._last_index = None
        self._trail = []
        self._last_frame = None
        self._stats_path = os.path.join(self.out_dir, "stats.jsonl")
        self._stats = open(self._stats_path, "w", encoding="utf-8")
        self._write_manifest()

    # --- readouts, mirroring PipelineView -----------------------------------
    #
    # Small enough to restate and load-bearing enough to want here rather than
    # reached for through a viewer object: this sink must work with `engine`
    # and no Rerun view at all.

    def _cell_m_per_slot(self, slots):
        """Cell edge length (m) per slot, from the ring it lives in."""
        out = np.full(len(slots), self.sched.base_cell_m, dtype=np.float32)
        for layout in self.engine.handle.rings:
            sel = (slots >= layout.offset) & (slots < layout.offset + layout.slots)
            out[sel] = layout.cell_m
        return out

    def _centres_world(self, slots):
        """World-frame `(x, y, z)` for arbitrary slots. ego (0, 0) leaves the
        centres in the world frame -- the 2-vector is what keeps z in WORLD,
        which is what a map view draws (see `MapEngine._centres`)."""
        n = len(slots)
        x, y, z = np.zeros(n), np.zeros(n), np.zeros(n)
        if n:
            self.engine._centres(slots, np.zeros(2), x, y, z)
        return x, y, z

    def _ring_slices(self):
        out = []
        for layout in self.engine.handle.rings:
            out.append((slice(layout.offset, layout.offset + layout.slots),
                        layout.side))
        return out

    def _flat(self, level, slot):
        """Curb/pothole slots are indices WITHIN a ring window; lift them to
        flat SoA slots or ring 0's slot 5 aliases onto ring 3's."""
        return slot.astype(np.int64) + self.engine.handle.rings[level].offset

    # --- layers --------------------------------------------------------------

    def _sigma_cm(self, slots):
        """One sigma of the cell's height estimate, in cm.

        `height_variance` is log-quantised into a byte and the code runs the
        OTHER way -- 0 is "no information", 255 is the quantisation floor --
        so it has to go through `dequantise_variance_cm2` rather than being
        scaled. Getting that backwards would draw the most confident cells as
        the least.
        """
        if not len(slots):
            return np.zeros(0, np.float32)
        code = self.engine.handle.grid["height_variance"][slots]
        return np.sqrt(dequantise_variance_cm2(code)).astype(np.float32)

    def _occupied_chunk(self):
        slots, x, y, z = self.engine.occupied_cells()
        self._last_occupied_n = len(slots)
        cell_m = self._cell_m_per_slot(slots) if len(slots) else np.zeros(0, np.float32)
        rgb = _height_ramp(z) if len(slots) else np.zeros((0, 3), np.uint8)
        rgba = (np.concatenate([np.asarray(rgb, np.uint8),
                                np.full((len(slots), 1), 255, np.uint8)], axis=1)
                if len(slots) else np.zeros((0, 4), np.uint8))
        return fmt.pack_cells(x, y, z, cell_m, rgba, self._sigma_cm(slots))

    def _state_chunk(self, state_value, require_observed=False):
        sel = self.engine.occ_state == state_value
        if require_observed:
            sel &= self.engine.handle.grid["obs_count"] > 0
        slots = np.flatnonzero(sel)
        x, y, z = self._centres_world(slots)
        cell_m = self._cell_m_per_slot(slots) if len(slots) else np.zeros(0, np.float32)
        rgba = _FREE_RGBA if state_value == OCC_FREE else _UNKNOWN_RGBA
        return fmt.pack_cells(x, y, z, cell_m,
                              np.array([rgba], np.uint8) if len(slots)
                              else np.zeros((0, 4), np.uint8),
                              self._sigma_cm(slots))

    def _feature_chunks(self):
        """§7.4 curbs and potholes as boxes, §7.5 confidence as floated tiles.

        Curbs stand UP at their measured rise and potholes sink DOWN to their
        measured depth, exactly as the Rerun view draws them -- the height is
        the thing a 2D grid loses, so drawing it at magnitude is the difference
        between showing a detection and showing a measurement.
        """
        from vrgrid.grid.confidence import drivable_confidence
        from vrgrid.grid.features import detect

        rings = self._ring_slices()
        curbs, holes = detect(self.engine.handle.grid, self.sched, rings,
                              self.engine.thresholds, buffers=self.engine.buffers)

        def boxes(groups, sign, field):
            cent, half = [], []
            for level, g in enumerate(groups):
                if not len(g):
                    continue
                x, y, z = self._centres_world(self._flat(level, g.slot))
                d = np.maximum(getattr(g, field).astype(np.float32), 1.0) / 100.0
                cent.append(np.stack([x, y, z + sign * d / 2.0], axis=1))
                half.append(np.stack([np.full_like(d, g.cell_m / 2.0),
                                      np.full_like(d, g.cell_m / 2.0),
                                      d / 2.0], axis=1))
            if not cent:
                return np.empty((0, 3), np.float32), np.empty((0, 3), np.float32)
            return np.concatenate(cent), np.concatenate(half)

        cc, ch = boxes(curbs, +1.0, "height_cm")
        pc, ph = boxes(holes, -1.0, "depth_cm")

        cent, cols, sizes = [], [], []
        for level, (sl, side) in enumerate(rings):
            cell_m = self.sched.rings[level].cell_m
            conf = drivable_confidence(self.engine.handle.grid, sl, side, cell_m,
                                       self.engine.thresholds)
            seen = np.flatnonzero(self.engine.handle.grid["obs_count"][sl] >= 1)
            if not seen.size:
                continue
            x, y, z = self._centres_world(seen + self.engine.handle.rings[level].offset)
            cent.append(np.stack([x, y, z + 0.15], axis=1))   # floated: z-fights at the surface
            cols.append(_confidence_ramp(conf[seen]))
            sizes.append(np.full(seen.size, cell_m, np.float32))

        if cent:
            cf = np.concatenate(cent)
            cc_rgb = np.asarray(np.concatenate(cols), np.uint8)
            conf_chunk = fmt.pack_cells(
                cf[:, 0], cf[:, 1], cf[:, 2], np.concatenate(sizes),
                np.concatenate([cc_rgb, np.full((len(cc_rgb), 1), 210, np.uint8)], axis=1))
        else:
            conf_chunk = fmt.pack_cells([], [], [], [], np.zeros((0, 4), np.uint8))

        return {
            b"CURB": fmt.pack_boxes(cc, ch, np.array([_CURB_RGBA], np.uint8)),
            b"POTH": fmt.pack_boxes(pc, ph, np.array([_POTHOLE_RGBA], np.uint8)),
            b"CONF": conf_chunk,
        }

    # --- the two methods the driver calls ------------------------------------

    def log_frame(self, frame, counters=None, timing_ms=None):
        if self._first_index is None:
            self._first_index = int(frame.index)
        self._last_index = int(frame.index)
        self._last_frame = frame

        chunks = {}

        # LIGHT skips the two heavy layers entirely. They are ~2.9 MB a
        # frame against ~100 bytes for everything else, and the Unreal
        # view does not read them -- it draws the accuracy bands from
        # scene.json and the pose from each header.
        if not self.light:
            xyz, rgb = get_display_points(frame, self.ghost_removal, self.color_by,
                                          self.palette)
            chunks[b"PNTS"] = fmt.pack_points(xyz, rgb)

            # The removed set on its own chunk -- this is what the demo toggles,
            # and it is a separate entity in Rerun for the same reason.
            ghosts = frame.points_world[frame.moving].astype(np.float32)
            chunks[b"GHST"] = fmt.pack_points(ghosts, np.array([GHOST_RGB], np.uint8))

        is_map_frame = ((not self.light) and self.engine is not None
                        and self._frames_logged % self.map_interval == 0)
        if is_map_frame:
            chunks[b"OCCU"] = self._occupied_chunk()       # refreshes occ_state
            chunks[b"FREE"] = self._state_chunk(OCC_FREE)
            chunks[b"UNKN"] = self._state_chunk(OCC_UNKNOWN, require_observed=True)

        self._trail.append(np.asarray(frame.vehicle_xyz_world, np.float32))
        if is_map_frame or (self.light and self._frames_logged % 20 == 0):
            chunks[b"TRAJ"] = fmt.pack_vec3(np.stack(self._trail))

        # Yaw the same way `pipeline_view.log_frame` derives it: the pose's
        # camera-z is forward, rotated into the z-up world convention.
        fwd_world = frame.pose[:3, :3] @ np.array([0.0, 0.0, 1.0])
        yaw = float(np.arctan2(-fwd_world[0], fwd_world[2]))

        path = os.path.join(self.frames_dir, f"{frame.index:06d}.vrgf")
        fmt.write_frame(path, frame.index, frame.vehicle_xyz_world, yaw,
                        frame.index / self._fps(), chunks)

        self._write_stats(frame, counters, timing_ms, yaw, chunks)
        self._frames_logged += 1

    def finish(self):
        """One more pass so the export ends on the true final state, whichever
        frame the run stopped on -- the frame a still gets taken from."""
        if self.engine is not None and self._last_frame is not None:
            chunks = {
                b"OCCU": self._occupied_chunk(),
                b"FREE": self._state_chunk(OCC_FREE),
                b"UNKN": self._state_chunk(OCC_UNKNOWN, require_observed=True),
                b"TRAJ": fmt.pack_vec3(np.stack(self._trail)) if self._trail
                else fmt.pack_vec3(np.zeros((0, 3), np.float32)),
            }
            if self.features:
                chunks.update(self._feature_chunks())
            f = self._last_frame
            fwd_world = f.pose[:3, :3] @ np.array([0.0, 0.0, 1.0])
            yaw = float(np.arctan2(-fwd_world[0], fwd_world[2]))
            fmt.write_frame(os.path.join(self.frames_dir, "final.vrgf"),
                            f.index, f.vehicle_xyz_world, yaw,
                            f.index / self._fps(), chunks)
        self._stats.close()
        self._write_manifest()          # rewritten with the real frame range

    # --- manifest and stats --------------------------------------------------

    def _fps(self):
        from vrgrid.dash._config import playback_fps
        return playback_fps()

    def _write_manifest(self):
        from vrgrid.dash._config import blind_cone_radius_m, playback_fps
        manifest = {
            "format": "vrgrid-unreal-scene",
            "format_version": fmt.FORMAT_VERSION,
            "scene": self.scene,
            "sequence": self.seq,
            "color_by": self.color_by,
            "palette": self.palette,
            "ghost_removal": self.ghost_removal,
            "features": self.features,
            "map_interval": self.map_interval,
            "light": self.light,
            "playback_fps": playback_fps(),
            "frame_first": self._first_index,
            "frame_last": self._last_index,
            "frame_count": self._frames_logged,
            "schedule": {
                "name": self.sched.name,
                "base_cell_m": self.sched.base_cell_m,
                "total_cells": self.sched.total_cells,
                "cell_bytes": CELL_BYTES,
                "map_mb": self.sched.total_cells * CELL_BYTES / 1e6,
                "vertical_extent_m": list(self.sched.vertical_extent_m),
                "rings": [
                    {"ring": r.ring, "half_width_m": r.half_width_m,
                     "cell_m": r.cell_m, "cells": r.cells}
                    for r in self.sched.rings
                ],
            },
            "blind_cone_m": blind_cone_radius_m(),
            # VRgrid world is x fwd / y LEFT / z up, right-handed, metres.
            # Unreal is x fwd / y RIGHT / z up, left-handed, centimetres. The
            # reader runs its own conversion over these and refuses to load on
            # a mismatch -- a mirrored map is invisible by inspection, so it
            # has to be caught at startup rather than on stage.
            "coordinate_note": ("file is VRgrid world metres (x fwd, y LEFT, z up, "
                                "right-handed); Unreal is cm, y RIGHT, left-handed"),
            "conversion_test_vectors": convert.conversion_test_vectors(),
        }
        tmp = os.path.join(self.out_dir, "scene.json.tmp")
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(manifest, f, indent=2)
        os.replace(tmp, os.path.join(self.out_dir, "scene.json"))

    def _write_stats(self, frame, counters, timing_ms, yaw, chunks):
        timing_ms = timing_ms or {}
        row = {
            "frame": int(frame.index),
            "time_s": frame.index / self._fps(),
            "vehicle_xyz_m": [float(v) for v in frame.vehicle_xyz_world],
            "yaw_rad": yaw,
            "n_ghosts": int(len(chunks[b"GHST"])) if b"GHST" in chunks else 0,
            "perception_ms": float(timing_ms.get("perception", float("nan"))),
            "engine_ms": float(timing_ms.get("engine", float("nan"))),
            "ground_method": getattr(frame, "ground_method", None),
        }
        if b"PNTS" in chunks:
            row["n_points"] = int(len(chunks[b"PNTS"]))
        if b"OCCU" in chunks:
            row["n_occupied"] = int(len(chunks[b"OCCU"]))
            row["n_free"] = int(len(chunks[b"FREE"]))
            row["n_unknown"] = int(len(chunks[b"UNKN"]))
        if counters is not None:
            # `truncated` is the one that matters on a reportable run: a
            # truncated cell keeps its occupancy and is never tested, so a
            # ghost among them is permanent and `cleared` cannot show it.
            row["counters"] = {
                "points": int(counters.points),
                "cells_touched": int(counters.cells_touched),
                "occupied": int(counters.occupied),
                "cleared": int(counters.cleared),
                "protected": int(counters.protected),
                "truncated": int(counters.truncated),
            }
        self._stats.write(json.dumps(row) + "\n")
        self._stats.flush()
