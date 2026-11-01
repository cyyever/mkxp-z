project('mkxp-file-io-tests', 'cpp', default_options: {'cpp_std': 'c++26,c++latest', 'b_vscrt': 'md'})
sdl = dependency('SDL3', version: '>=3.4', method: 'cmake', modules: ['SDL3::SDL3'])
ruby = dependency('ruby-3.4', 'ruby', version: '>=3.4')
io_tests = executable('file-io-tests',
    'file-io.cpp', '../../binding/filesystem-binding.cpp', '../../binding/binding-util.cpp',
    include_directories: include_directories('support', '../..', '../../binding', '../../src/util'),
    dependencies: [sdl, ruby],
)
test('ruby-file-io', io_tests, args: [files('file-io.rb')], timeout: 30)
