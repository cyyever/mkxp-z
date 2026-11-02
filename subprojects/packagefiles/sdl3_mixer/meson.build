# SDL_mixer has no upstream Meson build. Build only the header-only decoders the
# engine plays (WAV, FLAC via dr_flac, MP3 via dr_mp3, Ogg Vorbis via stb_vorbis),
# so no codec library or DLL is added. RAW and SINEWAVE are always compiled in
# upstream; they only decode when explicitly requested by name.
project('sdl3_mixer', 'c', version: '3.2.4')
sdl_dep = dependency('sdl3', version: '>=3.4.0')
version = meson.project_version().split('.')
mixer_args = [
  '-DBUILD_SDL',
  '-DSDL_BUILD_MAJOR_VERSION=' + version[0],
  '-DSDL_BUILD_MINOR_VERSION=' + version[1],
  '-DSDL_BUILD_MICRO_VERSION=' + version[2],
  '-DDECODER_WAV',
  '-DDECODER_FLAC_DRFLAC',
  '-DDECODER_MP3_DRMP3',
  '-DDECODER_OGGVORBIS_STB',
]
mixer_src = files(
  'src/SDL_mixer.c',
  'src/SDL_mixer_metadata_tags.c',
  'src/SDL_mixer_spatialization.c',
  'src/decoder_drflac.c',
  'src/decoder_drmp3.c',
  'src/decoder_raw.c',
  'src/decoder_sinewave.c',
  'src/decoder_stb_vorbis.c',
  'src/decoder_wav.c',
)
mixer_lib = library('sdl3_mixer', mixer_src, c_args: mixer_args,
                    include_directories: ['include', 'src'], dependencies: sdl_dep,
                    gnu_symbol_visibility: 'hidden')
sdl3_mixer_dep = declare_dependency(include_directories: 'include', link_with: mixer_lib,
                                    dependencies: sdl_dep)
