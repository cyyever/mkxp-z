"""Generate the embedded shader registry from the offline compiler manifest.

No shader compiler or translation library is linked into the engine. Metal
formats require an explicit exported-entrypoint map from their offline producer.
"""
import argparse
import importlib.util
import json
from pathlib import Path

FORMATS = {
    'dxbc': 'SDL_GPU_SHADERFORMAT_DXBC',
    'dxil': 'SDL_GPU_SHADERFORMAT_DXIL',
    'spirv': 'SDL_GPU_SHADERFORMAT_SPIRV',
    'msl': 'SDL_GPU_SHADERFORMAT_MSL',
    'metallib': 'SDL_GPU_SHADERFORMAT_METALLIB',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--formats', nargs='+', choices=FORMATS, required=True)
    parser.add_argument('--extra-shaders', action='store_true')
    parser.add_argument('--entrypoints', type=Path, help='JSON format -> semantic -> exported entrypoint')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if len(set(args.formats)) != len(args.formats):
        parser.error('Duplicate shader formats')
    manifest = Path(__file__).with_name('lint-hlsl.py')
    spec = importlib.util.spec_from_file_location('hlsl_manifest', manifest)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    entries = list(module.ENTRIES)
    if args.extra_shaders:
        entries.append('xbrzMain')
    exports = json.loads(args.entrypoints.read_text(encoding='utf-8')) if args.entrypoints else {}
    lines = ['// Generated from the offline shader manifest. Do not edit.',
             '#pragma once', '#include "gpu-shader.h"', '#include <string_view>']
    for entry in entries:
        for format in args.formats:
            lines.append(f'#include "{entry}.{format}.xxd"')
    lines += ['namespace gpu::embedded {',
              'struct Entry { std::string_view semantic; std::span<const ShaderCode> variants; };',
              'inline constexpr SDL_GPUShaderFormat formats = ' + ' | '.join(FORMATS[f] for f in args.formats) + ';']
    for entry in entries:
        lines.append(f'inline const ShaderCode {entry}_variants[] = {{')
        for format in args.formats:
            exported = exports.get(format, {}).get(entry)
            if exported is None:
                if format in ('msl', 'metallib'):
                    parser.error(f'Metal producer must supply the exported entrypoint for {entry}')
                exported = entry
            if not isinstance(exported, str) or not exported or '\0' in exported:
                parser.error(f'Invalid exported entrypoint for {format}/{entry}')
            symbol = f'mkxp_gpu_{entry}_{format}'
            lines.append(f'    {{{{{symbol}, {symbol}_len}}, {FORMATS[format]}, {json.dumps(exported)}}},')
        lines.append('};')
    lines.append('inline const Entry shaders[] = {')
    for entry in entries:
        lines.append(f'    {{"{entry}", {entry}_variants}},')
    lines += ['};', '}']
    args.output.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__':
    main()
