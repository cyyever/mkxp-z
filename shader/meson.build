# Compile and embed native SDL_GPU bytecode; no runtime GLSL compiler.
have_extra_shaders = get_option('extra_shaders').allowed()
if have_extra_shaders
    gplv3_features += 'Extra shaders'
    global_args += '-DMKXPZ_HAVE_EXTRA_SHADERS'
endif
subdir('gpu')
foreach entry, binary : gpu_shader_binaries
    global_dependencies += declare_dependency(sources: custom_target(
        entry + '.dxbc.xxd',
        input: binary,
        output: entry + '.dxbc.xxd',
        command: [embedtool, '@INPUT@', '@OUTPUT@', 'mkxp_gpu_' + entry],
    ))
endforeach
