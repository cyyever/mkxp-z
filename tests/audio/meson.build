project('mkxp-audio-tests', 'cpp', default_options: {'cpp_std': 'c++26,c++latest'})
sdl = dependency('sdl3', version: '>=3.4')
stream_tests = executable('audio-stream-tests',
    'audio-stream.cpp', '../../src/audio/alstream.cpp',
    include_directories: include_directories('support', '../../src/audio', '../../src/util'),
    dependencies: sdl,
)
test('audio-stream', stream_tests, env: {'SDL_AUDIO_DRIVER': 'dummy'}, timeout: 15)
