"""Strict process contract for the plate solver; stdout is JSONL with --events-jsonl."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import sys
import tempfile
import traceback

CONTRACT_VERSION = 1


def validate_input(data):
    """Validate every supported input before importing or allocating FEM data."""
    def object_keys(value, keys, name):
        if not isinstance(value, dict) or set(value) != set(keys):
            raise ValueError(f"{name} must contain exactly: {', '.join(keys)}")

    def number(value, name):
        if type(value) not in (int, float) or not math.isfinite(value):
            raise ValueError(f"{name} must be a finite number")
        return value

    def array(value, name, minimum=1):
        if not isinstance(value, list) or len(value) < minimum:
            raise ValueError(f"{name} requires at least {minimum} entries")
        for v in value:
            number(v, name)
        return value

    object_keys(data, ('nodes', 'slab', 'subgrade', 'loads'), 'input')
    object_keys(data['nodes'], ('x', 'y'), 'nodes')
    for axis in ('x', 'y'):
        values = array(data['nodes'][axis], f'nodes.{axis}', 2)
        if any(b <= a or not math.isfinite(b - a) for a, b in zip(values, values[1:])):
            raise ValueError(f'nodes.{axis} must be strictly increasing with finite spacing')
    dofs = len(data['nodes']['x']) * len(data['nodes']['y']) * 5
    if 8 * dofs * dofs > 1024**3:
        raise ValueError('Mesh exceeds the conservative 1 GiB dense-matrix allocation limit')
    object_keys(data['slab'], ('Emod', 'nu', 't'), 'slab')
    for key, value in data['slab'].items():
        number(value, f'slab.{key}')
    if data['slab']['Emod'] <= 0 or data['slab']['t'] <= 0 or not -1 < data['slab']['nu'] < 0.5:
        raise ValueError('Require Emod > 0, t > 0 and -1 < nu < 0.5')
    object_keys(data['subgrade'], ('Kx', 'Ky', 'Kz'), 'subgrade')
    for key, value in data['subgrade'].items():
        number(value, f'subgrade.{key}')
    keys = ('x1', 'x2', 'y1', 'y2', 'q')
    object_keys(data['loads'], keys, 'loads')
    arrays = [array(data['loads'][key], f'loads.{key}') for key in keys]
    if len({len(values) for values in arrays}) != 1:
        raise ValueError('All pressure-patch arrays must have equal lengths')
    x, y = data['nodes']['x'], data['nodes']['y']
    total = 0.0
    for x1, x2, y1, y2, q in zip(*arrays):
        if not (x[0] <= x1 < x2 <= x[-1] and y[0] <= y1 < y2 <= y[-1] and q >= 0):
            raise ValueError('Pressure patches require positive area, nonnegative q and bounds inside the plate')
        total += (x2 - x1) * (y2 - y1) * q
        if not math.isfinite(total):
            raise ValueError('Pressure-patch force is not finite')
    return data


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f'Duplicate JSON key: {key}')
        result[key] = value
    return result


def digest(path):
    checksum = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(block)
    return checksum.hexdigest()


def atomic_json(path, data):
    temporary = path.with_suffix(path.suffix + '.tmp')
    with temporary.open('w', encoding='utf-8') as stream:
        json.dump(data, stream, indent=2, allow_nan=False)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--check-backend', action='store_true')
    parser.add_argument('--events-jsonl', action='store_true')
    parser.add_argument('--diagnostics', choices=('none', 'summary', 'full'), default='summary')
    parser.add_argument('--cancel-file', type=Path)
    parser.add_argument('--linear-solver', choices=('dense', 'sparse'), default='dense')
    args = parser.parse_args(argv)
    if args.check_backend:
        try:
            os.environ['MPLBACKEND'] = 'Agg'
            import numpy
            import scipy
            import matplotlib
            from plate_analysis import run_analysis
            print(f'Backend imports OK: Python {sys.version.split()[0]}, NumPy {numpy.__version__}, '
                  f'SciPy {scipy.__version__}, Matplotlib {matplotlib.__version__}')
            return 0
        except ImportError as error:
            print(str(error), file=sys.stderr)
            return 3
    if args.input is None or args.output_dir is None:
        parser.error('--input and --output-dir are required for analysis')
    event_stream = sys.stdout

    def emit(event):
        if args.events_jsonl:
            print(json.dumps({'contractVersion': CONTRACT_VERSION, **event}, allow_nan=False),
                  file=event_stream, flush=True)
        elif event['type'] in ('error', 'cancelled', 'completed'):
            print(event.get('message', event['type']), file=sys.stderr, flush=True)

    cancelled = False

    def request_cancel(signum, frame):
        nonlocal cancelled
        cancelled = True

    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, request_cancel)

    def cancellation_requested():
        return cancelled or (args.cancel_file is not None and args.cancel_file.exists())

    stage = None
    try:
        emit({'type': 'phase', 'phase': 'validation', 'progressKnown': False})
        if not args.input.is_absolute() or not args.output_dir.is_absolute():
            raise ValueError('--input and --output-dir must be absolute paths')
        try:
            raw = args.input.read_bytes()
            data = validate_input(json.loads(raw.decode('utf-8-sig'), object_pairs_hook=unique_object))
        except (OSError, UnicodeError, OverflowError) as error:
            raise ValueError(f'Cannot read valid input: {error}') from error
        if cancellation_requested():
            emit({'type': 'cancelled', 'message': 'Cancelled by user.'})
            return 130
        if args.output_dir.exists():
            raise OSError('Output directory already exists; choose a fresh run directory')
        args.output_dir.parent.mkdir(parents=True, exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix=args.output_dir.name + '.partial-', dir=args.output_dir.parent))
        (stage / 'input.json').write_bytes(raw)
        # Dependency imports happen inside the error boundary and never at CLI import time.
        os.environ['MPLBACKEND'] = 'Agg'
        from plate_analysis import AnalysisCancelled, run_analysis
        import numpy as np
        import scipy
        import matplotlib
        if all(value == 0 for value in data['subgrade'].values()):
            emit({'type': 'error', 'code': 'UNSUPPORTED_CONFIGURATION',
                  'message': 'Zero foundation without supports is unsupported. This backend has no support constraints.'})
            return 4
        try:
            summary = run_analysis(stage / 'input.json', stage, emit, cancellation_requested, args.diagnostics,
                                   linear_solver=args.linear_solver)
        except AnalysisCancelled:
            emit({'type': 'cancelled', 'message': 'Cancelled by user.'})
            return 130
        except (np.linalg.LinAlgError, ValueError, FloatingPointError) as error:
            emit({'type': 'error', 'code': 'NUMERICAL_FAILURE', 'message': str(error)})
            return 4
        if cancellation_requested():
            emit({'type': 'cancelled', 'message': 'Cancelled by user.'})
            return 130
        manifest = {
            'contractVersion': CONTRACT_VERSION, 'status': 'Completed', 'result': 'results.vtk',
            'inputSha256': hashlib.sha256(raw).hexdigest(), 'resultSha256': digest(stage / 'results.vtk'),
            'unitSystem': 'mm-N-MPa', 'summary': summary,
            'runtime': {'python': sys.version.split()[0], 'numpy': np.__version__,
                        'scipy': scipy.__version__, 'matplotlib': matplotlib.__version__},
            'sources': {name: digest(Path(__file__).parent / name) for name in
                        ('plate_solver.py', 'plate_analysis.py', 'fem_local_Plate20_DOF5_07_09_2025.py')},
        }
        atomic_json(stage / 'run-result.json', manifest)
        if cancellation_requested():
            emit({'type': 'cancelled', 'message': 'Cancelled by user.'})
            return 130
        # A final directory and manifest appear together, only after successful export.
        os.rename(stage, args.output_dir)
        emit({'type': 'completed', 'manifest': 'run-result.json'})
        return 0
    except ValueError as error:
        emit({'type': 'error', 'code': 'INVALID_INPUT', 'message': str(error)})
        return 2
    except ImportError as error:
        emit({'type': 'error', 'code': 'MISSING_DEPENDENCY', 'message': str(error)})
        return 3
    except OSError as error:
        emit({'type': 'error', 'code': 'EXPORT_FAILURE', 'message': str(error)})
        return 5
    except Exception as error:
        emit({'type': 'error', 'code': 'INTERNAL_ERROR', 'message': str(error)})
        traceback.print_exc(file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
