foreach dep_name : [
    host_system != 'linux' ? 'alsa' : '',
    'audioio',
    'avahi-client',
    'epoll-shim',
    'fftw3f',
    'girepository-2.0',
    'gobject-introspection-1.0',
    'gthread-2.0',
    'gtk+-3.0',
    'intl',
    not is_unix ? 'jack' : '',
    'libapparmor',
    'libasyncns',
    'libcanberra',
    'libcrypt',
    'libffado',
    'libpcre2-8',
    not is_unix ? 'libpulse' : '',
    'libpulse-simple',
    'libselinux',
    'libsystemd',
    'libusb-1.0',
    'libxcrypt',
    'mount',
    'ncursesw',
    'oboe',
    host_system != 'android' ? 'opensl' : '',
    'opus',
    not is_unix ? 'oss' : '',
    'portaudio',
    'roc',
    'snapd-glib',
    'snapd-glib-2',
    'sndfile',
    'sndio',
    'systemd',
    'vulkan',
    'xscrnsaver',
    'xtrans',
]
    if dep_name != ''
        meson.override_dependency(dep_name, null_dep, static: true)
    endif
endforeach

cmake_base_options = {
    'CMAKE_POLICY_VERSION_MINIMUM': '3.10',
    'CMAKE_OSX_SYSROOT': '/nonexistent',
    'CMAKE_C_STANDARD': common.get_variable('c_std').replace('-std=c', '').replace('-std=gnu', '').replace('/std:c', ''),
    'CMAKE_CXX_STANDARD': common.get_variable('cpp_std').replace('-std=c++', '').replace('-std=gnu++', '').replace('/std:c++', ''),
    'CMAKE_C_BYTE_ORDER': host_endian == 'big' ? 'BIG_ENDIAN' : 'LITTLE_ENDIAN',
    'CMAKE_CXX_BYTE_ORDER': host_endian == 'big' ? 'BIG_ENDIAN' : 'LITTLE_ENDIAN',
    'CMAKE_OBJC_BYTE_ORDER': host_endian == 'big' ? 'BIG_ENDIAN' : 'LITTLE_ENDIAN',
    'CMAKE_OBJCXX_BYTE_ORDER': host_endian == 'big' ? 'BIG_ENDIAN' : 'LITTLE_ENDIAN',
    'CMAKE_BUILD_TYPE': 'Custom',
    'CMAKE_EXE_LINKER_FLAGS': run_command(quotetool, common.get_variable('c_linker_flags'), check: true).stdout().strip(),
}

cmake_override_options = {
    'b_coverage': 'false',
}

meson_base_options = cmake_override_options + {
    'default_library': 'static',
    'c_std': c_std,
    'cpp_std': dependency_cpp_std,
}

git_hash_fallback = ''
foreach line : import('fs').read('../git-hash').splitlines()
    line = line.strip()
    if line != '' and not line.startswith('#')
        git_hash_fallback = line.substring(0, 7)
    endif
endforeach
distscript_vcs_command = []
if git_hash_fallback != ''
    git_hash_file = configure_file(
        input: 'git-hash.h.in',
        output: 'git-hash.h',
        configuration: {
            'VCS_TAG': git_hash_fallback,
        },
    )
else
    git = meson.version().version_compare('>=1.10.0') ? find_program('git').cmd_array() : find_program('git')
    git_hash_file = vcs_tag(
        input: 'git-hash.h.in',
        output: 'git-hash.h',
        command: [git, 'rev-parse', '--short=7', 'HEAD'],
        fallback: 'unknown',
    )
    distscript_vcs_command = [git, 'rev-parse', 'HEAD']
endif
meson.add_dist_script(python, files('../distscript.py'), get_option('dist_subprojects'), distscript_vcs_command)
global_dependencies += declare_dependency(sources: git_hash_file)

gl = dependency('gl', allow_fallback: false, required: get_option('gl_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not gl.found()
    gl = subproject('mesa', default_options: meson_base_options).get_variable('gl')
endif

libiconv = dependency('iconv', allow_fallback: false, required: get_option('iconv_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not libiconv.found()
    libiconv_options = meson_base_options
    libiconv = subproject('libiconv', default_options: libiconv_options).get_variable('libiconv')
endif
global_dependencies += libiconv

zlib = dependency('zlib', allow_fallback: false, method: 'pkg-config', required: get_option('zlib_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not zlib.found()
    zlib_options = cmake.subproject_options()
    zlib_options.add_cmake_defines(cmake_base_options + {
        'BUILD_SHARED_LIBS': false,
        'ZLIB_BUILD_EXAMPLES': false,
    })
    foreach opt, val : cmake_override_options
        zlib_options.set_override_option(opt, val)
    endforeach
    zlib = cmake.subproject('zlib', options: zlib_options).dependency('z')
    zlib_includes = ['-I' + meson.project_source_root() / 'subprojects' / ('zlib-' + zlib.version()), '-I' + meson.project_build_root() / 'subprojects' / ('zlib-' + zlib.version()) / '__CMake_build']
    zlib_libs = ['-L' + meson.project_build_root() / 'subprojects' / ('zlib-' + zlib.version())]
    meson.override_dependency('zlib', zlib, static: true)
else
    zlib_includes = ['-I' + zlib.get_variable(pkgconfig: 'includedir')]
    zlib_libs = ['-L' + zlib.get_variable(pkgconfig: 'libdir')]
endif
global_dependencies += zlib

physfs = dependency('physfs', allow_fallback: false, required: get_option('physfs_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not physfs.found()
    physfs_options = cmake.subproject_options()
    physfs_options.add_cmake_defines(cmake_base_options + {
        'CMAKE_OBJC_FLAGS': run_command(quotetool, '-fno-objc-arc', '-fno-objc-weak', check: true).stdout().strip(),
        'PHYSFS_BUILD_STATIC': true,
        'PHYSFS_BUILD_SHARED': false,
        'PHYSFS_BUILD_TEST': false,
        'PHYSFS_BUILD_DOCS': false,
    })
    foreach opt, val : cmake_override_options
        physfs_options.set_override_option(opt, val)
    endforeach
    physfs = declare_dependency(dependencies: [
        cmake.subproject('physfs', options: physfs_options).dependency('physfs-static'),
        host_system == 'darwin'
            ? dependency(
                'appleframeworks',
                modules: [
                    'Foundation',
                    'IOKit',
                ],
                required: true,
            )
            : [],
    ])
    meson.override_dependency('physfs', physfs, static: true)
endif
global_dependencies += physfs

sdl3 = dependency('sdl3', version: '>=3.4', allow_fallback: false,
    required: get_option('sdl_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not sdl3.found()
    sdl3_options = cmake.subproject_options()
    sdl3_options.add_cmake_defines(cmake_base_options + {
        'SDL_SHARED': false,
        'SDL_STATIC': true,
        'SDL_TEST_LIBRARY': false,
        'SDL_TESTS': false,
        'SDL_INSTALL': false,
        # Subsystems the engine never calls. GPU stays for the planned GPU port;
        # HIDAPI stays because it provides the gamepad drivers.
        'SDL_CAMERA': false,
        'SDL_DIALOG': false,
        'SDL_HAPTIC': false,
        'SDL_RENDER': false,
        'SDL_SENSOR': false,
        'SDL_TRAY': false,
        'SDL_VULKAN': false,
    })
    sdl3 = cmake.subproject('sdl3', options: sdl3_options).dependency('SDL3-static')
    meson.override_dependency('sdl3', sdl3, static: true)
    sdl3_includes = ['-I' + meson.project_source_root() / 'subprojects' / ('SDL3-' + sdl3.version()) / 'include']
else
    sdl3_includes = ['-I' + sdl3.get_variable(pkgconfig: 'includedir')]
endif
global_dependencies += sdl3

# SDL3 owns the platform backends. Native OpenGL remains until the GPU port.

have_alsa = false
have_jack = false
have_oss = false
have_pipewire = false
have_pulseaudio = false

openal = dependency('openal', allow_fallback: false, required: get_option('openal_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not openal.found()
    fmt = dependency('fmt', allow_fallback: false, required: get_option('fmt_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
    if not fmt.found()
        fmt_options = cmake.subproject_options()
        fmt_options.add_cmake_defines(cmake_base_options + {
            'LIBTYPE': 'STATIC',
            'FMT_DOC': false,
            'FMT_TEST': false,
            'FMT_FUZZ': false,
            'FMT_CUDA_TEST': false,
            'FMT_MODULE': false,
            'FMT_WERROR': false,
        })
        foreach opt, val : cmake_override_options
            fmt_options.set_override_option(opt, val)
        endforeach
        fmt = cmake.subproject('fmt', options: fmt_options).dependency('fmt')
        meson.override_dependency('fmt', fmt, static: true)
    endif

    if is_unix
        have_oss = meson.get_compiler('c').has_header_symbol('sys/soundcard.h', 'SOUND_VERSION', required: get_option('oss'))
        if have_oss
            meson.override_dependency('oss', declare_dependency(version: 'undefined'), static: true)
        else
            meson.override_dependency('oss', null_dep, static: true)
        endif
    endif

    if host_system == 'linux'
        require_system_alsa = get_option('alsa_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled)
        if get_option('alsa').disabled() or (get_option('alsa').auto() and require_system_alsa.enabled())
            require_system_alsa = get_option('alsa')
        endif
        alsa = dependency('alsa', allow_fallback: false, required: require_system_alsa)
        if not alsa.found() and not get_option('alsa').disabled() and not get_option('alsa_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled).enabled()
            alsa_options = meson_base_options
            alsa = subproject('alsa-lib', default_options: alsa_options).get_variable('alsa_lib')
        endif
        have_alsa = alsa.found()
        if not have_alsa
            meson.override_dependency('alsa', null_dep, static: true)
        endif
    endif

    if is_unix
        require_system_jack = get_option('jack_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled)
        if get_option('jack').disabled() or (get_option('jack').auto() and require_system_jack.enabled())
            require_system_jack = get_option('jack')
        endif
        jack = dependency('jack', allow_fallback: false, required: require_system_jack)
        if not jack.found() and not get_option('jack').disabled() and not get_option('jack_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled).enabled()
            jack_options = meson_base_options
            jack = subproject('jack1', default_options: jack_options).get_variable('jack1')
        endif
        have_jack = jack.found()
        if not have_jack
            meson.override_dependency('jack', null_dep, static: true)
        endif

        require_system_pulseaudio = get_option('pulseaudio_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled)
        if get_option('pulseaudio').disabled() or (get_option('pulseaudio').auto() and require_system_pulseaudio.enabled())
            require_system_pulseaudio = get_option('pulseaudio')
        endif
        pulseaudio = dependency('libpulse', allow_fallback: false, required: require_system_pulseaudio)
        if not pulseaudio.found() and not get_option('pulseaudio').disabled() and not get_option('pulseaudio_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled).enabled()
            pulseaudio_options = meson_base_options
            pulseaudio = subproject('pulseaudio', default_options: pulseaudio_options).get_variable('pulseaudio')
        endif
        have_pulseaudio = pulseaudio.found()
        if not have_pulseaudio
            meson.override_dependency('libpulse', null_dep, static: true)
        endif

        require_system_pipewire = get_option('pipewire_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled)
        if get_option('pipewire').disabled() or (get_option('pipewire').auto() and require_system_pipewire.enabled())
            require_system_pipewire = get_option('pipewire')
        endif
        pipewire = dependency('libpipewire-0.3', allow_fallback: false, required: require_system_pipewire)
        if not pipewire.found() and not get_option('pipewire').disabled() and not get_option('pipewire_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled).enabled()
            pipewire_options = meson_base_options
            pipewire = subproject('pipewire', default_options: pipewire_options).get_variable('pipewire')
        endif
        have_pipewire = pipewire.found()
        if not have_pipewire
            meson.override_dependency('libpipewire-0.3', null_dep, static: true)
        endif
    endif

    neon = 'disabled'
    if host_machine.cpu_family() == 'arm'
        if get_option('arm_neon').disabled()
            neon = 'disabled'
        elif get_option('arm_neon').enabled()
            neon = 'enabled'
        else
            neon = 'auto'
        endif
    endif
    if host_machine.cpu_family() == 'aarch64'
        if get_option('aarch64_neon').disabled()
            neon = 'disabled'
        elif get_option('aarch64_neon').enabled()
            neon = 'enabled'
        else
            neon = 'auto'
        endif
    endif
    sse = 'disabled'
    if host_machine.cpu_family() == 'x86'
        if get_option('x86_sse').disabled()
            sse = 'disabled'
        elif get_option('x86_sse').enabled()
            sse = 'enabled'
        else
            sse = 'auto'
        endif
    endif
    if host_machine.cpu_family() == 'x86_64'
        if get_option('x86_64_sse').disabled()
            sse = 'disabled'
        elif get_option('x86_64_sse').enabled()
            sse = 'enabled'
        else
            sse = 'auto'
        endif
    endif
    sse2 = 'disabled'
    if host_machine.cpu_family() == 'x86'
        if get_option('x86_sse2').disabled()
            sse2 = 'disabled'
        elif get_option('x86_sse2').enabled()
            sse2 = 'enabled'
        else
            sse2 = 'auto'
        endif
    endif
    if host_machine.cpu_family() == 'x86_64'
        if get_option('x86_64_sse2').disabled()
            sse2 = 'disabled'
        elif get_option('x86_64_sse2').enabled()
            sse2 = 'enabled'
        else
            sse2 = 'auto'
        endif
    endif
    sse3 = 'disabled'
    if host_machine.cpu_family() == 'x86'
        if get_option('x86_sse3').disabled()
            sse3 = 'disabled'
        elif get_option('x86_sse3').enabled()
            sse3 = 'enabled'
        else
            sse3 = 'auto'
        endif
    endif
    if host_machine.cpu_family() == 'x86_64'
        if get_option('x86_64_sse3').disabled()
            sse3 = 'disabled'
        elif get_option('x86_64_sse3').enabled()
            sse3 = 'enabled'
        else
            sse3 = 'auto'
        endif
    endif
    sse41 = 'disabled'
    if host_machine.cpu_family() == 'x86'
        if get_option('x86_sse41').disabled()
            sse41 = 'disabled'
        elif get_option('x86_sse41').enabled()
            sse41 = 'enabled'
        else
            sse41 = 'auto'
        endif
    endif
    if host_machine.cpu_family() == 'x86_64'
        if get_option('x86_64_sse41').disabled()
            sse41 = 'disabled'
        elif get_option('x86_64_sse41').enabled()
            sse41 = 'enabled'
        else
            sse41 = 'auto'
        endif
    endif
    openal_options = meson_base_options + {
        'rtkit': 'disabled',
        'sdl2': 'disabled',
        'utils': false,
        'neon': neon,
        'sse': sse,
        'sse2': sse2,
        'sse3': sse3,
        'sse41': sse41,
    }
    openal = subproject('openal-soft', default_options: openal_options).get_variable('openal_dep')
endif
global_dependencies += openal

ogg = dependency('ogg', allow_fallback: false, method: 'pkg-config', required: get_option('ogg_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not ogg.found()
    ogg_options = cmake.subproject_options()
    ogg_options.add_cmake_defines(cmake_base_options + {
        'BUILD_SHARED_LIBS': false,
        'BUILD_TESTING': false,
        'BUILD_FRAMEWORK': false,
    })
    foreach opt, val : cmake_override_options
        ogg_options.set_override_option(opt, val)
    endforeach
    ogg = cmake.subproject('ogg', options: ogg_options).dependency('ogg')
    ogg_includes = ['-I' + meson.project_source_root() / 'subprojects' / ('libogg-' + ogg.version()) / 'include', '-I' + meson.project_build_root() / 'subprojects' / ('libogg-' + ogg.version()) / '__CMake_build' / 'include']
    meson.override_dependency('ogg', ogg, static: true)
else
    ogg_includes = ['-I' + ogg.get_variable(pkgconfig: 'includedir')]
endif
global_dependencies += ogg

vorbis = dependency('vorbis', allow_fallback: false, required: get_option('vorbis_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not vorbis.found()
    vorbis_internal_cflags = []
    vorbis_external_cflags = []
    if ogg.type_name() == 'internal'
        vorbis_internal_cflags += ogg_includes
    else
        vorbis_external_cflags += ogg_includes
    endif
    vorbis_options = cmake.subproject_options()
    vorbis_options.add_cmake_defines(cmake_base_options + {
        'CMAKE_C_FLAGS': run_command(quotetool, vorbis_internal_cflags + vorbis_external_cflags, check: true).stdout().strip(),
        'CMAKE_CXX_FLAGS': run_command(quotetool, vorbis_internal_cflags + vorbis_external_cflags, check: true).stdout().strip(),
        'BUILD_SHARED_LIBS': false,
        'BUILD_TESTING': false,
        'BUILD_FRAMEWORK': false,
    })
    foreach opt, val : cmake_override_options
        vorbis_options.set_override_option(opt, val)
    endforeach
    vorbis = declare_dependency(dependencies: [
        ogg,
        cmake.subproject('vorbis', options: vorbis_options).dependency('vorbis'),
    ])
    vorbisfile = declare_dependency(dependencies: [
        vorbis,
        cmake.subproject('vorbis', options: vorbis_options).dependency('vorbisfile'),
    ])
    meson.override_dependency('vorbis', vorbis, static: true)
    meson.override_dependency('vorbisfile', vorbisfile, static: true)
else
    vorbisfile = dependency('vorbisfile', allow_fallback: false, required: true)
endif
global_dependencies += [vorbis, vorbisfile]

sdl3_sound = dependency('sdl3_sound', allow_fallback: false,
    required: get_option('sdl_sound_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not sdl3_sound.found()
    sound_options = cmake.subproject_options()
    sound_options.add_cmake_defines(cmake_base_options + {
        'CMAKE_C_FLAGS': run_command(quotetool, sdl3_includes, check: true).stdout().strip(),
        'SDLSOUND_BUILD_SHARED': false,
        'SDLSOUND_BUILD_TEST': false,
        'SDLSOUND_INSTALL': false,
    })
    sdl3_sound = declare_dependency(dependencies: [sdl3,
        cmake.subproject('sdl3_sound', options: sound_options).dependency('SDL3_sound-static')])
endif
global_dependencies += sdl3_sound

freetype = dependency('freetype2', allow_fallback: false, method: 'pkg-config', required: get_option('freetype_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
freetype_system = freetype.found()
if not freetype.found()
    freetype_options = meson_base_options + {
        'brotli': 'disabled',
        'bzip2': 'disabled',
        'harfbuzz': 'disabled',
        'mmap': 'disabled',
        'png': 'disabled',
        'tests': 'disabled',
        'zlib': 'disabled',
    }
    freetype = subproject('freetype', default_options: freetype_options).get_variable('freetype_dep')
    freetype_includes = ['-I' + meson.project_source_root() / 'subprojects' / ('freetype-VER-' + freetype.version().replace('.', '-')) / 'include']
else
    freetype_includes = ['-I' + (freetype.get_variable(pkgconfig: 'includedir') / 'freetype2')]
endif
global_dependencies += freetype

sdl3_ttf = dependency('sdl3_ttf', allow_fallback: false,
    required: get_option('sdl_ttf_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not sdl3_ttf.found()
    # Share the same FreeType dependency with SDL_ttf and our font metrics code.
    if freetype_system
        meson.override_dependency('freetype2', freetype, static: true)
    endif
    sdl3_ttf = subproject('sdl3_ttf', default_options: meson_base_options).get_variable('sdl3_ttf_dep')
endif
global_dependencies += sdl3_ttf
global_args += '-DMKXPZ_TTF_FONT=TTF_Font'

pixman = dependency('pixman-1', allow_fallback: false, required: get_option('pixman_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not pixman.found()
    loongson_mmi = 'disabled'
    if host_machine.cpu_family() == 'loongarch64'
        if get_option('loongarch64_mmi').disabled()
            loongson_mmi = 'disabled'
        elif get_option('loongarch64_mmi').enabled()
            loongson_mmi = 'enabled'
        else
            loongson_mmi = 'auto'
        endif
    endif
    mmx = 'disabled'
    if host_machine.cpu_family() == 'x86'
        if get_option('x86_mmx').disabled()
            mmx = 'disabled'
        elif get_option('x86_mmx').enabled()
            mmx = 'enabled'
        else
            mmx = 'auto'
        endif
    endif
    if host_machine.cpu_family() == 'x86_64'
        if get_option('x86_64_mmx').disabled()
            mmx = 'disabled'
        elif get_option('x86_64_mmx').enabled()
            mmx = 'enabled'
        else
            mmx = 'auto'
        endif
    endif
    sse2 = 'disabled'
    if host_machine.cpu_family() == 'x86'
        if get_option('x86_sse2').disabled()
            sse2 = 'disabled'
        elif get_option('x86_sse2').enabled()
            sse2 = 'enabled'
        else
            sse2 = 'auto'
        endif
    endif
    if host_machine.cpu_family() == 'x86_64'
        if get_option('x86_64_sse2').disabled()
            sse2 = 'disabled'
        elif get_option('x86_64_sse2').enabled()
            sse2 = 'enabled'
        else
            sse2 = 'auto'
        endif
    endif
    ssse3 = 'disabled'
    if host_machine.cpu_family() == 'x86'
        if get_option('x86_ssse3').disabled()
            ssse3 = 'disabled'
        elif get_option('x86_ssse3').enabled()
            ssse3 = 'enabled'
        else
            ssse3 = 'auto'
        endif
    endif
    if host_machine.cpu_family() == 'x86_64'
        if get_option('x86_64_ssse3').disabled()
            ssse3 = 'disabled'
        elif get_option('x86_64_ssse3').enabled()
            ssse3 = 'enabled'
        else
            ssse3 = 'auto'
        endif
    endif
    vmx = 'disabled'
    if host_machine.cpu_family() in ['ppc', 'ppc64']
        if get_option('ppc_altivec').disabled()
            vmx = 'disabled'
        elif get_option('ppc_altivec').enabled()
            vmx = 'enabled'
        else
            vmx = 'auto'
        endif
    endif
    arm_simd = 'disabled'
    if host_machine.cpu_family() == 'arm'
        if get_option('arm_armv6').disabled()
            arm_simd = 'disabled'
        elif get_option('arm_armv6').enabled()
            arm_simd = 'enabled'
        else
            arm_simd = 'auto'
        endif
    endif
    neon = 'disabled'
    if host_machine.cpu_family() == 'arm'
        if get_option('arm_neon').disabled()
            neon = 'disabled'
        elif get_option('arm_neon').enabled()
            neon = 'enabled'
        else
            neon = 'auto'
        endif
    endif
    a64_neon = 'disabled'
    if host_machine.cpu_family() == 'aarch64'
        if get_option('aarch64_neon').disabled()
            a64_neon = 'disabled'
        elif get_option('aarch64_neon').enabled()
            a64_neon = 'enabled'
        else
            a64_neon = 'auto'
        endif
    endif
    mips_dspr2 = 'disabled'
    if host_machine.cpu_family() == 'mips'
        if get_option('mips_dspr2').disabled()
            mips_dspr2 = 'disabled'
        elif get_option('mips_dspr2').enabled()
            mips_dspr2 = 'enabled'
        else
            mips_dspr2 = 'auto'
        endif
    endif
    rvv = 'disabled'
    if host_machine.cpu_family() in ['riscv32', 'riscv64']
        if get_option('riscv_rvv').disabled()
            rvv = 'disabled'
        elif get_option('riscv_rvv').enabled()
            rvv = 'enabled'
        else
            rvv = 'auto'
        endif
    endif
    pixman_options = meson_base_options + {
        'demos': 'disabled',
        'libpng': 'disabled',
        'tests': 'disabled',
        'tls': 'disabled',
        'loongson-mmi': loongson_mmi,
        'mmx': mmx,
        'sse2': sse2,
        'ssse3': ssse3,
        'vmx': vmx,
        'arm-simd': arm_simd,
        'neon': neon,
        'a64-neon': host_system == 'darwin' ? 'disabled' : a64_neon,
        'mips-dspr2': mips_dspr2,
        'rvv': rvv,
    }
    pixman = subproject('pixman', default_options: pixman_options).get_variable('idep_pixman')
endif
global_dependencies += pixman

uchardet = dependency('uchardet', allow_fallback: false, required: get_option('uchardet_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not uchardet.found()
    uchardet_options = cmake.subproject_options()
    uchardet_options.add_cmake_defines(cmake_base_options + {
        'BUILD_BINARY': false,
        'BUILD_SHARED_LIBS': false,
    })
    foreach opt, val : cmake_override_options
        uchardet_options.set_override_option(opt, val)
    endforeach
    uchardet = cmake.subproject('uchardet', options: uchardet_options).dependency('libuchardet')
    meson.override_dependency('uchardet', uchardet, static: true)
endif
global_dependencies += uchardet

sdl3_image = dependency('sdl3_image', allow_fallback: false,
    required: get_option('sdl_image_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not sdl3_image.found()
    # Built with PNG and JPG only (stb_image decoders, built-in writers); see sdl3_image-png-jpg.patch.
    sdl3_image = subproject('sdl3_image', default_options: meson_base_options).get_variable('sdl3_image_dep')
endif
global_dependencies += sdl3_image



ruby = dependency('ruby-' + get_option('mri_version'), allow_fallback: false, required: get_option('ruby_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not ruby.found()
    ruby_internal_cflags = []
    ruby_external_cflags = []
    ruby_internal_ldflags = []
    ruby_external_ldflags = []
    if zlib.type_name() == 'internal'
        ruby_internal_cflags += zlib_includes
        ruby_internal_ldflags += zlib_libs
    else
        ruby_external_cflags += zlib_includes
        ruby_external_ldflags += zlib_libs
    endif
    ruby_options = meson_base_options + {
        'cflags': run_command(quotetool, ruby_internal_cflags + ruby_external_cflags, check: true).stdout().strip(),
        'cppflags': run_command(quotetool, ruby_internal_cflags + ruby_external_cflags, check: true).stdout().strip(),
        'ldflags': run_command(quotetool, ruby_internal_ldflags + ruby_external_ldflags, check: true).stdout().strip()
    }
    ruby = subproject('ruby-' + get_option('mri_version'), default_options: ruby_options).get_variable('ruby')
    ruby_build_dir = subproject('ruby-' + get_option('mri_version'), default_options: ruby_options).get_variable('ruby_build_dir')
endif
global_dependencies += ruby

if host_system in ['windows', 'cygwin']
    global_link_args += ['-static-libgcc', '-static-libstdc++', '-Wl,-Bstatic', '-lpthread', '-Wl,-Bdynamic', '-Wl,--stack=0x200000']
else
    global_link_args += ['-static-libstdc++']
endif



global_include_dirs += include_directories('.',
    'audio',
    'crypto',
    'display', 'display/gl',
    'etc',
    'filesystem',
    'input',
    'net',
    'system',
    'util', 'util/sigslot', 'util/sigslot/adapter'
)


if get_option('cjk_fallback_font') == true
    global_cpp_args += '-DMKXPZ_CJK_FONT'
endif

main_source = files(
    'main.cpp',
    'config.cpp',
    'eventthread.cpp',
    'settingsmenu.cpp',
    'sharedstate.cpp',

    'audio/alstream.cpp',
    'audio/audio.cpp',
    'audio/audiostream.cpp',
    'audio/sdlsoundsource.cpp',
    'audio/soundemitter.cpp',
    'audio/vorbissource.cpp',

    'crypto/rgssad.cpp',

    'display/autotiles.cpp',
    'display/autotilesvx.cpp',
    'display/bitmap.cpp',
    'display/font.cpp',
    'display/graphics.cpp',
    'display/plane.cpp',
    'display/sprite.cpp',
    'display/tilemap.cpp',
    'display/viewport.cpp',
    'display/window.cpp',


    'display/gl/gl-debug.cpp',
    'display/gl/gl-fun.cpp',
    'display/gl/gl-meta.cpp',
    'display/gl/glstate.cpp',
    'display/gl/scene.cpp',
    'display/gl/shader.cpp',
    'display/gl/texpool.cpp',
    'display/gl/tileatlas.cpp',
    'display/gl/tilequad.cpp',
    'display/gl/vertex.cpp',

    'util/iniconfig.cpp',
    'util/win-consoleutils.cpp',

    'etc/etc.cpp',
    'etc/table.cpp',

    'filesystem/filesystem.cpp',

    'input/input.cpp',
    'input/keybindings.cpp',


    host_system == 'darwin' ? 'system/systemImplApple.mm' : 'system/systemImpl.cpp',
)

global_sources += main_source
