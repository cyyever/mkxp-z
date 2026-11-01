"""Run a built engine in an isolated fixture; Linux CI uses xvfb-run."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import struct
import zlib
import sys
import tempfile
import wave


def write_png(path, rgba):
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' +
                     chunk(b'IHDR', struct.pack('>IIBBBBB', 1, 1, 8, 6, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(b'\x00' + bytes(rgba))) + chunk(b'IEND', b''))


def run_case(binary, path_cache):
    repository = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='mkxp-engine-smoke-') as temporary:
        fixture = Path(temporary)
        executable = fixture / binary.name
        shutil.copy2(binary, executable)
        configuration = {
            'rgssVersion': 1,
            'gameFolder': str(fixture),
            'customScript': str(Path(__file__).with_suffix('.rb').resolve()),
            'RTP': [str(fixture / 'rtp'), str(repository / 'assets')],
            'pathCache': path_cache,
            'enableSettings': False,
            'winConsole': False,
            'windowTitle': 'SDL3 migration smoke',
            'defScreenW': 640,
            'defScreenH': 480,
        }
        write_png(fixture / 'Graphics/cache/Hero.png', (255, 0, 0, 255))
        write_png(fixture / 'rtp/Graphics/cache/Hero.png', (0, 255, 0, 255))
        # Exercise the Ruby audio API through filesystem lookup, SDL_sound and SDL3.
        for name, channels, rate, sample_width in [('tone', 2, 48000, 2), ('tone-mono', 1, 22050, 1)]:
            with wave.open(str(fixture / (name + '.wav')), 'wb') as output:
                output.setnchannels(channels)
                output.setsampwidth(sample_width)
                output.setframerate(rate)
                silence = b'\x80' if sample_width == 1 else b'\x00\x00'
                output.writeframes(silence * channels * rate * 2)
        (fixture / 'invalid.wav').write_bytes(b'not a WAV file')
        (fixture / 'mkxp.json').write_text(json.dumps(configuration), encoding='utf-8')
        # Config reads the INI before applying customScript; avoid unrelated diagnostics.
        (fixture / 'Game.ini').write_text('[Game]\nTitle=SDL3 smoke\n', encoding='utf-8')
        environment = dict(os.environ, SDL_AUDIO_DRIVER='dummy', LIBGL_ALWAYS_SOFTWARE='1')
        environment.pop('SDL_VIDEO_DRIVER', None)
        environment.pop('SDL_VIDEODRIVER', None)
        try:
            completed = subprocess.run([str(executable)], cwd=fixture, env=environment,
                                       capture_output=True, text=True, timeout=45)
        except subprocess.TimeoutExpired as error:
            # TimeoutExpired retains bytes even when run(text=True) was requested.
            stdout, stderr = [value.decode('utf-8', 'replace') if isinstance(value, bytes)
                              else value or '' for value in (error.stdout, error.stderr)]
            message = f'Engine smoke timed out after 45s\nstdout:\n{stdout}\nstderr:\n{stderr}'
            diagnostics = binary.parent / 'meson-logs'
            if diagnostics.is_dir():
                (diagnostics / 'engine-smoke-timeout.txt').write_text(message, encoding='utf-8')
            raise RuntimeError(message) from error
        report = fixture / 'result.txt'
        result = report.read_text(encoding='utf-8') if report.exists() else 'No smoke result'
        if completed.returncode or result != 'PASS':
            raise RuntimeError(f'Engine exit {completed.returncode}: {result}\n'
                               f'{completed.stdout}\n{completed.stderr}')
        print(f'PASS: engine image/IO/fonts/audio/FFI/resize/screenshot, pathCache={path_cache}')


def main():
    binary = Path(sys.argv[1]).resolve(strict=True)
    for path_cache in (False, True):
        run_case(binary, path_cache)


if __name__ == '__main__':
    main()
