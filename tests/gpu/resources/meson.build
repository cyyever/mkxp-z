resource_tests = executable('gpu-resource-tests',
    'resources.cpp', '../../../src/display/gpu-device.cpp', '../../../src/display/gpu-resources.cpp',
    include_directories: include_directories('../../../src'),
    override_options: {'warning_level': '3', 'werror': true},
    dependencies: sdl,
)
test('gpu-native-resources', resource_tests, timeout: 30)
