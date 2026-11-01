"""Run a built engine in an isolated fixture; Linux CI uses xvfb-run."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import wave


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
            'RTP': [str(repository / 'assets')],
            'pathCache': path_cache,
            'enableSettings': False,
            'winConsole': False,
            'windowTitle': 'SDL3 migration smoke',
            'defScreenW': 640,
            'defScreenH': 480,
        }
        # Exercise the Ruby audio API through filesystem lookup, SDL_sound and SDL3.
        for name, channels, rate, sample_width in [('tone', 2, 48000, 2), ('tone-mono', 1, 22050, 1)]:
            with wave.open(str(fixture / (name + '.wav')), 'wb') as output:
                output.setnchannels(channels)
                output.setsampwidth(sample_width)
                output.setframerate(rate)
                silence = b'\x80' if sample_width == 1 else b'\x00\x00'
                output.writeframes(silence * channels * rate * 2)
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
            raise RuntimeError('Engine smoke timed out (possible graphics lock deadlock)') from error
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
