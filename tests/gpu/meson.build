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
    extra_shader_args = get_option('extra_shaders') ? ['--extra-shaders'] : []
    entry_points = run_command(hlsl_python, hlsl_checker, '--list', extra_shader_args, check: true).stdout().strip().split('\n')
    shader_outputs = []
    foreach specification : entry_points
        shader_outputs += specification.split(':')[0] + '.dxbc'
    endforeach
    shaders = custom_target('gpu-test-shaders',
        input: '../../shader/gpu/quad.hlsl',
        output: shader_outputs,
        depend_files: hlsl_checker + files('../../shader/gpu/xbrz.hlsl'),
        command: [hlsl_python, hlsl_checker, extra_shader_args, '--source', '@INPUT@',
                  '--output-dir', meson.current_build_dir()],
    )
    run_target('hlsl-lint', command: [hlsl_python, hlsl_checker, extra_shader_args, '--verify'] + shaders,
        depends: shaders)
    shader_tests = executable('gpu-shader-tests',
        'shader-pixels.cpp', '../../src/display/gpu-device.cpp', '../../src/display/gpu-shader.cpp',
        include_directories: include_directories('../../src'),
        dependencies: sdl,
    )
    test('gpu-shader-pixels', shader_tests, args: [meson.current_build_dir()] + extra_shader_args,
        depends: shaders, timeout: 30)
    frame_tests = executable('gpu-frame-tests',
        'frame-batches.cpp', '../../src/display/gpu-device.cpp', '../../src/display/gpu-shader.cpp',
        '../../src/display/gpu-frame.cpp',
        include_directories: include_directories('../../src'),
        dependencies: sdl,
    )
    test('gpu-frame-batches', frame_tests, args: [meson.current_build_dir()],
        depends: shaders, timeout: 30)
    bitmap_tests = executable('gpu-bitmap-tests',
        'bitmap-pixels.cpp', '../../src/display/gpu-device.cpp', '../../src/display/gpu-shader.cpp',
        '../../src/display/gpu-bitmap.cpp',
        include_directories: include_directories('../../src'),
        dependencies: sdl,
    )
    test('gpu-bitmap-pixels', bitmap_tests, args: [meson.current_build_dir()],
        depends: shaders, timeout: 30)
endif

geometry_tests = executable('gpu-geometry-tests',
    'geometry.cpp', '../../src/display/render/vertex.cpp', '../../src/display/render/tilequad.cpp',
    include_directories: include_directories('../../src', '../../src/etc', '../../src/util'),
    dependencies: sdl,
)
test('gpu-cpu-geometry', geometry_tests)

subdir('shader-state')

if host_machine.system() == 'windows'
    primitive_benchmark = executable('gpu-primitives-bench',
        '../../tools/bench/gpu-primitives.cpp', '../../src/display/gpu-device.cpp',
        '../../src/display/gpu-shader.cpp', '../../src/display/gpu-bitmap.cpp',
        '../../src/display/gpu-frame.cpp',
        include_directories: include_directories('../../src'),
        dependencies: sdl, build_by_default: false,
    )
endif
