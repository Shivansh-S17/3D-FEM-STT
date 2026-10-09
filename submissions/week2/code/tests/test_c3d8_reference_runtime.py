"""Runtime observations of the unchanged received driver in its reference env."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import numpy as np
import pyvista as pv
from c3d8_reference_support import ROOT
from c3d8_cases import cases

SOURCE=ROOT/'C3D8v/python'
sys.path.insert(0,str(SOURCE))
spec=importlib.util.spec_from_file_location('received_c3d8',SOURCE/'C3D8-slab.py')
received=importlib.util.module_from_spec(spec)
spec.loader.exec_module(received)


class ReceivedRuntimeTests(unittest.TestCase):
    def test_dormant_vtk_writer_preserves_values_but_misplaces_nodes(self):
        data=cases()['asymmetric_overlap']
        mesh=received.mesh_c3d8_enum_xzy(*(np.array(data['nodes'][a]) for a in 'xyz'))
        U=np.arange(mesh.getNumDofs(),dtype=float)/1000
        with tempfile.TemporaryDirectory(prefix='c3d8 vtk café ') as folder:
            path=Path(folder)/'received.vtk'
            received.export_to_vtk(mesh,received.SlabProperties(24000,.3),U,str(path))
            grid=pv.read(path)
            self.assertEqual(grid.n_points,mesh.getNumNodes())
            self.assertEqual(set(grid.point_data),{'u','v','w','sxx','syy','szz','sxy','syz','szx','exx','eyy','ezz','exy','eyz','ezx'})
            np.testing.assert_allclose(grid.point_data['u'],U[0::3])
            # The observed mismatch is retained as a regression of the original
            # source, not endorsed as the production interchange contract.
            self.assertFalse(np.array_equal(grid.points,mesh.getAllNodeCoordinates()))
            self.assertGreater(np.count_nonzero(np.any(grid.points != mesh.getAllNodeCoordinates(),axis=1)),0)

    def test_reader_errors_currently_exit_zero(self):
        with tempfile.TemporaryDirectory(prefix='c3d8 invalid café ') as folder:
            path=Path(folder)/'invalid.json'; path.write_text('{}')
            result=subprocess.run([sys.executable,'-B',str(SOURCE/'C3D8-slab.py'),str(path)],cwd=folder,
                                  capture_output=True,text=True,timeout=30,env={**os.environ,'PYTHONUTF8':'1'})
            self.assertEqual(result.returncode,0)
            self.assertIn('unexpected error',result.stdout)
            self.assertFalse((Path(folder)/'output-c3d8.csv').exists())

    def test_direct_original_matches_recorded_csv(self):
        case='asymmetric_overlap'; fixture=ROOT/'tests/fixtures/c3d8'/case
        with tempfile.TemporaryDirectory(prefix='c3d8 replay café ') as folder:
            result=subprocess.run([sys.executable,'-B',str(SOURCE/'C3D8-slab.py'),str(fixture/'input.json')],
                                  cwd=folder,capture_output=True,text=True,timeout=30,env={**os.environ,'PYTHONUTF8':'1'})
            self.assertEqual(result.returncode,0,result.stderr)
            actual=np.genfromtxt(Path(folder)/'output-c3d8.csv',delimiter=',',names=True)
            expected=np.genfromtxt(fixture/'expected/reference.csv',delimiter=',',names=True)
            self.assertEqual(actual.dtype.names,expected.dtype.names)
            for name in actual.dtype.names:
                # Per-unit baseline consistency tolerances, not author approval.
                atol=1e-8 if name.startswith('s') else 1e-10
                np.testing.assert_allclose(actual[name],expected[name],atol=atol,rtol=1e-9,err_msg=name)


if __name__=='__main__':
    unittest.main()
