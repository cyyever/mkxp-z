"""Run the self-checking engine regression scripts against a built engine.

Usage: regressions.py ENGINE_BINARY

Each tests/sdl3/*.rb script runs through hires-snapshot.py in the mode it is
written for. The KGL2 suite (tests/kgl2) reports through System.puts instead of
result.txt, so a wrapper script logs that output and any non-zero exit.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
HARNESS = HERE / 'hires-snapshot.py'

# (script, run with HiRes, run without HiRes)
SCRIPTS = [
    ('hires-readback.rb', True, False),
    ('hires-snapshot.rb', True, False),
    ('hires-tilemap.rb', True, False),
    ('tilemap-batches.rb', True, True),
    ('bitmap-copy.rb', False, True),
    ('mega-surface-flip.rb', False, True),
    ('pixel-access.rb', True, True),
]

# KGL2 tests that cannot pass in HiRes yet. Each entry needs a reason.
KGL2_HIRES_SKIP = set()

KGL2_WRAPPER = '''LOG = {log!r}
module System
  def self.puts(*args)
    File.write(LOG, args.join + "\\n", mode: 'a')
  end
end
begin
  load {test!r}
rescue SystemExit => e
  File.write(LOG, "EXIT #{{e.status}}\\n", mode: 'a')
rescue Exception => e
  # Log instead of letting the engine show a blocking error dialog.
  File.write(LOG, "ERROR #{{e.class}}: #{{e.message}}\\n", mode: 'a')
end
File.write('result.txt', 'PASS')
'''


def run_harness(binary, script, hires):
    command = [sys.executable, str(HARNESS), str(binary), str(script)]
    if not hires:
        command.append('--no-hires')
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(f'{script.name} (hires={hires}) failed:\n{result.stdout}{result.stderr}')


def run_kgl2(binary, test, scratch, hires=False):
    tag = test.stem + ('-hires' if hires else '')
    log = scratch / (tag + '.log')
    wrapper = scratch / (tag + '-wrapper.rb')
    wrapper.write_text(KGL2_WRAPPER.format(log=log.as_posix(), test=test.as_posix()), encoding='utf-8')
    run_harness(binary, wrapper, hires=hires)
    output = log.read_text(encoding='utf-8') if log.exists() else ''
    lines = output.splitlines()
    failed = [line for line in lines if 'failed' in line or 'should be' in line or
              line.startswith('ERROR ') or (line.startswith('EXIT ') and line != 'EXIT 0')]
    passed = [line for line in lines if line.endswith(' ok')]
    if failed or not passed:
        raise RuntimeError(f'kgl2/{test.name}: ' + ('; '.join(failed) or 'no passing checks') +
                           f'\n{output}')
    return len(passed)


def main():
    binary = Path(sys.argv[1]).resolve()
    failures = []
    for name, hires, lores in SCRIPTS:
        for mode in [m for m, enabled in ((True, hires), (False, lores)) if enabled]:
            try:
                run_harness(binary, HERE / name, mode)
                print(f'PASS: {name} (hires={mode})')
            except RuntimeError as error:
                failures.append(str(error))
    with tempfile.TemporaryDirectory(prefix='mkxp-kgl2-') as directory:
        for test in sorted((ROOT / 'tests' / 'kgl2').glob('*.rb')):
            for hires in (False, True):
                if hires and test.name in KGL2_HIRES_SKIP:
                    continue
                try:
                    count = run_kgl2(binary, test, Path(directory), hires)
                    print(f'PASS: kgl2/{test.name} (hires={hires}, {count} checks)')
                except RuntimeError as error:
                    failures.append(str(error))
    if failures:
        print('\n\n'.join(failures), file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
