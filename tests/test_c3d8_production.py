"""Production C3D8 wrapper contract and captured-baseline equivalence."""
import hashlib, json, subprocess, sys, tempfile
from pathlib import Path
import unittest
import numpy as np

ROOT=Path(__file__).resolve().parents[1]; DRIVER=ROOT/'solver/c3d8/c3d8_solver.py'; FIXTURES=ROOT/'tests/fixtures/c3d8'
sys.path.insert(0,str(DRIVER.parent))
from c3d8_solver import enforce_numerical_acceptance, validate_input

def read_vtk(path):
    lines=Path(path).read_text(encoding='ascii').splitlines(); out={}; i=lines.index(next(x for x in lines if x.startswith('POINTS ')))
    count=int(lines[i].split()[1]); out['coordinates']=np.array([[float(v) for v in row.split()] for row in lines[i+1:i+1+count]])
    i=next(n for n,x in enumerate(lines) if x.startswith('CELLS ')); elements=int(lines[i].split()[1]); out['connectivity']=np.array([[int(v) for v in row.split()[1:]] for row in lines[i+1:i+1+elements]])
    i=next(n for n,x in enumerate(lines) if x.startswith('POINT_DATA '))+1
    while i<len(lines):
        if lines[i].startswith('SCALARS '):
            name=lines[i].split()[1]; i+=2; out[name]=np.array([float(v) for v in lines[i:i+count]]); i+=count
        elif lines[i].startswith('VECTORS '):
            name=lines[i].split()[1]; i+=1; out[name]=np.array([[float(v) for v in row.split()] for row in lines[i:i+count]]); i+=count
        else: i+=1
    return out

def run(input_path,output,extra=()):
    command=[sys.executable,str(DRIVER),'--input',str(input_path.resolve()),'--output-dir',str(output.resolve()),'--events-jsonl',*extra]
    return subprocess.run(command,capture_output=True,text=True,encoding='utf-8',timeout=40)

class C3D8ProductionTests(unittest.TestCase):
    def test_working_element_copy_matches_received_source(self):
        self.assertEqual(hashlib.sha256((ROOT/'C3D8v/python/C3D8local.py').read_bytes()).hexdigest(),
                         hashlib.sha256((ROOT/'solver/c3d8/C3D8local.py').read_bytes()).hexdigest())

    def test_backend_check(self):
        result=subprocess.run([sys.executable,str(DRIVER),'--check-backend'],capture_output=True,text=True,timeout=20)
        self.assertEqual(result.returncode,0,result.stderr); self.assertIn('Backend imports OK',result.stdout)

    def test_captured_cases_match(self):
        mapping={'stress':['sxx','syy','szz','sxy','syz','szx'],'strain':['exx','eyy','ezz','exy','eyz','ezx']}
        for case in ('single_brick','asymmetric_overlap','analytical_compression','zero_load'):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as temp:
                output=Path(temp)/'result'; result=run(FIXTURES/case/'input.json',output)
                self.assertEqual(result.returncode,0,result.stderr+result.stdout)
                events=[json.loads(line) for line in result.stdout.splitlines()]
                self.assertEqual(events[-1]['type'],'completed'); self.assertTrue(any(e.get('phase')=='solve' for e in events))
                manifest=json.loads((output/'run-result.json').read_text())
                self.assertEqual(manifest['backendId'],'c3d8-python-v1'); self.assertEqual(manifest['status'],'Completed')
                self.assertEqual(manifest['resultSha256'],hashlib.sha256((output/'results.vtk').read_bytes()).hexdigest())
                summary=manifest['summary']; checks=summary['checks']
                self.assertEqual(summary['numericalAcceptance']['status'],'passed')
                self.assertLess(checks['scaledResidual'],1e-12); self.assertLess(checks['symmetryRelative'],1e-14)
                self.assertLess(checks['forceBalanceRelative'],1e-11); self.assertLess(checks['momentBalanceRelative'],1e-11)
                self.assertGreater(summary['stiffnessNonzeros'],0); self.assertGreater(summary['storedSystemBytes'],0)
                self.assertGreater(summary['timingsSeconds']['total'],0)
                actual=read_vtk(output/'results.vtk')
                with np.load(FIXTURES/case/'expected/arrays.npz') as expected:
                    np.testing.assert_allclose(actual['coordinates'],expected['coordinates'],rtol=0,atol=0)
                    np.testing.assert_array_equal(actual['connectivity'],expected['connectivity'])
                    np.testing.assert_allclose(actual['displacement'],expected['displacement'],rtol=2e-10,atol=2e-11)
                    for group,names in mapping.items():
                        np.testing.assert_allclose(np.column_stack([actual[n] for n in names]),expected[group],rtol=2e-9,atol=2e-10)

    def test_numerical_acceptance_rejects_missing_nonfinite_and_large_checks(self):
        valid={'checks':{'scaledResidual':1e-14,'symmetryRelative':0.0,
                         'forceBalanceRelative':1e-14,'momentBalanceRelative':1e-14}}
        self.assertEqual(enforce_numerical_acceptance(valid)['numericalAcceptance']['status'],'passed')
        for update in ({'forceBalanceRelative':1e-2},{'scaledResidual':float('nan')},{'momentBalanceRelative':None}):
            summary=json.loads(json.dumps(valid))
            summary.pop('numericalAcceptance',None)
            summary['checks'].update(update)
            with self.assertRaisesRegex(RuntimeError,'Numerical acceptance failed'):
                enforce_numerical_acceptance(summary)

    def test_invalid_and_reduced_inputs_publish_nothing(self):
        base=json.loads((FIXTURES/'single_brick/input.json').read_text())
        corruptions=[lambda d:d['options'].__setitem__('reduced_integration',True),
                     lambda d:d['nodes'].__setitem__('z',[0,0]), lambda d:d['subgrade'].__setitem__('Kz',0)]
        for index,corrupt in enumerate(corruptions):
            with tempfile.TemporaryDirectory() as temp:
                root=Path(temp); data=json.loads(json.dumps(base)); corrupt(data); input_path=root/'input.json'; input_path.write_text(json.dumps(data))
                output=root/'result'; result=run(input_path,output)
                self.assertEqual(result.returncode,2); self.assertFalse(output.exists()); self.assertIn('"type": "error"',result.stdout)

    def test_model_size_has_no_added_solver_limit(self):
        data=json.loads((FIXTURES/'single_brick/input.json').read_text())
        data['nodes']={'x':[i*100. for i in range(14)],
                       'y':[i*100. for i in range(14)],
                       'z':[i*100. for i in range(14)]}
        self.assertEqual(len(validate_input(data)['nodes']['x'])*14*14*3,8232)

    def test_preexisting_cancellation_publishes_nothing(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); marker=root/'cancel.request'; marker.write_text('cancel\n'); output=root/'result'
            result=run(FIXTURES/'single_brick/input.json',output,('--cancel-file',str(marker.resolve())))
            self.assertEqual(result.returncode,130); self.assertFalse(output.exists())
            self.assertEqual(json.loads(result.stdout.splitlines()[-1])['type'],'cancelled')

if __name__=='__main__': unittest.main()
