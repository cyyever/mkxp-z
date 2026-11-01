# Compile and embed native SDL_GPU bytecode; no runtime GLSL compiler.
have_extra_shaders = get_option('extra_shaders').allowed()
if have_extra_shaders
    gplv3_features += 'Extra shaders'
    global_args += '-DMKXPZ_HAVE_EXTRA_SHADERS'
endif
subdir('gpu')
foreach binary_name, binary : gpu_shader_binaries
    global_dependencies += declare_dependency(sources: custom_target(
        binary_name + '.xxd',
        input: binary,
        output: binary_name + '.xxd',
        command: [embedtool, '@INPUT@', '@OUTPUT@', 'mkxp_gpu_' + binary_name.replace('.', '_')],
    ))
endforeach
if gpu_shader_formats.length() == 0
    error('No offline GPU shader producer for this platform yet; SPIR-V/Metal artifacts are still required.')
endif
registry_python = find_program('python3', 'python')
registry_tool = files('../tools/shaders/embed-registry.py')
registry_extra = have_extra_shaders ? ['--extra-shaders'] : []
global_dependencies += declare_dependency(sources: custom_target(
    'gpu-embedded-registry',
    output: 'gpu-embedded-shaders.h',
    depend_files: files('../tools/shaders/lint-hlsl.py'),
    command: [registry_python, registry_tool, '--output', '@OUTPUT@', '--formats'] + gpu_shader_formats + registry_extra,
))
