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

An optional offline **MSL source producer** is also available. It uses pinned
SDL_shadercross + SPIRV-Cross, and never links either library into the engine:

```sh
cmake -S tools/shaders/offline-msl -B build/offline-msl -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DMKXP_SDL_SOURCE=/path/to/SDL3-source
cmake --build build/offline-msl --target shadercross mkxp-msl-entrypoint
```

Alternatively omit `MKXP_SDL_SOURCE` and provide an installed static SDL3 CMake
package. Only the offline CLI needs SDL. Source builds disable its audio/video/
GPU backends. Place both resulting executables (`shadercross` and
`mkxp-msl-entrypoint`) on PATH, or provide their absolute paths in Meson's
`[binaries]` native file. DXC is also required for the preceding SPIR-V stage.

Pinned inputs (CMake verifies archive SHA256):
- SDL_shadercross `1ff05bec573988a98ef9e0260b4da44f512b8367`,
  `feb818c93c804cff0f9a1f3f0d0462bfff09f33bc8406da7f716e9cff018187c`.
- SPIRV-Cross `57e4efcc84112abcd426dc5ec248b5339bee04f2`,
  `5be4b0ee78036ed6358f5320c6070df5c5bd1cf05c6f55dc221795f1d4e7bd38`.

Each entry point produces MSL 2.0 source plus a JSON metadata fragment. The
companion queries SPIRV-Cross's actual cleansed Metal entrypoint name; registry
generation merges those fragments and rejects duplicate/missing mappings. The
producer also checks that the reflected name occurs in the generated source.
macOS requires both offline tools rather than treating SPIR-V as a Metal fallback.

This is **MSL generation, not Apple compiler validation**. No `metal`/`metallib`
toolchain was available on the Windows build host; no metallib is produced and
native macOS compilation or Metal runtime/pixel parity is unverified. Likewise,
Windows SPIR-V generation does not establish Linux/FreeBSD native runtime support.

Official contracts:
- [SDL supported device formats](https://wiki.libsdl.org/SDL3/SDL_GetGPUShaderFormats)
- [SDL shader resource bindings](https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader)
- [SDL offline shader translation](https://github.com/libsdl-org/SDL_shadercross)
- [DXC SPIR-V mappings](https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst)

Validation: official portable DXC v1.9.2609 compiled all 25 SPIR-V entry points
(including xBRZ), with DXC validation and descriptor/uniform-ABI checks enabled.
Production Meson shader/embed rules and native MSVC compiled and linked the
combined DXBC/SPIR-V registry with extra shaders enabled and disabled. No runtime
tests were run; no Linux, FreeBSD or Apple Metal compiler/runtime validation is claimed.
The pinned offline tools also compiled and linked on Windows; all 25 MSL sources
and reflected maps were generated, then the production Meson rules embedded
DXBC/SPIR-V/MSL together into a native MSVC compiled/linked registry.
