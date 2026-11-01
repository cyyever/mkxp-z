project('mkxp-gpu-tests', 'cpp', default_options: {'cpp_std': 'c++26,c++latest'})
sdl = dependency('SDL3', version: '>=3.4', method: 'cmake', modules: ['SDL3::SDL3'])
device_tests = executable('gpu-device-tests',
    'device.cpp', '../../src/display/gpu-device.cpp',
    include_directories: include_directories('../../src'),
    dependencies: sdl,
)
# Requires a desktop GPU, not SDL's dummy video driver.
test('gpu-device', device_tests, timeout: 30)
