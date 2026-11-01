"""Strict offline FXC validation of every SDL_GPU HLSL entry point."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess

ENTRIES = {
    'vertexMain': 'vs_5_1',
    'tilemapVertexMain': 'vs_5_1',
    **dict.fromkeys(('simpleMain', 'flatColorMain', 'vertexColorMain',
                    'vertexAlphaMain', 'uniformAlphaMain', 'planeMain',
                    'transitionMain', 'simpleTransitionMain', 'bitmapBlendMain',
                    'hueMain', 'spriteMain', 'tilemapMain', 'flashMapMain', 'grayMain',
                    'blurMain', 'invertMain', 'compressAlphaMain', 'subtractMain',
                    'bicubicMain', 'lanczosMain', 'shadowHorizontalMain', 'shadowVerticalMain'), 'ps_5_1'),
}


def find_compiler(explicit):
    if explicit:
        return str(Path(explicit).resolve(strict=True))
    found = shutil.which('fxc')
    if found:
        return found
    sdk = os.environ.get('WindowsSdkDir')
    candidates = []
    if sdk:
        candidates.extend(Path(sdk).glob('bin/*/x64/fxc.exe'))
    program_files = os.environ.get('ProgramFiles(x86)')
    if program_files:
        candidates.extend((Path(program_files) / 'Windows Kits/10/bin').glob('*/x64/fxc.exe'))
    if candidates:
        return str(max(candidates, key=lambda p: tuple(int(v) for v in p.parts[-3].split('.'))))
    raise RuntimeError('FXC not found: install the Windows SDK or pass --compiler PATH')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--extra-shaders', action='store_true', help='Also compile optional GPLv3+ xBRZ')
    parser.add_argument('--compiler')
    parser.add_argument('--find-compiler', action='store_true')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2] / 'shader/gpu/quad.hlsl')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--verify', nargs='*', type=Path)
    args = parser.parse_args()
    entries = dict(ENTRIES)
    if args.extra_shaders:
        entries['xbrzMain'] = 'ps_5_1'
    if args.list:
        for entry, profile in entries.items():
            print(f'{entry}:{profile}')
        return
    # Refuse silent coverage gaps when a new entry point is added.
    declared = set(re.findall(r'^\w+\s+(\w+Main)\s*\(', args.source.read_text(encoding='utf-8'), re.MULTILINE))
    if declared != ENTRIES.keys():
        raise RuntimeError(f'HLSL entry manifest differs: missing={declared - ENTRIES.keys()}, stale={ENTRIES.keys() - declared}')
    if args.extra_shaders:
        extra = args.source.with_name('xbrz.hlsl')
        declared_extra = set(re.findall(r'^\w+\s+(\w+Main)\s*\(', extra.read_text(encoding='utf-8'), re.MULTILINE))
        if declared_extra != {'xbrzMain'}:
            raise RuntimeError(f'xBRZ entry manifest differs: {declared_extra}')
    if args.verify is not None:
        for binary in args.verify:
            if binary.read_bytes()[:4] != b'DXBC':
                raise RuntimeError(f'Invalid DXBC output: {binary}')
        print(f'PASS: {len(args.verify)} strict FXC shader entry points')
        return
    compiler = find_compiler(args.compiler)
    if args.find_compiler:
        print(compiler)
        return
    if args.output_dir is None:
        parser.error('--output-dir is required for compilation')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for entry, profile in entries.items():
        source = args.source.with_name('xbrz.hlsl') if entry == 'xbrzMain' else args.source
        output = args.output_dir / f'{entry}.dxbc'
        subprocess.run([compiler, '/nologo', '/Ges', '/WX', '/T', profile,
                        '/E', entry, '/Fo', str(output), str(source)], check=True)
        if output.read_bytes()[:4] != b'DXBC':
            raise RuntimeError(f'FXC did not produce DXBC: {output}')
    print(f'PASS: {len(entries)} strict FXC shader entry points')


if __name__ == '__main__':
    main()
