"""Reproducible, bounded dense/sparse profiling in separate child processes."""
import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def worker(count, solver, destination):
    sys.path.insert(0, str(ROOT / 'solver/reference'))
    sys.path.insert(0, str(ROOT / 'tests'))
    import numpy as np
    from expected_solver_implementation import run_analysis
    from numerical_cases import cases
    data = cases()['symmetric']
    data['nodes'] = dict(x=np.linspace(0, 1000, count).tolist(), y=np.linspace(0, 1000, count).tolist())
    path = destination / 'input.json'
    path.write_text(json.dumps(data), encoding='utf-8')
    summary = run_analysis(path, destination, diagnostics='none', linear_solver=solver)
    (destination / 'measurements.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--worker', action='store_true')
    parser.add_argument('--grid', type=int)
    parser.add_argument('--solver', choices=('dense', 'sparse'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--grids', type=int, nargs='+', default=[5, 9, 17, 25, 33])
    args = parser.parse_args()
    if args.worker:
        worker(args.grid, args.solver, args.output)
        return
    import numpy
    import scipy
    import psutil
    report = {
        'purpose': 'Software consistency and local performance measurement, not independent engineering acceptance',
        'platform': platform.platform(), 'processor': platform.processor(),
        'logicalCpus': psutil.cpu_count(), 'physicalMemoryBytes': psutil.virtual_memory().total,
        'python': platform.python_version(), 'numpy': numpy.__version__, 'scipy': scipy.__version__,
        'threadEnvironment': {k: os.environ.get(k) for k in ('OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS')},
        'sampling': 'One cold child per row; 20 ms polling; Windows peak working set when available, otherwise sampled RSS',
        'limits': {'timeoutSeconds': 90, 'peakMemoryBytes': 1024**3}, 'runs': [],
    }
    for count in args.grids:
        if count < 3 or count > 33:
            raise ValueError('Qualification grids must have 3..33 nodes per axis')
        for solver in ('dense', 'sparse'):
            with tempfile.TemporaryDirectory(prefix='plate benchmark ') as temporary:
                folder = Path(temporary)
                started = time.perf_counter()
                child = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), '--worker', '--grid', str(count),
                                          '--solver', solver, '--output', str(folder)],
                                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
                                         env={**os.environ, 'PYTHONUTF8': '1', 'MPLBACKEND': 'Agg'})
                process = psutil.Process(child.pid)
                peak = 0
                failure = None
                while child.poll() is None:
                    try:
                        info = process.memory_info()
                        peak = max(peak, getattr(info, 'peak_wset', info.rss))
                    except psutil.NoSuchProcess:
                        break
                    if peak > 1024**3 or time.perf_counter() - started > 90:
                        failure = 'Memory/time qualification limit exceeded'
                        child.kill()
                        break
                    time.sleep(0.02)
                _, error = child.communicate(timeout=5)
                elapsed = time.perf_counter() - started
                row = {'grid': count, 'nodes': count**2, 'dofs': count**2 * 5, 'solver': solver,
                       'wallSeconds': elapsed, 'peakWorkingSetBytes': peak, 'exitCode': child.returncode}
                if failure or child.returncode:
                    row['failure'] = failure or error.decode('utf-8', errors='replace')
                else:
                    row['summary'] = json.loads((folder / 'measurements.json').read_text())
                report['runs'].append(row)
                print(f"{count}x{count} {solver}: {elapsed:.2f}s, {peak / 1024**2:.1f} MiB, exit {child.returncode}", flush=True)
                args.output.parent.mkdir(parents=True, exist_ok=True)
                args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    if any('failure' in row for row in report['runs']):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
