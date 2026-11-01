"""Compile HLSL to SDL_GPU-compatible Vulkan SPIR-V with offline DXC.

DXC performs SPIRV-Tools validation unless explicitly disabled; do not pass -Vd.
The structural check below additionally enforces SDL's descriptor-set contract.
"""
import argparse
from pathlib import Path
import struct
import subprocess


def validate(path, vertex):
    data = path.read_bytes()
    if len(data) % 4 or len(data) < 20:
        raise RuntimeError(f'Invalid SPIR-V length: {path}')
    words = struct.unpack(f'<{len(data)//4}I', data)
    if words[0] != 0x07230203:
        raise RuntimeError(f'Invalid SPIR-V magic: {path}')
    decorations = {}
    names = {}
    offsets = {}
    matrix_strides = []
    types = {}
    variables = {}
    offset = 5
    while offset < len(words):
        size, opcode = words[offset] >> 16, words[offset] & 0xffff
        if not size or offset + size > len(words):
            raise RuntimeError(f'Invalid instruction: {path}')
        args = words[offset+1:offset+size]
        if opcode == 5:
            names[args[0]] = struct.pack(f'<{len(args)-1}I', *args[1:]).split(b'\0', 1)[0].decode()
        elif opcode == 72 and len(args) >= 4:
            if args[2] == 35:  # Offset
                offsets.setdefault(args[0], {})[args[1]] = args[3]
            elif args[2] == 7:  # MatrixStride
                matrix_strides.append(args[3])
        elif opcode == 71 and len(args) >= 3 and args[1] in (33, 34):
            decorations.setdefault(args[0], {})[args[1]] = args[2]
        elif opcode in (25, 26, 27, 32):
            types[args[0]] = (opcode, args[1:])
        elif opcode == 59:
            variables[args[1]] = (args[0], args[2])
        offset += size
    expected = {'VertexParameters': 3, 'FragmentParameters': 5,
                'EffectParameters': 9, 'TilemapParameters': 3}
    for identity, members in offsets.items():
        block = names.get(identity, '').removeprefix('type.')
        if block not in expected:
            raise RuntimeError(f'Unexpected uniform block in {path}: {block}')
        wanted = ([0, 64, 128] if block == 'VertexParameters'
                  else list(range(0, expected[block] * 16, 16)))
        if members != dict(enumerate(wanted)):
            raise RuntimeError(f'Uniform ABI mismatch in {path}: {block} {members}')
    if any(stride != 16 for stride in matrix_strides):
        raise RuntimeError(f'Uniform matrix stride mismatch in {path}')
    for identity, deco in decorations.items():
        if 34 not in deco:
            continue
        pointer, storage = variables[identity]
        pointee = types[pointer][1][1]
        if storage == 0:  # UniformConstant must be a combined sampled image.
            if types[pointee][0] != 27 or deco[34] != (0 if vertex else 2):
                raise RuntimeError(f'Non-SDL sampled-image layout in {path}: {deco}')
            if deco.get(33, 99) > 2:
                raise RuntimeError(f'Unexpected sampler binding in {path}: {deco}')
        elif storage == 2:
            if deco[34] != (1 if vertex else 3) or deco.get(33) not in (0, 1):
                raise RuntimeError(f'Non-SDL uniform layout in {path}: {deco}')
        else:
            raise RuntimeError(f'Unexpected descriptor storage in {path}: {storage}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--entry', required=True)
    parser.add_argument('--profile', choices=('vs_5_1', 'ps_5_1'), required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    vertex = args.profile.startswith('vs')
    subprocess.run([args.compiler, '-spirv', '-fspv-target-env=vulkan1.0',
                    '-Ges', '-WX', '-T', 'vs_6_0' if vertex else 'ps_6_0',
                    '-E', args.entry, '-Fo', str(args.output), str(args.source)], check=True)
    validate(args.output, vertex)


if __name__ == '__main__':
    main()

