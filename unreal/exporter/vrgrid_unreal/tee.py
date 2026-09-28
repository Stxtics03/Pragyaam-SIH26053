"""Fan one run out to several views.

`dashboard/__main__.py` holds a single `view` and calls `log_frame` / `finish`
on it. A `Tee` is a view that forwards both to a list of real ones, so the
Rerun recording and the Unreal export come out of ONE pass over the sequence,
off one `MapEngine`, on one frame index.

That is the whole guarantee the side-by-side rests on. Two separate runs would
be two separate Patchwork++ lifetimes and two separate engines, and the two
windows would be showing neighbouring truths rather than the same one.

Cost: each view reads the map independently, so `occupied_cells()` runs twice
on a map frame. It allocates and it is a readout -- nothing on the frame path
calls it, and a bake is offline -- so this is paid deliberately rather than
worked around with a cache that could go stale between the two consumers.
"""


class Tee:
    def __init__(self, *views):
        self.views = [v for v in views if v is not None]

    def log_frame(self, frame, counters=None, timing_ms=None):
        for v in self.views:
            v.log_frame(frame, counters=counters, timing_ms=timing_ms)

    def finish(self):
        for v in self.views:
            v.finish()
