"""Production CLI acceptance checks, independent of the Qt process adapter."""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
BACKEND = ROOT / 'solver' / 'reference'
sys.path.insert(0, str(BACKEND))
import plate_solver

FIXTURE = ROOT / 'tests' / 'fixtures' / 'tiny_3x3'
INPUT = json.loads((FIXTURE / 'input.json').read_text())


class ContractTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='plate contract café ')
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)

    def run_cli(self, data=INPUT, diagnostics='summary', output=None, extra=(), flags=()):
        path = self.folder / 'input café.json'
        path.write_text(json.dumps(data), encoding='utf-8')
        output = output or self.folder / 'output café'
        result = subprocess.run(
            [sys.executable, *flags, str(BACKEND / 'plate_solver.py'), '--input', str(path),
             '--output-dir', str(output), '--events-jsonl', '--diagnostics', diagnostics, *extra],
            cwd=self.folder, capture_output=True, text=True, encoding='utf-8', timeout=40,
            env={**os.environ, 'PYTHONUTF8': '1', 'MPLBACKEND': 'Agg'})
        events = [json.loads(line) for line in result.stdout.splitlines()]
        return result, events, output

    def test_strict_validation(self):
        mutations = [
            lambda d: d.pop('nodes'),
            lambda d: d.update(legacy=True),
            lambda d: d['nodes'].update(x=[0, 0]),
            lambda d: d['nodes'].update(x=[0, float('nan')]),
            lambda d: d['nodes'].update(y=[True, 1000]),
            lambda d: d['nodes'].update(x=[0]),
            lambda d: d['slab'].update(Emod='30000'),
            lambda d: d['slab'].update(nu=0.5),
            lambda d: d['slab'].update(t=0),
            lambda d: d['subgrade'].update(Kz=float('inf')),
            lambda d: d['loads'].update(x1=[-1]),
            lambda d: d['loads'].update(x2=[250]),
            lambda d: d['loads'].update(q=[-1]),
            lambda d: d['loads'].update(q=[]),
            lambda d: d['loads'].update(q=[1, 2]),
            lambda d: d['loads'].update(q=[1e308]),
            lambda d: d['nodes'].update(x=list(range(1000)), y=list(range(1000))),
        ]
        for index, mutate in enumerate(mutations):
            with self.subTest(index=index):
                data = copy.deepcopy(INPUT)
                mutate(data)
                with self.assertRaises((ValueError, OverflowError)):
                    plate_solver.validate_input(data)
        with self.assertRaises(ValueError):
            json.loads('{"nodes":{},"nodes":{}}', object_pairs_hook=plate_solver.unique_object)

    def test_invalid_input_does_not_fall_back(self):
        # Legacy-looking files must never rescue malformed unified input.
        for name in ('mesh_inputDOF507-09-2025.json', 'inputstoDOF5_07_09_2025.json', 'load_inputDOF507-09-2025.json'):
            (self.folder / name).write_text('{}')
        result, events, output = self.run_cli({})
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(events[-1]['code'], 'INVALID_INPUT')
        self.assertFalse(output.exists())
        self.assertFalse((self.folder / 'output_python_square').exists())

    def test_reference_and_atomic_manifest(self):
        result, events, output = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(events[-1]['type'], 'completed')
        manifest = json.loads((output / 'run-result.json').read_text())
        self.assertEqual(manifest['status'], 'Completed')
        self.assertEqual(manifest['summary']['nodeCount'], 9)
        self.assertEqual(len(manifest['summary']['fields']), 14)
        self.assertEqual(manifest['resultSha256'], plate_solver.digest(output / 'results.vtk'))
        self.assertEqual(manifest['inputSha256'], plate_solver.digest(self.folder / 'input café.json'))
        # Exact serialized equivalence for the existing baseline, in addition to
        # the per-field tolerance comparison exercised by the Qt integration test.
        self.assertEqual((output / 'results.vtk').read_text(), (FIXTURE / 'expected_output/results.vtk').read_text())
        self.assertFalse(list(self.folder.glob('*.partial-*')))
        self.assertFalse(list(output.glob('*.tmp')))
        self.assertFalse(list(output.glob('*.png')))
        self.assertFalse((output / 'results.txt').exists())
        self.assertTrue((output / 'summary_results.txt').exists())
        self.assertIn('numpy', manifest['runtime'])

    def test_missing_dependencies_have_distinct_exit(self):
        result, events, output = self.run_cli(flags=('-S',))
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertEqual(events[-1]['code'], 'MISSING_DEPENDENCY')
        self.assertFalse(output.exists())

    def test_failure_then_restart(self):
        data = copy.deepcopy(INPUT)
        data['subgrade'] = dict(Kx=0, Ky=0, Kz=0)
        result, events, output = self.run_cli(data)
        self.assertEqual(result.returncode, 4, result.stderr + result.stdout)
        self.assertEqual(events[-1]['code'], 'UNSUPPORTED_CONFIGURATION')
        self.assertFalse(output.exists())
        result, events, output = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((output / 'run-result.json').exists())

    def test_existing_output_is_preserved(self):
        output = self.folder / 'output'
        output.mkdir()
        (output / 'results.vtk').write_text('previous output')
        result, events, _ = self.run_cli(output=output)
        self.assertEqual(result.returncode, 5)
        self.assertEqual(events[-1]['code'], 'EXPORT_FAILURE')
        self.assertEqual((output / 'results.vtk').read_text(), 'previous output')

    def test_cooperative_cancel_before_import(self):
        marker = self.folder / 'cancel.request'
        marker.write_text('cancel')
        result, events, output = self.run_cli(extra=('--cancel-file', str(marker)))
        self.assertEqual(result.returncode, 130)
        self.assertEqual(events[-1]['type'], 'cancelled')
        self.assertFalse(output.exists())

    def test_import_has_no_output_side_effect(self):
        result = subprocess.run([sys.executable, '-B', '-c',
            'import sys; sys.path.insert(0, sys.argv[1]); import expected_solver_implementation', str(BACKEND)],
            cwd=self.folder, capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(list(self.folder.iterdir()), [])

    def test_cancellation_at_numerical_boundaries(self):
        from expected_solver_implementation import AnalysisCancelled, run_analysis
        for target in ('assembly', 'loads', 'solve', 'postprocessing', 'export'):
            with self.subTest(phase=target):
                cancelled = False
                def event(value):
                    nonlocal cancelled
                    if value['phase'] == target:
                        cancelled = True
                output = self.folder / target
                with self.assertRaises(AnalysisCancelled):
                    run_analysis(FIXTURE / 'input.json', output, event, lambda: cancelled)
                self.assertFalse((output / 'results.vtk').exists())

    def test_export_failure_never_publishes(self):
        from expected_solver_implementation import run_analysis
        output = self.folder / 'failed-export'
        def fail(mesh, path, fields, vectors):
            Path(path).write_text('partial VTK')
            raise OSError('simulated full disk')
        with patch('expected_solver_implementation.write_legacy_vtk_results', side_effect=fail):
            with self.assertRaises(OSError):
                run_analysis(FIXTURE / 'input.json', output)
        self.assertFalse((output / 'results.vtk').exists())

    def test_numerical_failure_exit_and_nonfinite_arrays(self):
        import contextlib
        import io
        import numpy as np
        import expected_solver_implementation as reference
        for scenario in ('singular', 'nan'):
            output = self.folder / scenario
            stream = io.StringIO()
            def solve(matrix, rhs):
                if scenario == 'singular':
                    raise np.linalg.LinAlgError('Singular matrix')
                return np.full(rhs.shape, np.nan)
            with patch.object(reference.la, 'solve', side_effect=solve), contextlib.redirect_stdout(stream):
                code = plate_solver.main(['--input', str(FIXTURE / 'input.json'), '--output-dir', str(output), '--events-jsonl'])
            self.assertEqual(code, 4)
            self.assertEqual(json.loads(stream.getvalue().splitlines()[-1])['code'], 'NUMERICAL_FAILURE')
            self.assertFalse(output.exists())

    def test_optional_diagnostics(self):
        for mode in ('none', 'full'):
            result, events, output = self.run_cli(diagnostics=mode, output=self.folder / mode)
            self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
            self.assertEqual((output / 'results.txt').exists(), mode == 'full')
            self.assertEqual((output / 'summary_results.txt').exists(), mode == 'full')
            self.assertEqual(len(list(output.glob('*.png'))), 2 if mode == 'full' else 0)


if __name__ == '__main__':
    unittest.main()
