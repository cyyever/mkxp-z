core_tests = executable(
    'portable-core-tests',
    'portable-core.cpp',
    include_directories: include_directories('../../src'),
    override_options: engine_standards + {'warning_level': '3', 'werror': true},
)
test('portable-core', core_tests)

# macOS also compiles engine headers as Objective-C++.
if host_machine.system() == 'darwin'
    objcpp_tests = executable(
        'portable-core-objcpp-tests',
        'portable-core.mm',
        include_directories: include_directories('../../src'),
        override_options: engine_standards + {'warning_level': '3', 'werror': true},
    )
    test('portable-core-objcpp', objcpp_tests)
endif
