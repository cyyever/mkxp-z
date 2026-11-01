"""Compare separately built native GL and SDL_GPU primitive benchmark executables."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import platform
import statistics
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gl-baseline', type=Path, required=True)
    parser.add_argument('--gpu', type=Path, required=True)
    parser.add_argument('--shaders', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--samples', type=int, default=30)
    parser.add_argument('--iterations', type=int, default=10)
    parser.add_argument('--rounds', type=int, default=3)
    parser.add_argument('--notes', default='', help='Build flags and host workload conditions')
    options = parser.parse_args()
    if options.samples < 3 or options.iterations < 1 or options.rounds < 1:
        parser.error('At least 3 samples, 1 iteration and 1 round are required')
    binaries = {'OpenGL': options.gl_baseline.resolve(strict=True), 'SDL_GPU': options.gpu.resolve(strict=True)}
    runs = []
    for round_index in range(options.rounds):
        order = ['OpenGL', 'SDL_GPU'] if round_index % 2 == 0 else ['SDL_GPU', 'OpenGL']
        for backend in order:
            command = [str(binaries[backend]), str(options.shaders.resolve()),
                       str(options.samples), str(options.iterations)]
            completed = subprocess.run(command, capture_output=True, text=True, timeout=300)
            if completed.returncode:
                raise RuntimeError(f'{backend} failed: {completed.stdout}\n{completed.stderr}')
            rows = [json.loads(line) for line in completed.stdout.splitlines() if line.strip()]
            expected_count = 15 if backend == 'SDL_GPU' else 12
            if len(rows) != expected_count or any(row['backend'] != backend for row in rows):
                raise RuntimeError(f'{backend} returned an incomplete workload set')
            runs.append({'round': round_index, 'backend': backend, 'measurements': rows})
            print(f'Completed round {round_index + 1}: {backend}', flush=True)
    grouped = {}
    for run in runs:
        for row in run['measurements']:
            key = (row['operation'], row['width'], row['height'])
            backends = grouped.setdefault(key, {})
            item = backends.setdefault(run['backend'], {'checksums': set(), 'samples': []})
            item['checksums'].add(row['pixel_checksum'])
            item['samples'].extend(row['samples_us'])
    # Both GPU fill paths use the identical old GL scissored-clear workload.
    for (operation, width, height), backends in grouped.items():
        if operation == 'framefill64':
            backends['OpenGL'] = grouped[('fill64', width, height)]['OpenGL']
    comparisons = []
    for (operation, width, height), backends in grouped.items():
        if backends['OpenGL']['checksums'] != backends['SDL_GPU']['checksums'] or len(backends['OpenGL']['checksums']) != 1:
            raise RuntimeError(f'Pixel mismatch for {operation} {width}x{height}')
        row = {'operation': operation, 'width': width, 'height': height, 'pixel_equal': True}
        for backend, item in backends.items():
            samples = sorted(item['samples'])
            row[backend] = {'median_us': statistics.median(samples),
                            'p95_us': samples[math.ceil(.95 * len(samples)) - 1], 'sample_count': len(samples)}
        row['gpu_over_gl_median'] = row['SDL_GPU']['median_us'] / row['OpenGL']['median_us']
        comparisons.append(row)
    report = {
        'scope': 'Native primitive CPU+GPU fenced latency; not engine FPS or game performance',
        'timing': 'Five warmup samples per workload/run; one completion wait per sample; readback outside timing',
        'host': platform.platform(), 'notes': options.notes,
        'binaries': {backend: {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                     for backend, path in binaries.items()},
        'comparisons': comparisons, 'runs': runs,
    }
    options.output.parent.mkdir(parents=True, exist_ok=True)
    options.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    for row in comparisons:
        print(f"{row['operation']:8} {row['width']}x{row['height']}: "
              f"GL {row['OpenGL']['median_us']:.2f} us, GPU {row['SDL_GPU']['median_us']:.2f} us, "
              f"GPU/GL {row['gpu_over_gl_median']:.2f}")


if __name__ == '__main__':
    main()
