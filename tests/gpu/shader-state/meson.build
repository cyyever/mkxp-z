# Compile exact adapter source/header copies with only resource identity and the
# RenderContext hooks replaced. This isolates setter semantics from GPU lifetime.
state_source = configure_file(input: '../../../src/display/render/shader.cpp',
    output: 'shader-state-production.cpp', copy: true)
configure_file(input: '../../../src/display/render/shader.h', output: 'shader.h', copy: true)
configure_file(input: '../../../src/display/gpu-shader-state.h', output: 'gpu-shader-state.h', copy: true)
configure_file(input: '../support/shader-resource.h', output: 'gpu-resources.h', copy: true)
state_tests = executable('gpu-shader-state-tests', '../shader-state.cpp', state_source,
    include_directories: include_directories('../../../src/display', '../../../src/etc', '../../../src/util'),
    dependencies: sdl,
)
test('gpu-shader-state', state_tests)
