project('ruby-4.0', 'c', 'cpp', meson_version: '>=1.10.0')

common = subproject('common')
python = common.get_variable('python')
cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')
source_dir = meson.project_source_root()
build_dir = meson.project_build_root()
ruby_build_dir = build_dir
destdir = build_dir / 'dist'

run_command(python, files('prepare-source.py'), source_dir, check: true)

ruby_deps = [
    dependency('zlib', required: true),
]

if host_machine.system() == 'windows' and cc.get_argument_syntax() != 'msvc'
    error('Windows builds require native MSVC')
endif

if cc.get_argument_syntax() == 'msvc'
    # MSVC cannot drive Ruby's autoconf build; use Ruby's own mswin build (win32/configure.bat + nmake).
    ruby_arch = {'x86_64': 'x64-mswin64_140', 'aarch64': 'arm64-mswin64_140'}[host_machine.cpu_family()]
    ruby_so_prefix = {'x86_64': 'x64', 'aarch64': 'arm64'}[host_machine.cpu_family()]
    ruby_msvc_build_dir = build_dir / 'build'
    ruby_msvc = custom_target(
        'ruby-msvc',
        output: 'ruby-msvc.stamp',
        command: [
            python,
            files('build-msvc.py'),
            source_dir,
            ruby_msvc_build_dir,
            destdir,
            get_option('cflags'),
            get_option('ldflags'),
            '@OUTPUT@',
        ],
        depends: executable('deps', sources: ['deps-stub.c'], dependencies: [ruby_deps]),
        build_by_default: true,
        console: true,
    )
    ruby = declare_dependency(
        sources: ruby_msvc,
        dependencies: [
            ruby_deps,
            cc.find_library('user32', required: true),
            cc.find_library('advapi32', required: true),
            cc.find_library('shell32', required: true),
            cc.find_library('ws2_32', required: true),
            cc.find_library('iphlpapi', required: true),
            cc.find_library('imagehlp', required: true),
            cc.find_library('shlwapi', required: true),
            cc.find_library('bcrypt', required: true),
        ],
        compile_args: [
            '-I' + source_dir / 'include',
            '-I' + ruby_msvc_build_dir / '.ext' / 'include' / ruby_arch,
        ],
        link_args: [
            ruby_msvc_build_dir / 'ext' / 'extinit.obj',
            ruby_msvc_build_dir / 'ext' / 'zlib' / 'zlib.lib',
            ruby_msvc_build_dir / 'enc' / 'encinit.obj',
            ruby_msvc_build_dir / 'enc' / 'libenc.lib',
            ruby_msvc_build_dir / 'enc' / 'libtrans.lib',
            ruby_msvc_build_dir / (ruby_so_prefix + '-vcruntime140-ruby400-static.lib'),
        ],
    )
else
    find_program('cmp', required: true)
    quotetool = common.get_variable('quotetool')
    cflags = common.get_variable('base_cflags')
    cppflags = common.get_variable('base_cppflags')

    cflags += get_option('c_args')
    cppflags += get_option('cpp_args')

    ldflags = common.get_variable('base_ldflags')

    ldflags += get_option('c_link_args')

    baseruby = run_command(quotetool, find_program('ruby').cmd_array(), check: true).stdout().strip()

    # Ruby's generated makefiles use GNU make, including on FreeBSD.
    gmake = find_program('gmake', required: false)
    if gmake.found()
        meson.override_find_program('make', gmake)
    endif
    ruby = declare_dependency(
        dependencies: [
            ruby_deps,
            import('unstable-external_project').add_project(
                'configure',
                configure_options: [
                    '--srcdir=' + source_dir,
                    '--prefix=' + '@PREFIX@',
                    '--prefix=/',
                    '--bindir=' + '@PREFIX@' / '@BINDIR@',
                    '--bindir=/bin',
                    '--libdir=' + '@PREFIX@' / '@LIBDIR@',
                    '--libdir=/lib',
                    '--includedir=' + '@PREFIX@' / '@INCLUDEDIR@',
                    '--includedir=/include',
                    '--with-rubyhdrdir=/include',
                    '--with-rubyarchhdrdir=/include',
                    '--with-destdir=' + destdir,
                    '--with-baseruby=' + baseruby,
                    '--with-soname=ruby',
                    '--disable-shared',
                    '--disable-yjit',
                    '--disable-install-doc',
                    # Ruby's Unix installer loads RubyGems, which requires monitor.
                    # The games themselves only require zlib.
                    '--with-ext=zlib,monitor',
                    '--with-static-linked-ext',
                    '--disable-rubygems',
                    '--without-gmp',
                    '--enable-mkmf-verbose',
                    'CC=' + run_command(quotetool, cc.cmd_array(), check: true).stdout().strip(),
                    'CXX=' + run_command(quotetool, cpp.cmd_array(), check: true).stdout().strip(),
                    'AR=' + run_command(quotetool, common.get_variable('ar'), check: true).stdout().strip(),
                    'RANLIB=' + run_command(quotetool, common.get_variable('ranlib'), check: true).stdout().strip(),
                    'AS=false',
                    'LD=false',
                    'NM=false',
                    'OBJCOPY=false',
                    'OBJDUMP=false',
                    'STRIP=false',
                    'CFLAGS=' + run_command(quotetool, cflags, check: true).stdout().strip() + ' ' + get_option('cflags'),
                    'CXXFLAGS=' + run_command(quotetool, cppflags, check: true).stdout().strip() + ' ' + get_option('cppflags'),
                    'LDFLAGS=' + run_command(quotetool, ldflags, check: true).stdout().strip() + ' ' + get_option('ldflags'),
                ],
                cross_configure_options: [
                    '--host=@HOST@',
                    '--host=' + common.get_variable('host_triplet'),
                ],
                env: {
                    'CONFIG_SITE': ' ',
                    'MKXP_LIBGCC': cc.has_link_argument('-lgcc') ? '-lgcc' : '',
                    # Disable LTO while running link tests for Ruby extensions if the linker is LLD (LLVM linker) and we are building for 32-bit ARM.
                    # Otherwise, the link tests may fail due to https://github.com/llvm/llvm-project/issues/108262.
                    'MKXP_DISABLE_LTO_FOR_LINK_TESTS': (cc.get_linker_id().split('.')[-1] == 'lld' or cc.get_linker_id() == 'lld-link') and host_machine.cpu_family() == 'arm' ? 'yes' : 'no',
                    'YACC': run_command(quotetool, find_program('bison').cmd_array(), check: true).stdout().strip(),
                    'ac_cv_func___builtin_setjmp': 'no',
                    'ac_cv_func_crypt': 'no',
                    'ac_cv_func_crypt_r': 'no',
                    'ac_cv_header_crypt_h': 'no',
                    'ac_cv_lib_crypt_crypt': 'no',
                    'ac_cv_lib_z_uncompress': 'yes',
                    'rb_cv_broken_crypt': 'no',
                    'rb_cv_gcc_atomic_builtins': 'yes',
                    'rb_cv_gcc_sync_builtins': 'no',
                    'rb_cv_prog_gnu_ld': common.get_variable('c_linker_is_gnu') ? 'yes' : 'no',
                    'rb_cv_symbol_prefix': cc.symbols_have_underscore_prefix() ? '_' : 'NONE',
                },
                depends: [
                    # Force Meson to fully build all of our vendored dependencies that are required by Ruby before building Ruby itself
                    executable(
                        'deps',
                        sources: [
                            'deps-stub.c',
                        ],
                        dependencies: [
                            ruby_deps,
                        ],
                    ),
                ],
                verbose: true,
            ).dependency('ruby-static').partial_dependency(
                sources: true,
            ),
            host_machine.system() != 'darwin'
                ? [
                    cc.find_library('atomic', required: false),
                    cc.find_library('dl', required: false),
                    cc.find_library('rt', required: false),
                ]
                : [],
            host_machine.system() == 'darwin'
                ? [dependency('appleframeworks', modules: ['CoreFoundation'])]
                : [],
            host_machine.system() == 'freebsd'
                ? [
                    cc.find_library('procstat', required: true),
                ]
                : [],
            host_machine.system() in ['freebsd', 'netbsd']
                ? [
                    cc.find_library('execinfo', required: true),
                ]
                : [],
        ],
        compile_args: [
            '-I' + destdir / 'include',
        ],
        link_args: [
            # If the executable format is ELF, we need this linker flag to export Ruby's symbols from our executable so that gems can find them.
            # Otherwise, any Ruby gems that have extensions will fail to load with undefined symbol errors.
            # For non-ELF executable formats (e.g. Mach-O or PE), this behavior is already the default so no linker flag is needed for non-ELF formats.
            cc.first_supported_link_argument('-rdynamic', '-Wl,-export-dynamic'),
            build_dir / 'build' / 'ext' / 'extinit.a',
            build_dir / 'build' / 'ext' / 'zlib' / 'zlib.a',
            build_dir / 'build' / 'ext' / 'monitor' / 'monitor.a',
            build_dir / 'build' / 'enc' / 'encinit.a',
            build_dir / 'build' / 'enc' / 'libenc.a',
            build_dir / 'build' / 'enc' / 'libtrans.a',
            build_dir / 'build' / 'libruby-static.a',
        ],
    )


    miniruby = custom_target(
        'miniruby',
        output: 'miniruby',
        command: [
            python,
            files('build-miniruby.py'),
            baseruby,
            build_dir / 'build',
            '@OUTPUT@',
        ],
        depends: [
            executable(
                'miniruby-deps',
                sources: [
                    'deps-stub.c',
                ],
                dependencies: [
                    ruby,
                ],
            ),
        ],
        build_by_default: true,
    )
endif

meson.override_dependency('ruby-4.0', ruby, static: true)
