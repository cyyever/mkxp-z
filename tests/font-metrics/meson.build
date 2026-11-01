project('mkxp-font-metrics-tests', 'cpp', default_options: {'cpp_std': 'c++20'})
sdl = dependency('SDL3', version: '>=3.4', method: 'cmake', modules: ['SDL3::SDL3'])
ttf = dependency('SDL3_ttf', version: '>=3.2', method: 'cmake', modules: ['SDL3_ttf::SDL3_ttf'])
freetype = dependency('freetype2')

# Exercise the actual private helper without moving a one-caller implementation
# into a public header or copying its logic into the regression.
fs = import('fs')
font_source = fs.read('../../src/display/font.cpp')
region = font_source.split('struct FontMetrics {')
assert(region.length() == 2, 'FontMetrics definition must remain unique')
end = region[1].split('static SDL_IOStream *openBundledFont()')
assert(end.length() == 2, 'FontMetrics helper boundary changed')
configuration = configuration_data()
configuration.set('IMPLEMENTATION', 'struct FontMetrics {' + end[0])
configure_file(input: 'font-metrics-under-test.h.in',
               output: 'font-metrics-under-test.h', configuration: configuration)
metrics_tests = executable('font-metrics-tests', 'font-metrics.cpp',
                           dependencies: [sdl, ttf, freetype])
test('font-metrics-stream-errors', metrics_tests,
     args: files('../../assets/liberation.ttf'), timeout: 30)
