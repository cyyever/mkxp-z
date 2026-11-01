asset_tool_link_args = []
if host_system == 'windows' and compilers['cpp'].get_argument_syntax() != 'msvc'
    asset_tool_link_args += '-municode'
endif
asset_converter = executable('mkxp-asset-convert', 'main.cpp',
    dependencies: [sdl3, sdl3_image, sdl3_sound],
    include_directories: include_directories('../../src'),
    override_options: engine_standards,
    link_args: asset_tool_link_args,
)
test('asset-container-conversion', python,
    args: [files('../../tests/asset-convert/test_converter.py'), asset_converter],
    timeout: 120)
