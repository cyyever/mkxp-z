project('mkxp-audio-tests', 'cpp', default_options: {'cpp_std': 'c++26,c++latest'})
cpp = meson.get_compiler('cpp')
# libc++ before LLVM 20 (e.g. FreeBSD 15's base clang 19) ships std::jthread and
# std::stop_token only behind -fexperimental-library. Probe the feature rather than
# the platform, and fall back to that flag only when the plain build cannot link it.
jthread_probe = '''#include <stop_token>
#include <thread>
int main() { std::jthread t([](std::stop_token) {}); }
'''
threads = dependency('threads')
jthread_args = []
if not cpp.links(jthread_probe, dependencies: threads, name: 'std::jthread')
    if cpp.get_argument_syntax() == 'gcc' and cpp.links(jthread_probe,
            args: '-fexperimental-library', dependencies: threads,
            name: 'std::jthread with -fexperimental-library')
        jthread_args = ['-fexperimental-library']
    else
        error('The C++ standard library does not provide std::jthread.')
    endif
endif
add_project_arguments(jthread_args, language: 'cpp')
add_project_link_arguments(jthread_args, language: 'cpp')
sdl = dependency('SDL3', version: '>=3.4', method: 'cmake', modules: ['SDL3::SDL3'])
mixer = dependency('SDL3_mixer', version: '>=3.2', method: 'cmake', modules: ['SDL3_mixer::SDL3_mixer'])
stream_tests = executable('audio-stream-tests',
    'audio-stream.cpp', '../../src/audio/alstream.cpp',
    include_directories: include_directories('support', '../../src/audio', '../../src/util'),
    dependencies: [sdl, mixer, threads],
)
test('audio-stream', stream_tests, args: meson.current_source_dir() / 'fixtures', timeout: 30)

mixer_decode_tests = executable('mixer-decode-tests', 'mixer-decode.cpp', dependencies: [sdl, mixer, threads])
test('mixer-decode', mixer_decode_tests, args: meson.current_source_dir() / 'fixtures', timeout: 15)

emitter_tests = executable('sound-emitter-tests',
    'sound-emitter.cpp', '../../src/audio/soundemitter.cpp',
    include_directories: include_directories('support', '../../src/audio', '../../src/util'),
    dependencies: [sdl, mixer, threads],
)
test('sound-emitter', emitter_tests, args: meson.current_source_dir() / 'fixtures', timeout: 30)

semantics_tests = executable('audio-semantics-tests',
    'audio-semantics.cpp', '../../src/audio/audio.cpp', '../../src/audio/audiostream.cpp',
    '../../src/audio/alstream.cpp', '../../src/audio/soundemitter.cpp',
    include_directories: include_directories('support', '../../src/audio', '../../src/util'),
    dependencies: [sdl, mixer, threads],
)
test('audio-semantics', semantics_tests, args: meson.current_source_dir() / 'fixtures', timeout: 90)
