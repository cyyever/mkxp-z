normalization_native_sources = []
normalization_deps = []
normalization_objcpp_args = []
if host_machine.system() == 'darwin'
    normalization_native_sources += files('../../src/filesystem/filename-normalization.mm')
    normalization_deps += dependency('appleframeworks', modules: ['Foundation'])
    normalization_objcpp_args += '-fobjc-arc'
endif

core_tests = executable(
    'portable-core-tests',
    'portable-core.cpp',
    normalization_native_sources,
    dependencies: normalization_deps,
    objcpp_args: normalization_objcpp_args,
    include_directories: include_directories('../../src'),
    override_options: engine_standards + {'warning_level': '3', 'werror': true},
)
test('portable-core', core_tests)

signal_tests = executable(
    'signal-tests',
    'signals.cpp',
    include_directories: include_directories('../../src'),
    override_options: engine_standards + {'warning_level': '3', 'werror': true},
)
test('signals', signal_tests)

encoding_tests = executable(
    'text-encoding-tests',
    'text-encoding.cpp',
    include_directories: include_directories('../../src'),
    override_options: engine_standards + {'warning_level': '3', 'werror': true},
)
test('text-encoding', encoding_tests)

# macOS also compiles engine headers as Objective-C++.
if host_machine.system() == 'darwin'
    objcpp_tests = executable(
        'portable-core-objcpp-tests',
        'portable-core.mm',
        normalization_native_sources,
        dependencies: normalization_deps,
        objcpp_args: normalization_objcpp_args,
        include_directories: include_directories('../../src'),
        override_options: engine_standards + {'warning_level': '3', 'werror': true},
    )
    test('portable-core-objcpp', objcpp_tests)
endif

normalization_tests = executable(
    'filename-normalization-tests',
    'filename-normalization.cpp',
    normalization_native_sources,
    dependencies: normalization_deps,
    objcpp_args: normalization_objcpp_args,
    include_directories: include_directories('../../src'),
    override_options: engine_standards + {'warning_level': '3', 'werror': true},
)
test('filename-normalization', normalization_tests)

directory_cache_tests = executable(
    'directory-cache-tests',
    'directory-cache.cpp',
    normalization_native_sources,
    dependencies: normalization_deps,
    objcpp_args: normalization_objcpp_args,
    include_directories: include_directories('../../src'),
    override_options: engine_standards + {'warning_level': '3', 'werror': true},
)
test('directory-cache', directory_cache_tests)

ini_tests = executable(
    'ini-configuration-tests',
    'ini-configuration.cpp', '../../src/util/iniconfig.cpp',
    include_directories: include_directories('../../src'),
    override_options: engine_standards + {'warning_level': '3', 'werror': true},
)
test('ini-configuration', ini_tests)

# Exercise the production Table implementation without SDL or Ruby stubs.
table_tests = executable(
    'table-serialization-tests',
    'table-serialization.cpp', '../../src/etc/table.cpp',
    include_directories: include_directories('../../src', '../../src/util'),
    override_options: engine_standards + {'warning_level': '3', 'werror': true},
)
test('table-serialization', table_tests)
