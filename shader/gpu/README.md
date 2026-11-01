# Strict offline HLSL validation

Every entry point in `quad.hlsl` is compiled by the Windows SDK FXC compiler with
`/Ges /WX`: strict language rules and warnings as errors. `vertexMain` targets
`vs_5_1`; the ten fragment entry points target `ps_5_1`. Outputs are DXBC suitable
for SDL_GPU's Direct3D12 backend. This checks syntax, types, semantics and shader
model compatibility; rendered pixel parity still requires GPU integration tests.

From a Windows SDK installation:

```powershell
python tools/shaders/lint-hlsl.py --output-dir build/hlsl-lint
```

Pass `--compiler PATH` to select an SDK compiler. The checker discovers FXC in
PATH or the Windows SDK directory and refuses missing/stale entry-point coverage.
No compiler DLL is added to the engine runtime.

`shader/gpu/meson.build` exports `gpu_shader_binaries` for future embedding and
provides an `hlsl-lint` run target depending on every strict custom compilation.
When that subdirectory is included by the engine shader build:

```sh
meson compile -C build hlsl-lint
```

The standalone `tests/gpu` project also provides `hlsl-lint`; it runs the same
checker and profiles. DXC/SM6 and SPIR-V/MSL validation are not covered by this
FXC target. Shadercross or additional offline compilers will be required for
those backends.
