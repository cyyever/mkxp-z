# Exercise the exact vendored dependency and static link list used by the engine.
# Only its external zlib comes from the test host.
test_cflags = []
test_ldflags = []
foreach name : ['zlib']
    dep = dependency(name, required: true)
    # Passing a system include dir (e.g. the macOS SDK's usr/include) with -I demotes it from system-header status;
    # the resulting warnings make Ruby's configure probes fail (PRI_LL_PREFIX ends up undefined).
    includedir = dep.get_variable(pkgconfig: 'includedir')
    if not includedir.endswith('/usr/include')
        test_cflags += '-I' + includedir
    endif
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
    'json-settings.cpp',
    '../../binding/json-binding.cpp',
    dependencies: ruby_runtime,
    include_directories: include_directories('../../binding', '../../src', '../../src/util'),
    override_options: engine_standards,
)
test('ruby-rgss', embedded_ruby, args: [files('rgss.rb')], env: {
    'RUBYLIB': ruby_project.get_variable('ruby_build_dir') / 'dist/lib/ruby/3.4.0',
    'RUBYOPT': '',
}, timeout: 120)

# Exercise the production settings converters independently of engine/assets.
test('ruby-json-settings', embedded_ruby, args: [files('json-settings.rb')], env: {
    'RUBYLIB': ruby_project.get_variable('ruby_build_dir') / 'dist/lib/ruby/3.4.0',
    'RUBYOPT': '',
}, timeout: 120)
