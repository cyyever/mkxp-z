"""Exercise real script-archive filenames; use xvfb-run on headless Linux."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ARCHIVE_WRITER = r'''
require 'zlib'
source = File.binread(ARGV[1])
names = File.binread(ARGV[2]).split("\0")
scripts = names.each_with_index.map { |name, index| [index, name, Zlib::Deflate.deflate(source)] }
File.binwrite(ARGV[0], Marshal.dump(scripts))
'''


def run_case(binary, ruby, use_names):
    names = ['ordinary', 'long-script-' + 'x' * 1536, 'UTF8-' + '腳本名稱' * 200]
    with tempfile.TemporaryDirectory(prefix='mkxp-script-names-') as temporary:
        fixture = Path(temporary)
        executable = fixture / binary.name
        shutil.copy2(binary, executable)
        # Export __FILE__ from each actual archive entry without truncation.
        (fixture / 'source.rb').write_text(
            'File.open("actual-filenames.bin", "ab") { |f| f.write(__FILE__); f.write("\\0") }\n',
            encoding='utf-8')
        (fixture / 'names.bin').write_bytes('\0'.join(names).encode('utf-8'))
        subprocess.run([ruby, '-e', ARCHIVE_WRITER, str(fixture / 'Scripts.rxdata'),
                        str(fixture / 'source.rb'), str(fixture / 'names.bin')], check=True)
        configuration = {
            'rgssVersion': 1, 'gameFolder': str(fixture), 'useScriptNames': use_names,
            'enableSettings': False, 'winConsole': False, 'windowTitle': 'Script filename regression',
        }
        (fixture / 'mkxp.json').write_text(json.dumps(configuration), encoding='utf-8')
        (fixture / 'Game.ini').write_text(
            '[Game]\nTitle=Script filename regression\nScripts=Scripts.rxdata\n', encoding='utf-8')
        environment = dict(os.environ, SDL_AUDIO_DRIVER='dummy', LIBGL_ALWAYS_SOFTWARE='1')
        environment.pop('SDL_VIDEO_DRIVER', None)
        environment.pop('SDL_VIDEODRIVER', None)
        completed = subprocess.run([str(executable)], cwd=fixture, env=environment,
                                   capture_output=True, text=True, timeout=45)
        result = fixture / 'actual-filenames.bin'
        actual = result.read_bytes().split(b'\0')[:-1] if result.exists() else []
        expected = [(f'{index:03}:{name}' if use_names else f'Section{index:03}').encode('utf-8')
                    for index, name in enumerate(names)]
        if completed.returncode or actual != expected:
            raise RuntimeError(f'Archive filename regression, useScriptNames={use_names}, '
                               f'exit={completed.returncode}, lengths={[len(x) for x in actual]}\n'
                               f'{completed.stdout}\n{completed.stderr}')
        print(f'PASS: actual script-archive __FILE__ values, useScriptNames={use_names}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine', type=Path)
    parser.add_argument('--ruby', default='ruby', help='Ruby executable with the zlib standard library')
    options = parser.parse_args()
    binary = options.engine.resolve(strict=True)
    for use_names in (True, False):
        run_case(binary, options.ruby, use_names)


if __name__ == '__main__':
    main()
