"""Executable C3D8 software-qualification and measured-envelope checks."""
import json, subprocess, sys, tempfile, time
from pathlib import Path
import unittest
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'solver/c3d8'))
from c3d8_analysis import run_analysis
from C3D8local import Klocal,Kslocal
from test_c3d8_production import read_vtk


def base_data():
    return {'nodes':{'x':[10.,180.,460.],'y':[-20.,260.],'z':[5.,68.,151.]},
            'material':{'Emod':24000.,'nu':.3},'subgrade':{'Kx':20.,'Ky':35.,'Kz':50.},
            'loads':[],'options':{'reduced_integration':False}}


class C3D8QualificationTests(unittest.TestCase):
    def solve(self,data,folder):
        summary=run_analysis(data,folder)
        result=read_vtk(Path(folder)/'results.vtk')
        self.assertLess(summary['checks']['scaledResidual'],1e-12)
        self.assertLess(summary['checks']['forceBalanceRelative'],1e-11)
        self.assertLess(summary['checks']['momentBalanceRelative'],1e-11)
        return result,summary

    def test_linearity_and_pressure_superposition(self):
        a={'q':.8,'x1':30.,'x2':330.,'y1':10.,'y2':220.}
        b={'q':.35,'x1':130.,'x2':440.,'y1':100.,'y2':240.}
        with tempfile.TemporaryDirectory() as temp:
            outputs=[]
            for index,loads in enumerate(([a],[b],[a,b])):
                data=base_data(); data['loads']=loads
                outputs.append(self.solve(data,Path(temp)/str(index))[0])
            for name in ('u','v','w','sxx','syy','szz','sxy','syz','szx','exx','eyy','ezz','exy','eyz','ezx'):
                np.testing.assert_allclose(outputs[2][name],outputs[0][name]+outputs[1][name],rtol=2e-10,atol=2e-11)

    def test_analytical_compression_under_xyz_refinement(self):
        q,E,kz=.8,24000.,50.
        with tempfile.TemporaryDirectory() as temp:
            for divisions in (1,2,4):
                data={'nodes':{'x':np.linspace(0,170,divisions+1).tolist(),
                               'y':np.linspace(0,280,divisions+1).tolist(),
                               'z':np.linspace(0,63,divisions+1).tolist()},
                      'material':{'Emod':E,'nu':0.},'subgrade':{'Kx':50.,'Ky':50.,'Kz':kz},
                      'loads':[{'q':q,'x1':0.,'x2':170.,'y1':0.,'y2':280.}],
                      'options':{'reduced_integration':False}}
                result,_=self.solve(data,Path(temp)/str(divisions))
                expected=q/kz+q*(63-result['coordinates'][:,2])/E
                np.testing.assert_allclose(result['w'],expected,rtol=1e-9,atol=1e-10)
                np.testing.assert_allclose(result['u'],0,atol=1e-10); np.testing.assert_allclose(result['v'],0,atol=1e-10)
                np.testing.assert_allclose(result['szz'],-q,atol=1e-8)

    def test_supported_material_and_aspect_sensitivity_is_finite(self):
        for nu in (0.,.3,.45,.49):
            for depth in (10.,63.,280.):
                matrix=Klocal(24000.,nu,170.,280.,depth)+Kslocal(170.,280.,depth,20.,35.,50.)
                values=np.linalg.eigvalsh(matrix)
                self.assertGreater(values[0],0)
                condition=float(values[-1]/values[0])
                self.assertTrue(np.isfinite(condition)); self.assertLess(condition,1e12)

    def test_measured_6615_dof_envelope_matches_received_baseline(self):
        driver=ROOT/'solver/c3d8/c3d8_solver.py'; input_path=ROOT/'C3D8v/python/inputs.json'
        with tempfile.TemporaryDirectory() as temp:
            output=Path(temp)/'result'; started=time.perf_counter()
            run=subprocess.run([sys.executable,str(driver),'--input',str(input_path.resolve()),
                                '--output-dir',str(output.resolve())],capture_output=True,text=True,timeout=90)
            elapsed=time.perf_counter()-started
            self.assertEqual(run.returncode,0,run.stderr+run.stdout); self.assertLess(elapsed,60)
            manifest=json.loads((output/'run-result.json').read_text()); summary=manifest['summary']; checks=summary['checks']
            self.assertEqual((summary['nodes'],summary['elements'],summary['dofs']),(2205,1600,6615))
            self.assertEqual(summary['stiffnessNonzeros'],293463)
            self.assertLess(summary['storedSystemBytes'],10*1024*1024)
            self.assertLess(checks['scaledResidual'],1e-12); self.assertLess(checks['forceBalanceRelative'],1e-11)
            self.assertLess(checks['momentBalanceRelative'],1e-11); self.assertLess(summary['timingsSeconds']['total'],60)
            actual=read_vtk(output/'results.vtk')
            with np.load(ROOT/'tests/fixtures/c3d8/supplied_21x21x5/expected/arrays.npz') as expected:
                np.testing.assert_allclose(actual['displacement'],expected['displacement'],rtol=2e-9,atol=2e-10)
                for group,names in {'stress':['sxx','syy','szz','sxy','syz','szx'],
                                    'strain':['exx','eyy','ezz','exy','eyz','ezx']}.items():
                    np.testing.assert_allclose(np.column_stack([actual[n] for n in names]),expected[group],rtol=2e-8,atol=2e-9)

    def test_user_input_passes_numerical_gates(self):
        driver=ROOT/'solver/c3d8/c3d8_solver.py'
        input_path=ROOT/'tests/fixtures/c3d8/user_21x21x6/input.json'
        with tempfile.TemporaryDirectory() as temp:
            output=Path(temp)/'result'; started=time.perf_counter()
            run=subprocess.run([sys.executable,str(driver),'--input',str(input_path.resolve()),
                                '--output-dir',str(output.resolve())],capture_output=True,text=True,timeout=90)
            elapsed=time.perf_counter()-started
            self.assertEqual(run.returncode,0,run.stderr+run.stdout); self.assertLess(elapsed,60)
            summary=json.loads((output/'run-result.json').read_text())['summary']; checks=summary['checks']
            self.assertEqual((summary['nodes'],summary['elements'],summary['dofs']),(2646,2000,7938))
            self.assertLess(summary['storedSystemBytes'],12*1024*1024)
            self.assertLess(checks['scaledResidual'],1e-12)
            self.assertLess(checks['forceBalanceRelative'],1e-11)
            self.assertLess(checks['momentBalanceRelative'],1e-11)
            self.assertEqual(summary['numericalAcceptance']['status'],'passed')


if __name__=='__main__': unittest.main()
