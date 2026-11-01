"""Exercise logical and physical snapshot storage with a built native engine."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile


def main():
    binary = Path(sys.argv[1]).resolve()
    source = Path(__file__).resolve()
    with tempfile.TemporaryDirectory(prefix='mkxp-hires-snapshot-') as directory:
        fixture = Path(directory)
        executable = fixture / binary.name
        shutil.copy2(binary, executable)
        config = {
            'rgssVersion': 1, 'gameFolder': str(fixture),
            'customScript': str(source.with_suffix('.rb')),
            'RTP': [str(source.parents[2] / 'assets')],
            'enableSettings': False, 'winConsole': False,
            'defScreenW': 640, 'defScreenH': 480, 'enableHires': True,
            'framebufferScalingFactor': 3, 'textureScalingFactor': 3,
            'pathCache': False,
        }
        (fixture / 'mkxp.json').write_text(json.dumps(config), encoding='utf-8')
        (fixture / 'Game.ini').write_text('[Game]\nTitle=HiRes snapshot regression\n', encoding='utf-8')
        result = subprocess.run([str(executable)], cwd=fixture,
                                env=dict(os.environ, SDL_AUDIO_DRIVER='dummy'),
                                capture_output=True, text=True, timeout=45)
        report = fixture / 'result.txt'
        status = report.read_text(encoding='utf-8') if report.exists() else 'MISSING RESULT'
        if result.returncode or status != 'PASS':
            raise RuntimeError(f'{status}\nexit={result.returncode}\n{result.stdout}\n{result.stderr}')
        print('PASS: 3x logical/hires RGBA, PNG dimensions and screenshot reload')


if __name__ == '__main__':
    main()
