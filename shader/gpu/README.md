# Strict offline HLSL validation

Every entry point in `quad.hlsl` is compiled by the Windows SDK FXC compiler with
`/Ges /WX`: strict language rules and warnings as errors. `vertexMain` targets
`vs_5_1`, as does `tilemapVertexMain`; the 22 core fragment entry points target
`ps_5_1`. Optional xBRZ adds one fragment entry point. Outputs are DXBC suitable
for SDL_GPU's Direct3D12 backend. This checks syntax, types, semantics and shader
model compatibility; rendered pixel parity still requires GPU integration tests.

From a Windows SDK installation:

```powershell
python tools/shaders/lint-hlsl.py --output-dir build/hlsl-lint
# Also validate the separately licensed optional GPLv3+ xBRZ effect:
python tools/shaders/lint-hlsl.py --extra-shaders --output-dir build/hlsl-extra-lint
```

Pass `--compiler PATH` to select an SDK compiler. The checker discovers FXC in
PATH or the Windows SDK directory and refuses missing/stale entry-point coverage.
No compiler DLL is added to the engine runtime.

`shader/gpu/meson.build` exports `gpu_shader_binaries` for engine embedding and
provides an `hlsl-lint` run target depending on every strict custom compilation.
The engine shader build includes this subdirectory:

```sh
meson compile -C build hlsl-lint
```

The standalone `tests/gpu` project also provides `hlsl-lint`; it runs the same
checker and profiles. DXC/SM6 and SPIR-V/MSL validation are not covered by this
FXC target. Shadercross or additional offline compilers will be required for
those backends.

## Extended effect interface

The original vertex/fragment uniform blocks remain 144/80 bytes. Extended
fragment effects use a second 144-byte `EffectParameters` block at slot 1:

| float4 | Meaning |
|---|---|
| patternScaleOffset | affine normalized source UV to pattern UV, scale xy / offset zw |
| spriteFlags | pattern enabled, pattern blend type (0/1/2), invert, bush Y axis |
| bush | slope, intercept, bush opacity, bush-under boolean |
| filterSize | source width/height, inverse texture width/height |
| filterParameters | Mitchell-Netravali B/C, xBRZ target scale xy |
| blurDirection | normalized one-texel blur delta xy, sprite pattern opacity z |
| shadowBounds | H: x1/x2/y/wall; V: y1/y2/x/wall |
| shadowDimensions | width, height, center x, center y |
| shadowSlopes | slope1, slope2, soft-edge boolean, reserved |

Animated tilemaps optionally use `tilemapVertexMain` and a second 48-byte vertex
block: animation index in int4.x, followed by two int4 values holding seven
positive autotile frame counts. Atlas animation shifts match the original
96-pixel frame columns / 32-pixel rows. Ordinary atlas coordinates do not index
the autotile table; the original GLSL's out-of-range access at the boundary is
not reproduced. Animation indices are nonnegative.

All core GLSL fragment effects now have HLSL equivalents. Bicubic/Lanczos retain
their upstream MIT provenance, and KGL shadows retain the original documented
bounds quirks. `xbrz.hlsl` retains the full original GPLv3+/upstream notices and is
compiled only with `--extra-shaders` (engine `have_extra_shaders` gate). The
pipeline accepts omitted program bytecode and rejects requesting such an effect,
so xBRZ need not be embedded in builds that disable it. This does not establish
full rendered parity: xBRZ edge behavior still needs comparison against the GL
baseline, as do full sprite/tilemap scenes and high-resolution resize filters.
