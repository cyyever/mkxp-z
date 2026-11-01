project('mkxp-z', 'c', 'cpp', version: '2.4.2', meson_version: '>=1.10.0',
        default_options: {'cpp_std': 'c++26,c++latest'})

c_std = 'gnu11,c11'
# MSVC exposes its implemented C++26 features through /std:c++latest.
cpp_std = 'c++26,c++latest'
engine_standards = {'c_std': c_std, 'cpp_std': cpp_std}
if host_machine.system() == 'darwin'
    add_languages('objcpp', native: false)
    engine_standards += {'objcpp_std': cpp_std}
endif

# Exercise the production portable code without downloading engine dependencies.
if get_option('core_tests_only')
    subdir('tests/core')
    subdir_done()
endif

gplv3_features = []

host_system = host_machine.system()
host_endian = host_machine.endian()
exe_name = meson.project_name() + '.' + host_machine.cpu()

is_bsd = host_system in ['dragonfly', 'freebsd', 'netbsd', 'openbsd']
is_unix = is_bsd or host_system in ['linux']

default_system_disabled = get_option('default_system').disabled()
default_system_enabled = get_option('default_system').enabled()

null_dep = dependency('', required: false)

compilers = {'cpp': meson.get_compiler('cpp')}

global_sources = []
global_dependencies = []
global_include_dirs = []
global_args = []
global_cpp_args = []
global_objc_args = ['-fobjc-arc', '-fobjc-weak']
global_objcpp_args = []
global_link_args = []
# External projects keep their own language baseline.
dependency_cpp_std = 'gnu++14,c++14'

common = subproject('common', default_options: {'c_std': c_std, 'cpp_std': dependency_cpp_std})
cmake = import('cmake')
python = common.get_variable('python')
embedtool = common.get_variable('embedtool')
quotetool = common.get_variable('quotetool')
sonametool = common.get_variable('sonametool')

if get_option('ruby_tests_only')
    subdir('tests/ruby')
    subdir_done()
endif

sizeof = {'void*': compilers['cpp'].sizeof('void*'),
          'long': compilers['cpp'].sizeof('long')
         }
win64 = (sizeof['void*'] != sizeof['long'])

global_args += '-DMKXPZ_VERSION="@0@"'.format(meson.project_version())
global_args += '-DHAVE_NANOSLEEP'

if get_option('gfx_backend') == 'gl'
    use_gles_by_default = false
elif get_option('gfx_backend') == 'gles'
    use_gles_by_default = true
elif host_system == 'android'
    use_gles_by_default = true
elif host_system == 'darwin'
    # If the host machine is macOS (as opposed to iOS, tvOS, etc.), use OpenGL by default, otherwise use OpenGL ES by default
    use_gles_by_default = false
elif is_unix
    use_gles_by_default = host_machine.cpu_family() in ['arm', 'aarch64']
else
    use_gles_by_default = false
endif
if use_gles_by_default
    global_args += '-DMKXPZ_USE_GLES_BY_DEFAULT'
endif

if host_system in ['windows', 'cygwin'] and host_machine.cpu_family() == 'x86'
    global_args += '-masm=intel'
endif

# ====================
# Ext libs
# ====================

# ====================
# Main source
# ====================

# Suppress warnings
global_args += ['-Wno-non-virtual-dtor', '-Wno-reorder', '-Wno-uninitialized', '-Wno-unknown-pragmas', '-Wno-stringop-truncation']
if compilers['cpp'].get_id() == 'clang'
    global_args += ['-Wno-undefined-var-template', '-Wno-delete-non-abstract-non-virtual-dtor']
endif

# Decide whether or not to use MiniFFI
miniffi = get_option('use_miniffi')
if miniffi == true
    miniffi = true
    global_args += '-DMKXPZ_MINIFFI'
endif

# Defines
if get_option('workdir_current')
    global_args += '-DWORKDIR_CURRENT'
endif

if get_option('force32') == true
    global_args += '-m32'
endif

subdir('src')
subdir('binding')
subdir('shader')
subdir('assets')

global_include_dirs += include_directories('src', 'binding')

rpath = meson.get_compiler('cpp').get_linker_id().split('.')[0] == 'ld64' ? '@loader_path' : '$ORIGIN'

if host_system in ['windows', 'cygwin']
    windows_resource_directory = '../' + get_option('windows_resource_directory')
    subdir('windows')
    global_include_dirs += include_directories('windows')
else
    windows_resources = []
endif

if host_system == 'darwin'
    global_dependencies += dependency(
        'appleframeworks',
        modules: [
            'AppKit',
            'Foundation',
            'GameController',
            'Metal',
        ],
        required: true,
    )
    subdir('macos/views')
endif

libmkxp = static_library(
    meson.project_name(),
    sources: global_sources,
    dependencies: global_dependencies,
    include_directories: global_include_dirs,
    c_args: global_args,
    cpp_args: global_args + global_cpp_args,
    objc_args: global_args + global_objc_args,
    objcpp_args: global_args + global_objc_args + global_cpp_args + global_objcpp_args,
    override_options: engine_standards,
)

executable(
    exe_name,
    sources: [
        git_hash_file, # Dummy source file just so that Meson doesn't complain about the executable not having any sources
        windows_resources,
    ],
    link_with: libmkxp,
    include_directories: global_include_dirs,
    link_args: global_link_args,
    win_subsystem: 'windows',
    build_rpath: rpath,
    install_rpath: rpath,
    install: true,
    install_dir: '.',
    install_tag: meson.project_name(),
)

global_install_tags = [meson.project_name()]
if host_system == 'darwin'
    global_install_tags += meson.project_name() + '-bundle'
endif

foreach install_tag : global_install_tags
    install_data(
        gplv3_features.length() != 0 ? 'assets/LICENSE.mkxp-z-with-https.txt' : 'COPYING',
        rename: 'LICENSE.txt',
        install_dir: '.',
        install_tag: install_tag,
    )

    install_data(
        'mkxp.json',
        install_dir: '.',
        install_tag: install_tag,
    )

    install_subdir(
        'scripts',
        install_dir: '.',
        install_tag: install_tag,
    )

    if ruby.type_name() == 'internal'
        meson.add_install_script(
            common.get_variable('install_directory'),
            ruby_build_dir / 'dist' / 'lib' / 'ruby' / (get_option('mri_version') + '.0'),
            'stdlib',
            install_tag: install_tag,
        )
    endif
endforeach

if host_system == 'darwin'
    executable(
        'bundle_' + meson.project_name(),
        sources: [
            git_hash_file, # Dummy source file just so that Meson doesn't complain about the executable not having any sources
            windows_resources,
        ],
        link_with: libmkxp,
        include_directories: global_include_dirs,
        link_args: global_link_args,
        win_subsystem: 'windows',
        build_rpath: rpath,
        install_rpath: rpath,
        install: true,
        install_dir: (exe_name + '.app') / 'Contents' / 'MacOS',
        install_tag: meson.project_name() + '-bundle',
    )

    configure_file(
        input: 'macos/Info.plist.in',
        output: 'Info.plist',
        configuration: {
            'CFBundleExecutable': 'bundle_' + meson.project_name(),
            'CFBundleGetInfoString': meson.project_name(),
            'CFBundleIdentifier': meson.project_name(),
            'CFBundleName': meson.project_name(),
            'CFBundleShortVersionString': meson.project_version(),
        },
        install: true,
        install_dir: (exe_name + '.app') / 'Contents',
        install_tag: meson.project_name() + '-bundle',
    )

    install_data(
        'macos/icon.icns',
        install_dir: (exe_name + '.app') / 'Contents' / 'Resources',
        install_tag: meson.project_name() + '-bundle',
    )
endif

if openal.type_name() == 'internal'
    summary('ALSA', have_alsa, bool_yn: true)
endif
summary('Extra shaders', have_extra_shaders, bool_yn: true)
if openal.type_name() == 'internal'
    summary('JACK Audio Connection Kit', have_jack, bool_yn: true)
endif
if openal.type_name() == 'internal'
    summary('OSS', have_oss, bool_yn: true)
endif
if openal.type_name() == 'internal'
    summary('PipeWire', have_pipewire, bool_yn: true)
endif
if openal.type_name() == 'internal'
    summary('PulseAudio', have_pulseaudio, bool_yn: true)
endif
summary('License', gplv3_features.length() != 0 ? 'GPL-3.0-or-later' : 'GPL-2.0-or-later')
if gplv3_features.length() != 0
    summary('Enabled features requiring GPLv3', gplv3_features)
endif
