project('mkxp-audio-tests', 'cpp', default_options: {'cpp_std': 'c++26,c++latest'})
sdl = dependency('SDL3', version: '>=3.4', method: 'cmake', modules: ['SDL3::SDL3'])
mixer = dependency('SDL3_mixer', version: '>=3.2', method: 'cmake', modules: ['SDL3_mixer::SDL3_mixer'])
stream_tests = executable('audio-stream-tests',
    'audio-stream.cpp', '../../src/audio/alstream.cpp',
    include_directories: include_directories('support', '../../src/audio', '../../src/util'),
    dependencies: [sdl, mixer],
)
test('audio-stream', stream_tests, env: {'SDL_AUDIO_DRIVER': 'dummy'}, timeout: 15)

mixer_decode_tests = executable('mixer-decode-tests', 'mixer-decode.cpp', dependencies: [sdl, mixer])
test('mixer-decode', mixer_decode_tests, args: meson.current_source_dir() / 'fixtures', timeout: 15)

source_tests = executable('mixer-source-tests',
    'mixer-source.cpp', '../../src/audio/mixersource.cpp',
    include_directories: include_directories('support', '../../src/audio', '../../src/util'),
    dependencies: [sdl, mixer],
)
test('mixer-source', source_tests, args: meson.current_source_dir() / 'fixtures', timeout: 30)
