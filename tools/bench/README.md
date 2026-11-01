The optional Windows `gpu-primitives-bench` target in `tests/gpu` measures the
production `BitmapRenderer` and `FrameEncoder` with native SDL_GPU/D3D12.
Build the focused GPU project in release mode, compile its shader target, then
explicitly build `gpu-primitives-bench`. Pass the compiled shader directory as
the first argument, followed by sample count and iterations per sample.

`compare-primitives.py` accepts this executable and a separately preserved native
OpenGL baseline executable. The OpenGL baseline is an external benchmark artifact;
it is not part of the migrated engine. Both executables emit the same JSONL schema. SDL_GPU additionally measures
`framefill64`: sixty-four FlatColor draws with per-rectangle scissors recorded
in one FrameEncoder submission, against the same GL scissored-clear reference. Example:

```
python tools/bench/compare-primitives.py --gl-baseline /path/gl-primitives.exe --gpu /path/gpu-primitives-bench.exe --shaders /path/compiled-shaders --output /path/comparison.json --notes "Native MSVC release /O2 /MD; host workload conditions"
```

Workloads cover RGBA8 clears, 64 non-overlapping rectangular fills, nearest
full-texture blits, and a committed batch of 64 opaque colored quads at 640x480,
1920x1080 and 3840x2160. Initialization, pipeline compilation and pixel readback
are outside timing. Exact pixels are checked before each workload; cross-backend
checksums must agree. GPU submission and completion waits are included, once per
sample. Defaults are five warmup samples and thirty measured samples of ten
iterations, repeated in three alternating backend rounds. Raw samples, device
identity, binary hashes, median and p95 are retained.

These measurements include actual backend costs: the GL batch reuses committed
indexed geometry, while `FrameEncoder` records and uploads the equivalent quad
batch per frame. They do not measure Ruby bindings, game assets, presentation,
frame pacing, or whole-game performance. A ratio above one means SDL_GPU took
longer for that workload; no backend is assumed faster.

The recorded RTX 2080 result shows why immediate bitmap fills must not be used
as the frame submission path: batching 64 scissored FlatColor draws reduces
fenced GPU latency from 754/786/841 microseconds to 53/78/135 microseconds at
480p/1080p/4K in this run. The matched GL scissored clears take 54/50/55
microseconds. This is a primitive benchmark on one shared development machine,
not a game speedup or an isolated hardware guarantee. The committed compact
summary retains median/p95; its SHA256 identifies the separately retained raw
sample report.
