project('mkxp-audio-tests', 'cpp', default_options: {'cpp_std': 'c++26,c++latest'})
sdl = dependency('SDL3', version: '>=3.4', method: 'cmake', modules: ['SDL3::SDL3'])
mixer = dependency('SDL3_mixer', version: '>=3.2', method: 'cmake', modules: ['SDL3_mixer::SDL3_mixer'])
stream_tests = executable('audio-stream-tests',
    'audio-stream.cpp', '../../src/audio/alstream.cpp',
    include_directories: include_directories('support', '../../src/audio', '../../src/util'),
    dependencies: [sdl, mixer],
)
test('audio-stream', stream_tests, args: meson.current_source_dir() / 'fixtures', timeout: 30)

mixer_decode_tests = executable('mixer-decode-tests', 'mixer-decode.cpp', dependencies: [sdl, mixer])
test('mixer-decode', mixer_decode_tests, args: meson.current_source_dir() / 'fixtures', timeout: 15)

emitter_tests = executable('sound-emitter-tests',
    'sound-emitter.cpp', '../../src/audio/soundemitter.cpp',
    include_directories: include_directories('support', '../../src/audio', '../../src/util'),
    dependencies: [sdl, mixer],
)
test('sound-emitter', emitter_tests, args: meson.current_source_dir() / 'fixtures', timeout: 30)

semantics_tests = executable('audio-semantics-tests',
    'audio-semantics.cpp', '../../src/audio/audio.cpp', '../../src/audio/audiostream.cpp',
    '../../src/audio/alstream.cpp', '../../src/audio/soundemitter.cpp',
    include_directories: include_directories('support', '../../src/audio', '../../src/util'),
    dependencies: [sdl, mixer],
)
test('audio-semantics', semantics_tests, args: meson.current_source_dir() / 'fixtures', timeout: 90)
