project('mkxp-gpu-tests', 'cpp', default_options: {'cpp_std': 'c++26,c++latest'})
sdl = dependency('SDL3', version: '>=3.4', method: 'cmake', modules: ['SDL3::SDL3'])
device_tests = executable('gpu-device-tests',
    'device.cpp', '../../src/display/gpu-device.cpp',
    include_directories: include_directories('../../src'),
    dependencies: sdl,
)
# Requires a desktop GPU, not SDL's dummy video driver.
test('gpu-device', device_tests, timeout: 30)

if host_machine.system() == 'windows'
    hlsl_python = find_program('python3', 'python')
    hlsl_checker = files('../../tools/shaders/lint-hlsl.py')
    entry_points = run_command(hlsl_python, hlsl_checker, '--list', check: true).stdout().strip().split('\n')
    shader_outputs = []
    foreach specification : entry_points
        shader_outputs += specification.split(':')[0] + '.dxbc'
    endforeach
    shaders = custom_target('gpu-test-shaders',
        input: '../../shader/gpu/quad.hlsl',
        output: shader_outputs,
        depend_files: hlsl_checker,
        command: [hlsl_python, hlsl_checker, '--source', '@INPUT@',
                  '--output-dir', meson.current_build_dir()],
    )
    run_target('hlsl-lint', command: [hlsl_python, hlsl_checker, '--verify'] + shaders,
        depends: shaders)
    shader_tests = executable('gpu-shader-tests',
        'shader-pixels.cpp', '../../src/display/gpu-device.cpp', '../../src/display/gpu-shader.cpp',
        include_directories: include_directories('../../src'),
        dependencies: sdl,
    )
    test('gpu-shader-pixels', shader_tests, args: [meson.current_build_dir()],
        depends: shaders, timeout: 30)
endif
