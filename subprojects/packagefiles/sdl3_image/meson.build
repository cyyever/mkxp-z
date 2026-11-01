# SDL_image has no upstream Meson build. Keep the same small overlay contract
# as the former WrapDB 3.2.4 build, using upstream 3.4.8's CMake source list.
project('sdl3_image', 'c', version: '3.4.8')
sdl_dep = dependency('sdl3', version: '>=3.4.0')
# PNG uses SDL's built-in load/save API. stb is only needed by offline JPEG.
img_args = ['-DSDL_IMAGE_USE_COMMON_BACKEND', '-DLOAD_PNG', '-DSAVE_PNG=1']
if get_option('legacy_jpeg')
  img_args += ['-DUSE_STBIMAGE', '-DLOAD_JPG', '-DSAVE_JPG=1']
else
  img_args += '-DSAVE_JPG=0'
endif
subdir('src')
img_lib = library('sdl3_image', img_src, c_args: img_args,
                  include_directories: 'include', dependencies: sdl_dep)
sdl3_image_dep = declare_dependency(include_directories: 'include', link_with: img_lib,
                                   dependencies: sdl_dep)
