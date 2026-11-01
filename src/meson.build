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
    'opensl',
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
    git = find_program('git').cmd_array()
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

zlib = dependency('zlib', allow_fallback: false, method: 'pkg-config', required: get_option('zlib_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not zlib.found()
    zlib_options = cmake.subproject_options()
    zlib_options.add_cmake_defines(cmake_base_options + {
        'BUILD_SHARED_LIBS': false,
        'ZLIB_BUILD_SHARED': false,
        'ZLIB_BUILD_STATIC': true,
        'ZLIB_BUILD_TESTING': false,
        'ZLIB_INSTALL': false,
    })
    foreach opt, val : cmake_override_options
        zlib_options.set_override_option(opt, val)
    endforeach
    zlib = cmake.subproject('zlib', options: zlib_options).dependency('zlibstatic')
    zlib_includes = ['-I' + meson.project_source_root() / 'subprojects' / ('zlib-' + zlib.version()), '-I' + meson.project_build_root() / 'subprojects' / ('zlib-' + zlib.version()) / '__CMake_build']
    zlib_libs = ['-L' + meson.project_build_root() / 'subprojects' / ('zlib-' + zlib.version())]
    meson.override_dependency('zlib', zlib, static: true)
else
    zlib_includes = ['-I' + zlib.get_variable(pkgconfig: 'includedir')]
    zlib_libs = ['-L' + zlib.get_variable(pkgconfig: 'libdir')]
endif
global_dependencies += zlib

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

sdl3_sound = dependency('sdl3_sound', allow_fallback: false,
    required: get_option('sdl_sound_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not sdl3_sound.found()
    sound_options = cmake.subproject_options()
    sound_options.add_cmake_defines(cmake_base_options + {
        'CMAKE_C_FLAGS': run_command(quotetool, sdl3_includes, check: true).stdout().strip(),
        'SDLSOUND_BUILD_SHARED': false,
        'SDLSOUND_BUILD_TEST': false,
        'SDLSOUND_INSTALL': false,
        # File decoders only: SDL3's native audio playback backends stay enabled.
        'SDLSOUND_DECODER_WAV': true,
        'SDLSOUND_DECODER_FLAC': true,
        'SDLSOUND_DECODER_VORBIS': get_option('asset_conversion_tools'),
        'SDLSOUND_DECODER_MP3': get_option('asset_conversion_tools'),
        'SDLSOUND_DECODER_AIFF': false,
        'SDLSOUND_DECODER_AU': false,
        'SDLSOUND_DECODER_VOC': false,
        'SDLSOUND_DECODER_RAW': false,
        'SDLSOUND_DECODER_SHN': false,
        'SDLSOUND_DECODER_MODPLUG': false,
        'SDLSOUND_DECODER_MIDI': false,
        'SDLSOUND_DECODER_COREAUDIO': get_option('asset_conversion_tools'),
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

sdl3_image = dependency('sdl3_image', version: '>=3.4.8', allow_fallback: false,
    required: get_option('sdl_image_system').disable_auto_if(default_system_disabled).enable_auto_if(default_system_enabled))
if not sdl3_image.found()
    # Legacy JPEG is needed only to rebuild PNG assets from original archives.
    sdl3_image = subproject('sdl3_image', default_options: meson_base_options + {
        'legacy_jpeg': get_option('asset_conversion_tools'),
    }).get_variable('sdl3_image_dep')
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

if compilers['cpp'].get_argument_syntax() == 'msvc'
    global_link_args += '/STACK:2097152'
else
    global_link_args += ['-static-libstdc++']
endif



global_include_dirs += include_directories('.',
    'audio',
    'display', 'display/gl',
    'etc',
    'filesystem',
    'input',
    'system',
    'util'
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

    'display/autotiles.cpp',
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

if host_system == 'darwin'
    main_source += files('filesystem/filename-normalization.mm')
endif

global_sources += main_source
