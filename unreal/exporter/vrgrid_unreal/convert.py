"""VRgrid world -> Unreal world, in one place.

This is the file that decides whether the map is mirrored, and a mirrored map
looks entirely plausible. `docs/frames.md` opens by saying frame confusion is
the most common silent bug in this project; this is that bug's Unreal-shaped
twin, so it lives alone, is tested, and is asserted again on the C++ side at
load time against vectors written into the scene manifest.

    VRgrid world   x FORWARD, y LEFT,  z UP, RIGHT-handed, METRES
    Unreal world   x FORWARD, y RIGHT, z UP, LEFT-handed,  CENTIMETRES

So the map is `(x, y, z)_m -> (100x, -100y, 100z)_cm`. The y negation is the
whole of the handedness change; forget it and every scene is its own mirror
image, which reads as correct until someone compares it with the Rerun window
and finds the kerb on the wrong side of the car.

Yaw follows the same rule. A right-handed yaw about +z (counter-clockwise seen
from above) becomes a left-handed yaw about +z, so the sign flips:

    yaw_ue_deg = -degrees(yaw_vrgrid_rad)
"""

import numpy as np

M_TO_CM = 100.0

# Handed into the manifest and re-checked by the C++ reader at load. Chosen so
# that a sign error on ANY axis changes at least one of them: no zeros, no
# repeated magnitudes, and one negative per axis.
CONVERSION_TEST_VECTORS_M = np.array(
    [
        [1.0, 0.0, 0.0],      # pure forward  -> (+100, 0, 0)
        [0.0, 1.0, 0.0],      # pure LEFT     -> (0, -100, 0)   <- the one that catches mirroring
        [0.0, 0.0, 1.0],      # pure up       -> (0, 0, +100)
        [3.5, -2.25, 0.75],   # mixed, all three signs exercised
    ],
    dtype=np.float64,
)


def xyz_to_unreal(xyz):
    """(N, 3) metres in VRgrid world -> (N, 3) centimetres in Unreal world."""
    a = np.asarray(xyz, dtype=np.float64).reshape(-1, 3)
    out = np.empty_like(a)
    out[:, 0] = a[:, 0] * M_TO_CM
    out[:, 1] = a[:, 1] * -M_TO_CM
    out[:, 2] = a[:, 2] * M_TO_CM
    return out


def length_to_unreal(m):
    """A scalar length (cell size, ring radius) in metres -> centimetres."""
    return np.asarray(m, dtype=np.float64) * M_TO_CM


def yaw_to_unreal_deg(yaw_rad):
    """Right-handed yaw about +z (radians) -> Unreal yaw (degrees)."""
    return float(-np.degrees(yaw_rad))


def conversion_test_vectors():
    """`[{"m": [...], "cm": [...]}, ...]` for the scene manifest.

    The C++ reader runs its own conversion over `m` and compares against `cm`
    at load. A mismatch is a startup error, not a thing anyone discovers by
    looking at the picture on stage.
    """
    out = xyz_to_unreal(CONVERSION_TEST_VECTORS_M)
    return [
        # `+ 0.0` normalises -0.0, which is equal to 0.0 everywhere but
        # reads as a sign error to anyone auditing the manifest by eye.
        {"m": [float(v) + 0.0 for v in src], "cm": [float(v) + 0.0 for v in dst]}
        for src, dst in zip(CONVERSION_TEST_VECTORS_M, out)
    ]
