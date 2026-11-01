"""Offline SPIR-V -> MSL using SDL_shadercross and reflected export metadata."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--reflector', required=True)
    parser.add_argument('--entry', required=True)
    parser.add_argument('--stage', choices=('vertex', 'fragment'), required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--metadata', type=Path, required=True)
    args = parser.parse_args()
    subprocess.run([args.compiler, str(args.source), '-s', 'SPIRV', '-d', 'MSL',
                    '-t', args.stage, '-e', args.entry, '--msl-version', '2.0.0',
                    '-o', str(args.output)], check=True)
    subprocess.run([args.reflector, str(args.source), args.entry, args.stage,
                    str(args.metadata)], check=True)
    exported = json.loads(args.metadata.read_text(encoding='utf-8'))['msl'][args.entry]
    # The name comes from SPIRV-Cross reflection, not a guessed HLSL-name mapping.
    source = args.output.read_text(encoding='utf-8')
    if not re.search(r'\b' + re.escape(exported) + r'\s*\(', source):
        raise RuntimeError(f'Reflected Metal entrypoint is absent in {args.output}')


if __name__ == '__main__':
    main()
