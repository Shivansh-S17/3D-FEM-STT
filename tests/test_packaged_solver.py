"""Exercise the frozen solver with system Python discovery deliberately unavailable."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXECUTABLE = Path(os.environ.get('FEM_TEST_BUNDLED_SOLVER', ROOT / 'dist/solver/FemSolver/FemSolver.exe')).resolve()
FIXTURE = ROOT / 'tests/fixtures/tiny_3x3'


class PackagedSolverTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(EXECUTABLE.is_file(), str(EXECUTABLE))
        self.temp = tempfile.TemporaryDirectory(prefix='frozen solver café ')
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.environment = {**os.environ, 'PATH': os.path.join(os.environ['SystemRoot'], 'System32'),
                            'PYTHONHOME': str(self.folder / 'no-python'),
                            'PYTHONPATH': str(self.folder / 'no-modules'),
                            'MPLCONFIGDIR': str(self.folder / 'matplotlib')}

    def invoke(self, *arguments):
        return subprocess.run([str(EXECUTABLE), '--backend', 'plate', *map(str, arguments)], cwd=self.folder,
                              env=self.environment, capture_output=True, text=True,
                              encoding='utf-8', timeout=60)

    def test_imports_without_python_on_path(self):
        result = self.invoke('--check-backend')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Backend imports OK', result.stdout)

    def test_dense_sparse_and_full_diagnostics(self):
        for solver, diagnostics in (('dense', 'summary'), ('sparse', 'full')):
            with self.subTest(solver=solver):
                path = self.folder / 'input café.json'
                path.write_bytes((FIXTURE / 'input.json').read_bytes())
                output = self.folder / ('output café ' + solver)
                result = self.invoke('--input', path, '--output-dir', output, '--events-jsonl',
                                     '--linear-solver', solver, '--diagnostics', diagnostics)
                self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
                self.assertEqual(json.loads(result.stdout.splitlines()[-1])['type'], 'completed')
                manifest = json.loads((output / 'run-result.json').read_text())
                self.assertEqual(manifest['summary']['linearSolver'], solver)
                self.assertEqual(manifest['resultSha256'], hashlib.sha256((output / 'results.vtk').read_bytes()).hexdigest())
                self.assertEqual(manifest['inputSha256'], hashlib.sha256(path.read_bytes()).hexdigest())
                for name, checksum in manifest['sources'].items():
                    self.assertEqual(checksum, hashlib.sha256((ROOT / 'solver/reference' / name).read_bytes()).hexdigest())
                if solver == 'dense':
                    self.assertEqual((output / 'results.vtk').read_bytes(), (FIXTURE / 'expected_output/results.vtk').read_bytes())
                else:
                    self.assertEqual(len(list(output.glob('*.png'))), 2)
                self.assertFalse(list(self.folder.glob('*.partial-*')))

    def test_failure_and_cancellation_do_not_publish(self):
        path = self.folder / 'input.json'
        path.write_text('{}')
        output = self.folder / 'output'
        result = self.invoke('--input', path, '--output-dir', output, '--events-jsonl')
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertFalse(output.exists())
        path.write_bytes((FIXTURE / 'input.json').read_bytes())
        marker = self.folder / 'cancel.request'
        marker.touch()
        result = self.invoke('--input', path, '--output-dir', output, '--events-jsonl', '--cancel-file', marker)
        self.assertEqual(result.returncode, 130, result.stderr)
        self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()
