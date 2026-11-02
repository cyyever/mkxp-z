project('mkxp-sdl3-migration', 'cpp', meson_version: '>=1.10.0',
        default_options: {'cpp_std': 'c++26,c++latest', 'warning_level': '3'})

sdl = dependency('SDL3', version: '>=3.4.14', method: 'cmake', modules: ['SDL3::SDL3'])

borrowed_io_tests = executable('sdl3-borrowed-io-tests', 'borrowed-io.cpp',
    include_directories: include_directories('../../src'),
    dependencies: sdl)
test('SDL3 borrowed IO ownership and status', borrowed_io_tests)

# Compile the actual helper with only the dialog entrypoint replaced; no UI is opened.
folder_dialog_tests = executable('sdl3-folder-dialog-tests',
    'folder-dialog.cpp', '../../src/system/folder-dialog.cpp',
    include_directories: include_directories('../../src'),
    dependencies: [sdl, dependency('threads')],
    cpp_args: '-DSDL_ShowFileDialogWithProperties=TestShowFileDialogWithProperties')
test('SDL3 folder dialog callbacks and main-thread event pumping', folder_dialog_tests,
    env: {'SDL_VIDEO_DRIVER': 'dummy'}, timeout: 30)

keybinding_tests = executable('sdl3-keybinding-tests',
    'keybindings.cpp', '../../src/input/keybindings.cpp',
    include_directories: include_directories('../../src', '../../src/util'),
    dependencies: sdl)
test('SDL3 keybinding persistence and legacy binary compatibility', keybinding_tests)
