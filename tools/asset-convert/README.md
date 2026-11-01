# Verified asset container conversion

Enable the optional tool in an existing engine build:

```sh
meson setup --reconfigure build -Dasset_conversion_tools=true
meson compile -C build mkxp-asset-convert
meson test -C build asset-container-conversion --print-errorlogs
```

It reuses the engine's selected SDL3, SDL3_image and SDL3_sound libraries and adds
no decoder dependencies. Input decoders must still be enabled in that build.

```sh
mkxp-asset-convert image original.jpg staged.png
mkxp-asset-convert audio original.mp3 staged.wav
```

`--image` and `--audio` are accepted aliases. Parent directories must exist.
Sources are read-only. Destinations are created exclusively: existing files,
hardlinks, symlinks and source/destination collisions are refused. A failed write
or failed verification removes only the new output created by that invocation.

Each successful invocation emits one JSON object after reloading its output:

- Images: source and reloaded PNG are normalized to ABGR8888 and compared row by
  row, excluding padding. Dimensions and every decoded pixel byte must match.
- Audio: SDL_sound decodes with a null desired format, so no sample conversion or
  resampling is requested. The WAV preserves rate, channels and U8, S16LE, S32LE
  or F32LE samples. Float output uses IEEE_FLOAT/WAVEFORMATEX plus a fact chunk.
  Reloaded format, rate, channels and every sample byte must match. Unsupported
  sample formats are rejected rather than quantized. RIFF's 32-bit size limit
  applies.

Reports include `kind`, `source`, `destination`, `decoder`, `verified` and
`decoded_crc32`. Image reports include `width`, `height`, `pixel_format`; audio
reports include `rate`, `channels`, `sdl_audio_format`, `bits_per_sample`, `frames`,
`sample_bytes` and `wave_format` (`PCM` or `IEEE_FLOAT`). CRC32 describes decoded
bytes; verification itself compares full bytes, not hashes. Exit status is zero
only on verified success; diagnostics go to stderr.

Legacy save thumbnails can also be BMP. If the selected SDL3_image build
rejects a BMP, the tool uses SDL3's existing `SDL_LoadBMP` loader, reports that
fallback explicitly, and verifies the new PNG against its normalized pixels.
The original thumbnail remains read-only.

The vendored JPEG decoder is stb_image through SDL3_image. SDL_sound's MP3
(dr_mp3) and Ogg/Vorbis (stb_vorbis) decoders return F32, which stays F32 in WAV.
This preserves the chosen decoder output; it cannot recover detail previously
lost by JPEG, MP3 or Vorbis compression. It does not preserve container metadata.

When SDL_sound rejects a WAV, the tool tries SDL3's existing `SDL_LoadWAV`
decoder. This handles the games' A-law WAV files without FFmpeg. Its decoded
sample format/rate/channels and PCM bits are preserved and verified by reloading
the resulting ordinary WAV through SDL_sound; the JSON decoder field explicitly
identifies the fallback. A-law is expanded to its normative signed 16-bit PCM,
without quantizing those decoded values.

Default engine builds decode PNG images and WAV/FLAC audio. Enabling
`asset_conversion_tools` additionally builds JPEG, MP3 and Vorbis decoding for
this offline tool; runtime Bitmap loading and saving still require PNG.
The tool's stb_vorbis F32 output is not asserted to be bit-identical to older
engine versions' libvorbis S16 path.
Ogg loop tags, explicit filename references and extension collisions require a
separate game migration audit before replacing assets or disabling decoders.
The existing WMA-to-PCM16 FFmpeg assembly stage remains necessary for WMA input
unsupported by SDL_sound; the tool can verify/repackage that staged WAV, without
claiming to undo the earlier decode/conversion.

The Python regression test generates its own PNG and integer/float WAV fixtures.
It checks alpha, legacy 24-bit BMP pixels, Unicode paths, exact PCM bits including float signed zero and
subnormals, known A-law sample values, stereo/rate preservation, and
overwrite/source-collision refusal.
Real game assets belong in a separate staging audit, never in the source tree.
