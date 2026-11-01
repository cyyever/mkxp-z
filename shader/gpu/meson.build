# Included by the engine shader build. Keep the entry-point list in the checker
# so the standalone tests and production binaries use the same profiles.
gpu_shader_binaries = {}
gpu_shader_formats = []
gpu_shader_targets = []
gpu_shader_entrypoint_maps = []
hlsl_python = find_program('python3', 'python')
hlsl_checker = files('../../tools/shaders/lint-hlsl.py')
extra_shader_args = get_variable('have_extra_shaders', false) ? ['--extra-shaders'] : []
entry_points = run_command(hlsl_python, hlsl_checker, '--list', extra_shader_args, check: true).stdout().strip().split('\n')
if host_machine.system() == 'windows'
    gpu_shader_formats = ['dxbc']
    fxc_path = run_command(hlsl_python, hlsl_checker, '--find-compiler', check: true).stdout().strip()
    fxc = find_program(fxc_path)
    foreach specification : entry_points
        entry = specification.split(':')[0]
        profile = specification.split(':')[1]
        compiled_shader = custom_target(
            'gpu-' + entry,
            input: entry == 'xbrzMain' ? 'xbrz.hlsl' : 'quad.hlsl',
            depend_files: files('quad.hlsl'),
            output: entry + '.dxbc',
            command: [fxc, '/nologo', '/Ges', '/WX', '/T', profile,
                      '/E', entry, '/Fo', '@OUTPUT@', '@INPUT@'],
        )
        gpu_shader_binaries += {entry + '.dxbc': compiled_shader}
        gpu_shader_targets += compiled_shader
    endforeach
    run_target('hlsl-lint', command: [hlsl_python, hlsl_checker, extra_shader_args, '--verify'] + gpu_shader_targets,
        depends: gpu_shader_targets)
endif

# DXC is an offline build tool only. Official portable DXC includes SPIR-V;
# some Windows SDK builds expose the switch but omit its implementation.
spirv_compiler = find_program('dxc', required: host_machine.system() != 'windows')
spirv_tool = files('../../tools/shaders/compile-spirv.py')
have_spirv_compiler = spirv_compiler.found()
if have_spirv_compiler and host_machine.system() == 'windows'
    # VsDevCmd may put the SDK's DXC (without SPIR-V CodeGen) on PATH.
    # Probe an actual shader instead of trusting a misleading -help listing.
    probe = run_command(hlsl_python, spirv_tool, '--compiler', spirv_compiler,
        '--entry', 'vertexMain', '--profile', 'vs_5_1',
        '--source', files('quad.hlsl'),
        '--output', meson.current_build_dir() / 'spirv-probe.spirv', check: false)
    have_spirv_compiler = probe.returncode() == 0
    if not have_spirv_compiler
        warning('DXC SPIR-V probe failed; keeping DXBC only. Use the official portable DXC for Vulkan artifacts. ' + probe.stderr())
    endif
endif
if have_spirv_compiler
    gpu_shader_formats += ['spirv']
    foreach specification : entry_points
        entry = specification.split(':')[0]
        profile = specification.split(':')[1]
        compiled_shader = custom_target(
            'gpu-spirv-' + entry,
            input: entry == 'xbrzMain' ? 'xbrz.hlsl' : 'quad.hlsl',
            depend_files: files('quad.hlsl') + spirv_tool,
            output: entry + '.spirv',
            command: [hlsl_python, spirv_tool, '--compiler', spirv_compiler,
                      '--entry', entry, '--profile', profile,
                      '--source', '@INPUT@', '--output', '@OUTPUT@'],
        )
        gpu_shader_binaries += {entry + '.spirv': compiled_shader}
        gpu_shader_targets += compiled_shader
    endforeach
endif

# SDL_shadercross and the reflection companion are offline tools only.
msl_compiler = find_program('shadercross', required: host_machine.system() == 'darwin')
msl_reflector = find_program('mkxp-msl-entrypoint', required: host_machine.system() == 'darwin')
if have_spirv_compiler and msl_compiler.found() and msl_reflector.found()
    gpu_shader_formats += ['msl']
    msl_tool = files('../../tools/shaders/compile-msl.py')
    foreach specification : entry_points
        entry = specification.split(':')[0]
        stage = specification.split(':')[1].startswith('vs') ? 'vertex' : 'fragment'
        compiled_shader = custom_target(
            'gpu-msl-' + entry,
            input: gpu_shader_binaries[entry + '.spirv'],
            output: [entry + '.msl', entry + '.msl.json'],
            command: [hlsl_python, msl_tool, '--compiler', msl_compiler,
                      '--reflector', msl_reflector, '--entry', entry, '--stage', stage,
                      '--source', '@INPUT@', '--output', '@OUTPUT0@', '--metadata', '@OUTPUT1@'],
        )
        gpu_shader_binaries += {entry + '.msl': compiled_shader[0]}
        gpu_shader_entrypoint_maps += compiled_shader[1]
        gpu_shader_targets += compiled_shader
    endforeach
endif
