if compilers['cpp'].get_id() == 'clang'
    global_args += '-fdeclspec'
endif

global_include_dirs += include_directories('.')

binding_source = [files(
    'binding-mri.cpp',
    'binding-util.cpp',
    'json-binding.cpp',
    'table-binding.cpp',
    'etc-binding.cpp',
    'bitmap-binding.cpp',
    'font-binding.cpp',
    'graphics-binding.cpp',
    'input-binding.cpp',
    'sprite-binding.cpp',
    'viewport-binding.cpp',
    'plane-binding.cpp',
    'window-binding.cpp',
    'tilemap-binding.cpp',
    'audio-binding.cpp',
    'module_rpg.cpp',
    'filesystem-binding.cpp'
)]

if miniffi == true
    binding_source += files('miniffi-binding.cpp')
    binding_source += files('miniffi.cpp')
endif

global_sources += binding_source

global_dependencies += declare_dependency(sources: custom_target(
    'system-prelude.rb.xxd',
    output: 'system-prelude.rb.xxd',
    command: [embedtool, files('system-prelude.rb'), '@OUTPUT@', 'mkxp_system_prelude_rb'],
))
