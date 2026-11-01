# Included by the engine shader build. Keep the entry-point list in the checker
# so the standalone tests and production binaries use the same profiles.
gpu_shader_binaries = {}
gpu_shader_formats = []
gpu_shader_targets = []
if host_machine.system() == 'windows'
    gpu_shader_formats = ['dxbc']
    hlsl_python = find_program('python3', 'python')
    hlsl_checker = files('../../tools/shaders/lint-hlsl.py')
    fxc_path = run_command(hlsl_python, hlsl_checker, '--find-compiler', check: true).stdout().strip()
    fxc = find_program(fxc_path)
    extra_shader_args = get_variable('have_extra_shaders', false) ? ['--extra-shaders'] : []
    entry_points = run_command(hlsl_python, hlsl_checker, '--list', extra_shader_args, check: true).stdout().strip().split('\n')
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
