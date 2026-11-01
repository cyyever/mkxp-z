project('mkxp-sdl3-migration', 'cpp', meson_version: '>=1.10.0',
        default_options: {'cpp_std': 'c++26,c++latest', 'warning_level': '3'})

sdl = dependency('SDL3', version: '>=3.4.14', method: 'cmake', modules: ['SDL3::SDL3'])
image = dependency('SDL3_image', version: '>=3.2.4', method: 'cmake', modules: ['SDL3_image::SDL3_image'])
ttf = dependency('SDL3_ttf', version: '>=3.2.2', method: 'cmake', modules: ['SDL3_ttf::SDL3_ttf'])

migration_tests = executable('sdl3-migration-tests', 'migration.cpp',
    include_directories: include_directories('../../src'),
    dependencies: [sdl, image, ttf])
test('SDL3 image, IO, font and window migration', migration_tests,
    args: files('../../assets/liberation.ttf'),
    env: {'SDL_VIDEO_DRIVER': 'dummy', 'SDL_AUDIO_DRIVER': 'dummy'}, timeout: 30)
