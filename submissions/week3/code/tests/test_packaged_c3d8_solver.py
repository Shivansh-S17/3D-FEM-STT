"""Exercise the frozen C3D8 solver without any discoverable system Python."""
import hashlib, json, os, subprocess, tempfile
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
EXECUTABLE=Path(os.environ.get('FEM_TEST_BUNDLED_C3D8_SOLVER',ROOT/'dist/solver/FemSolver/FemSolver.exe')).resolve()
FIXTURE=ROOT/'tests/fixtures/c3d8/single_brick'
QUALIFIED_INPUT=ROOT/'tests/fixtures/c3d8/user_21x21x6/input.json'


class PackagedC3D8SolverTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(EXECUTABLE.is_file(),str(EXECUTABLE))
        self.temp=tempfile.TemporaryDirectory(prefix='frozen c3d8 café '); self.addCleanup(self.temp.cleanup)
        self.folder=Path(self.temp.name)
        self.environment={**os.environ,'PATH':os.path.join(os.environ['SystemRoot'],'System32'),
                          'PYTHONHOME':str(self.folder/'no-python'),'PYTHONPATH':str(self.folder/'no-modules')}
    def invoke(self,*args):
        return subprocess.run([str(EXECUTABLE),'--backend','c3d8',*map(str,args)],cwd=self.folder,env=self.environment,
                              capture_output=True,text=True,encoding='utf-8',timeout=90)
    def test_imports_and_solve_without_python(self):
        checked=self.invoke('--check-backend'); self.assertEqual(checked.returncode,0,checked.stderr)
        input_path=self.folder/'input café.json'; input_path.write_bytes((FIXTURE/'input.json').read_bytes())
        output=self.folder/'output café'
        solved=self.invoke('--input',input_path,'--output-dir',output,'--events-jsonl')
        self.assertEqual(solved.returncode,0,solved.stderr+solved.stdout)
        self.assertEqual(json.loads(solved.stdout.splitlines()[-1])['type'],'completed')
        manifest=json.loads((output/'run-result.json').read_text())
        self.assertEqual(manifest['backendId'],'c3d8-python-v1')
        self.assertEqual(manifest['modelKind'],'structured_solid_c3d8')
        self.assertEqual(manifest['resultSha256'],hashlib.sha256((output/'results.vtk').read_bytes()).hexdigest())
        self.assertEqual((output/'results.vtk').read_bytes(),(FIXTURE/'expected/production_results.vtk').read_bytes())
    def test_failure_and_cancellation_publish_nothing(self):
        invalid=self.folder/'invalid.json'; invalid.write_text('{}'); output=self.folder/'output'
        failed=self.invoke('--input',invalid,'--output-dir',output,'--events-jsonl')
        self.assertEqual(failed.returncode,2); self.assertFalse(output.exists())
        valid=self.folder/'input.json'; valid.write_bytes((FIXTURE/'input.json').read_bytes())
        marker=self.folder/'cancel.request'; marker.touch()
        cancelled=self.invoke('--input',valid,'--output-dir',output,'--events-jsonl','--cancel-file',marker)
        self.assertEqual(cancelled.returncode,130); self.assertFalse(output.exists())

    def test_user_input(self):
        input_path=self.folder/'qualified input.json'; input_path.write_bytes(QUALIFIED_INPUT.read_bytes())
        output=self.folder/'qualified output'
        solved=self.invoke('--input',input_path,'--output-dir',output,'--events-jsonl')
        self.assertEqual(solved.returncode,0,solved.stderr+solved.stdout)
        summary=json.loads((output/'run-result.json').read_text())['summary']
        self.assertEqual((summary['nodes'],summary['elements'],summary['dofs']),(2646,2000,7938))
        self.assertEqual(summary['numericalAcceptance']['status'],'passed')


if __name__=='__main__': unittest.main()
