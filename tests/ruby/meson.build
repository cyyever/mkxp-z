# Exercise the exact vendored dependency and static link list used by the engine.
# Only its three external libraries come from the test host.
test_cflags = []
test_ldflags = []
foreach name : ['libffi', 'yaml-0.1', 'zlib']
    dep = dependency(name, required: true)
    test_cflags += '-I' + dep.get_variable(pkgconfig: 'includedir')
    test_ldflags += '-L' + dep.get_variable(pkgconfig: 'libdir')
endforeach
ruby_project = subproject('ruby-3.4', default_options: {
    'c_std': c_std,
    'cpp_std': dependency_cpp_std,
    'cflags': run_command(quotetool, test_cflags, check: true).stdout().strip(),
    'cppflags': run_command(quotetool, test_cflags, check: true).stdout().strip(),
    'ldflags': run_command(quotetool, test_ldflags, check: true).stdout().strip(),
})
ruby_runtime = ruby_project.get_variable('ruby')
embedded_ruby = executable(
    'embedded-ruby',
    'embedded-ruby.cpp',
    dependencies: ruby_runtime,
    include_directories: include_directories('../../binding', '../../src/util'),
    override_options: engine_standards,
)
test('ruby-rgss', embedded_ruby, args: [files('rgss.rb')], env: {
    'RUBYLIB': ruby_project.get_variable('ruby_build_dir') / 'dist/lib/ruby/3.4.0',
    'RUBYOPT': '',
}, timeout: 120)
