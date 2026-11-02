project('mkxp-sdl3-migration', 'cpp', meson_version: '>=1.10.0',
        default_options: {'cpp_std': 'c++26,c++latest', 'warning_level': '3'})

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

sdl = dependency('SDL3', version: '>=3.4.14', method: 'cmake', modules: ['SDL3::SDL3'])

borrowed_io_tests = executable('sdl3-borrowed-io-tests', 'borrowed-io.cpp',
    include_directories: include_directories('../../src'),
    dependencies: sdl)
test('SDL3 borrowed IO ownership and status', borrowed_io_tests)

# Compile the actual helper with only the dialog entrypoint replaced; no UI is opened.
folder_dialog_tests = executable('sdl3-folder-dialog-tests',
    'folder-dialog.cpp', '../../src/system/folder-dialog.cpp',
    include_directories: include_directories('../../src'),
    dependencies: [sdl, threads],
    cpp_args: '-DSDL_ShowFileDialogWithProperties=TestShowFileDialogWithProperties')
test('SDL3 folder dialog callbacks and main-thread event pumping', folder_dialog_tests,
    env: {'SDL_VIDEO_DRIVER': 'dummy'}, timeout: 30)

keybinding_tests = executable('sdl3-keybinding-tests',
    'keybindings.cpp', '../../src/input/keybindings.cpp',
    include_directories: include_directories('../../src', '../../src/util'),
    dependencies: sdl)
test('SDL3 keybinding persistence and legacy binary compatibility', keybinding_tests)
