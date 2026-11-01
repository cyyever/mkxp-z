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

## Embedded format descriptors

The engine now carries `ShaderCode { bytes, format, entrypoint }` instead of
assuming every shader is DXBC. The registry is generated from the same entry
manifest used by offline compilation; blob symbols include the format, and the
GPU device is offered only formats actually embedded. Pipeline creation selects
an available variant accepted by `SDL_GetGPUShaderFormats`. Existing DXBC-span
callers remain source compatible. SPIR-V input is copied to Uint32-aligned
storage during shader creation.

The build now also produces real **Vulkan SPIR-V** for every entry point with
an offline SPIR-V-enabled DXC. Put the official portable DXC `bin/x64` (Windows)
or `bin` (Linux) on PATH, or select it reproducibly in a Meson native file:

```ini
[binaries]
dxc = '/absolute/path/to/dxc'
```

Windows retains FXC/DXBC and adds SPIR-V when DXC passes an actual compilation
probe. SDK DXC without SPIR-V emits a warning and keeps the DXBC build usable. Linux/FreeBSD require
DXC. No compiler or translation library is linked into the engine. Some Windows
SDK DXC builds advertise `-spirv` but were built without its code generator;
use the official portable DirectXShaderCompiler release in that case.

The producer uses Vulkan 1.0 / shader model 6.0, strict diagnostics and warnings
as errors. DXC's SPIRV-Tools validation remains enabled. HLSL sampled textures
and samplers explicitly share `vk::combinedImageSampler`; an offline structural
check rejects descriptor sets or resource kinds outside SDL's contract. Vertex
uniforms use set 1, fragment sampled images set 2 and fragment uniforms set 3.
All existing uniform blocks retain their float4/matrix layout and slot numbers.

Metal remains unavailable: there is no MSL/metallib producer or Apple compiler
validation. macOS configuration reports that gap explicitly. Metal registry
generation still requires an explicit exported-entrypoint map. Compiling SPIR-V
on Windows does not establish Linux/FreeBSD runtime support or pixel parity.

Official contracts:
- [SDL supported device formats](https://wiki.libsdl.org/SDL3/SDL_GetGPUShaderFormats)
- [SDL shader resource bindings](https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader)
- [SDL offline shader translation](https://github.com/libsdl-org/SDL_shadercross)
- [DXC SPIR-V mappings](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst)

Validation: official portable DXC v1.9.2609 compiled all 25 SPIR-V entry points
(including xBRZ), with DXC validation and descriptor/uniform-ABI checks enabled.
Production Meson shader/embed rules and native MSVC compiled and linked the
combined DXBC/SPIR-V registry with extra shaders enabled and disabled. No runtime
tests were run; no Linux, FreeBSD or Metal compiler/runtime validation is claimed.
