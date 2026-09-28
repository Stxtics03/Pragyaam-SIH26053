"""The `.vrgf` frame container -- one file per frame, read by the UE module.

Why a container and not `.rrd`: Rerun's recording format is Arrow-based and
coupled to the SDK version, and `vrgrid`'s `pyproject.toml` pins only
`rerun-sdk>=0.16` (this machine resolves 0.37.2). Decoding it in C++ would be
more work than this and would break on an SDK bump. `PipelineView.log_frame`
is the seam instead -- the sink taps the same per-frame call the Rerun view is
fed from, so the two windows cannot be showing different state.

Layout, little-endian throughout, every offset 4-byte aligned:

    HEADER (64 B)
      char   magic[4]        "VRGF"
      uint32 version         FORMAT_VERSION
      int32  frame_index     the SEQUENCE frame number, not the loop counter
      uint32 chunk_count
      float  vehicle_xyz[3]  metres, VRgrid world
      float  vehicle_yaw     radians, right-handed about +z
      float  time_s          frame_index / playback_fps
      byte   reserved[24]

    CHUNK * chunk_count
      char   id[4]           see CHUNK_IDS
      uint32 count           items
      uint32 stride          bytes per item
      uint32 byte_length     count * stride
      byte   payload[byte_length]

A reader MUST skip chunks whose id it does not know, by `byte_length` -- that
is what lets a later export add a layer without a C++ rebuild.

Coordinates in the file are VRgrid world METRES, exactly as the engine
produced them. The conversion to Unreal's left-handed centimetres happens once
on the C++ side (`convert.py` is the reference, and the manifest carries test
vectors the reader checks itself against at load). Converting here instead
would put a silent mirror inside a binary nobody can inspect.
"""

import struct

import numpy as np

MAGIC = b"VRGF"
FORMAT_VERSION = 2
HEADER_BYTES = 64
CHUNK_HEADER_BYTES = 16

# --- item layouts ------------------------------------------------------------
#
# uint8 colour rather than float: the dense chunks dominate the file, and a
# 450,000-cell frame is 9 MB at 20 B/cell against 14 MB at 32. Unreal reads
# them straight into a per-instance colour, which is 8-bit anyway.

POINT_DTYPE = np.dtype([
    ("x", "<f4"), ("y", "<f4"), ("z", "<f4"),
    ("r", "u1"), ("g", "u1"), ("b", "u1"), ("a", "u1"),
])                                                          # 16 B

CELL_DTYPE = np.dtype([
    ("x", "<f4"), ("y", "<f4"), ("z", "<f4"),
    ("cell_m", "<f4"),
    # Height uncertainty, one sigma in CENTIMETRES, decoded from the cell's
    # log-quantised `height_variance` byte. This is what lets the viewer draw
    # a cell the map is unsure about differently from one it is confident in --
    # the map has always known this and it was simply never leaving the engine.
    ("sigma_cm", "<f4"),
    ("r", "u1"), ("g", "u1"), ("b", "u1"), ("a", "u1"),
])                                                          # 24 B

BOX_DTYPE = np.dtype([
    ("x", "<f4"), ("y", "<f4"), ("z", "<f4"),
    ("hx", "<f4"), ("hy", "<f4"), ("hz", "<f4"),
    ("r", "u1"), ("g", "u1"), ("b", "u1"), ("a", "u1"),
])                                                          # 28 B

VEC3_DTYPE = np.dtype([("x", "<f4"), ("y", "<f4"), ("z", "<f4")])   # 12 B

# --- chunk ids ---------------------------------------------------------------

CHUNK_IDS = {
    b"PNTS": POINT_DTYPE,   # world/points      the sweep, ghost-free
    b"GHST": POINT_DTYPE,   # world/ghosts      the moving returns, toggled
    b"OCCU": CELL_DTYPE,    # world/map/occupied
    b"FREE": CELL_DTYPE,    # world/map/free
    b"UNKN": CELL_DTYPE,    # world/map/unknown (observed, still unknown)
    b"CURB": BOX_DTYPE,     # world/map/curbs
    b"POTH": BOX_DTYPE,     # world/map/potholes
    b"CONF": CELL_DTYPE,    # world/map/confidence
    b"TRAJ": VEC3_DTYPE,    # world/trajectory  the path driven so far
}


def pack_points(xyz, rgb, alpha=255):
    """(N,3) float + (N,3) uint8 -> a POINT_DTYPE array."""
    xyz = np.asarray(xyz, dtype=np.float32).reshape(-1, 3)
    out = np.empty(len(xyz), dtype=POINT_DTYPE)
    if len(xyz):
        out["x"], out["y"], out["z"] = xyz[:, 0], xyz[:, 1], xyz[:, 2]
        rgb = np.asarray(rgb, dtype=np.uint8).reshape(-1, 3)
        if len(rgb) == 1 and len(xyz) != 1:
            rgb = np.repeat(rgb, len(xyz), axis=0)
        out["r"], out["g"], out["b"] = rgb[:, 0], rgb[:, 1], rgb[:, 2]
        out["a"] = alpha
    return out


def pack_cells(x, y, z, cell_m, rgba, sigma_cm=None):
    """Per-cell centre + its ring's edge length + colour -> a CELL_DTYPE array.

    `cell_m` is what makes the foveation visible -- a cell drawn at its own
    size steps 5 -> 10 -> 20 -> 40 cm outward -- so it travels per item rather
    than being re-derived downstream from whichever ring a position falls in.
    The ring windows shift with the vehicle, and a reader that re-derived the
    size would disagree with the engine at the boundaries.
    """
    n = len(x)
    out = np.empty(n, dtype=CELL_DTYPE)
    if n:
        out["x"] = np.asarray(x, dtype=np.float32)
        out["y"] = np.asarray(y, dtype=np.float32)
        out["z"] = np.asarray(z, dtype=np.float32)
        out["cell_m"] = np.asarray(cell_m, dtype=np.float32)
        # 0 reads as "not reported" rather than "perfectly certain", which is
        # the safer default for a layer that has no variance of its own.
        out["sigma_cm"] = (0.0 if sigma_cm is None
                           else np.asarray(sigma_cm, dtype=np.float32))
        rgba = np.asarray(rgba, dtype=np.uint8).reshape(-1, 4)
        if len(rgba) == 1 and n != 1:
            rgba = np.repeat(rgba, n, axis=0)
        out["r"], out["g"], out["b"], out["a"] = (
            rgba[:, 0], rgba[:, 1], rgba[:, 2], rgba[:, 3])
    return out


def pack_boxes(centres, half_extents, rgba):
    """(N,3) centre + (N,3) half-extent + colour -> a BOX_DTYPE array."""
    c = np.asarray(centres, dtype=np.float32).reshape(-1, 3)
    out = np.empty(len(c), dtype=BOX_DTYPE)
    if len(c):
        h = np.asarray(half_extents, dtype=np.float32).reshape(-1, 3)
        if len(h) == 1 and len(c) != 1:
            h = np.repeat(h, len(c), axis=0)
        out["x"], out["y"], out["z"] = c[:, 0], c[:, 1], c[:, 2]
        out["hx"], out["hy"], out["hz"] = h[:, 0], h[:, 1], h[:, 2]
        rgba = np.asarray(rgba, dtype=np.uint8).reshape(-1, 4)
        if len(rgba) == 1 and len(c) != 1:
            rgba = np.repeat(rgba, len(c), axis=0)
        out["r"], out["g"], out["b"], out["a"] = (
            rgba[:, 0], rgba[:, 1], rgba[:, 2], rgba[:, 3])
    return out


def pack_vec3(xyz):
    xyz = np.asarray(xyz, dtype=np.float32).reshape(-1, 3)
    out = np.empty(len(xyz), dtype=VEC3_DTYPE)
    if len(xyz):
        out["x"], out["y"], out["z"] = xyz[:, 0], xyz[:, 1], xyz[:, 2]
    return out


# --- container ---------------------------------------------------------------


def write_frame(path, frame_index, vehicle_xyz, vehicle_yaw, time_s, chunks):
    """Write one `.vrgf`.

    `chunks` is `{b"PNTS": array, ...}`. An EMPTY layer is written with count 0
    rather than dropped, so a reader can tell "this layer is empty this frame"
    from "this export does not carry that layer" -- which is the difference
    between clearing the instances and leaving last frame's on screen. Rerun
    gets this right by logging `rr.Clear`; this is the same statement.
    """
    vx, vy, vz = (float(v) for v in vehicle_xyz)
    header = bytearray(HEADER_BYTES)
    struct.pack_into(
        "<4sIiI5fI", header, 0,
        MAGIC, FORMAT_VERSION, int(frame_index), len(chunks),
        vx, vy, vz, float(vehicle_yaw), float(time_s), 0,
    )
    with open(path, "wb") as f:
        f.write(header)
        for cid, arr in chunks.items():
            if cid not in CHUNK_IDS:
                raise KeyError(f"unknown chunk id {cid!r}; add it to CHUNK_IDS")
            expected = CHUNK_IDS[cid]
            if arr.dtype != expected:
                raise TypeError(f"chunk {cid!r} is {arr.dtype}, expected {expected}")
            payload = arr.tobytes()
            f.write(struct.pack("<4sIII", cid, len(arr), expected.itemsize,
                                len(payload)))
            f.write(payload)


def read_frame(path):
    """Inverse of `write_frame`, for the tests and for anyone debugging an
    export without launching Unreal. Returns `(header_dict, {id: array})`.

    Unknown chunk ids are skipped by `byte_length`, exactly as the C++ reader
    must -- this function is the executable statement of that rule.
    """
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) < HEADER_BYTES or raw[:4] != MAGIC:
        raise ValueError(f"not a .vrgf file: {path}")
    (_magic, version, frame_index, chunk_count,
     vx, vy, vz, yaw, time_s, _reserved) = struct.unpack_from("<4sIiI5fI", raw, 0)
    if version != FORMAT_VERSION:
        raise ValueError(f"{path}: format version {version}, "
                         f"this reader speaks {FORMAT_VERSION}")
    header = {
        "frame_index": frame_index,
        "vehicle_xyz": (vx, vy, vz),
        "vehicle_yaw": yaw,
        "time_s": time_s,
    }
    chunks = {}
    off = HEADER_BYTES
    for _ in range(chunk_count):
        cid, count, stride, byte_length = struct.unpack_from("<4sIII", raw, off)
        off += CHUNK_HEADER_BYTES
        if cid in CHUNK_IDS:
            dt = CHUNK_IDS[cid]
            if stride != dt.itemsize:
                raise ValueError(f"{path}: chunk {cid!r} stride {stride}, "
                                 f"expected {dt.itemsize}")
            chunks[cid] = np.frombuffer(raw, dtype=dt, count=count, offset=off)
        off += byte_length
    return header, chunks
