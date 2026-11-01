"""Run a built engine in an isolated fixture; Linux CI uses xvfb-run."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve(strict=True)
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
            'pathCache': True,
            'enableSettings': False,
            'winConsole': False,
            'windowTitle': 'SDL3 migration smoke',
            'defScreenW': 640,
            'defScreenH': 480,
        }
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
        print('PASS: SDL3 image IO, fonts/GC, settings, native FFI, resize and screenshot')


if __name__ == '__main__':
    main()
