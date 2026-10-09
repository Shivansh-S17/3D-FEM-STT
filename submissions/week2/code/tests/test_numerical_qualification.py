import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'solver/reference'))
from expected_solver_implementation import run_analysis
from fem_local_Plate20_DOF5_07_09_2025 import Klocal, Flocal
from plate_solver import validate_input
from numerical_cases import cases, tolerance


class NumericalQualification(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='plate numerical ')
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.counter = 0

    def solve(self, data, solver='dense'):
        validate_input(data)
        self.counter += 1
        folder = self.folder / str(self.counter)
        folder.mkdir()
        input_path = folder / 'input.json'
        input_path.write_text(json.dumps(data), encoding='utf-8')
        raw = folder / 'raw.npz'
        summary = run_analysis(input_path, folder, diagnostics='none', linear_solver=solver, qualification_path=raw)
        with np.load(raw) as archive:
            arrays = {key: archive[key].copy() for key in archive.files}
        return summary, arrays

    def assert_fields(self, left, right):
        for key in left:
            if key in ('global_force', 'displacement_dofs'):
                continue # mixed-unit vectors are checked by DOF elsewhere
            atol, rtol = tolerance(key)
            np.testing.assert_allclose(left[key], right[key], atol=atol, rtol=rtol, err_msg=key)

    def test_dense_sparse_equivalence_and_equilibrium(self):
        for name, data in cases().items():
            with self.subTest(case=name):
                dense_summary, dense = self.solve(data)
                sparse_summary, sparse = self.solve(data, 'sparse')
                self.assert_fields(dense, sparse)
                for summary in (dense_summary, sparse_summary):
                    checks = summary['numericalChecks']
                    self.assertLess(checks['scaledStiffnessSymmetryError'], 1e-12)
                    for dof, residual in checks['scaledResidualByDof'].items():
                        self.assertLessEqual(residual, 1e-10, (name, dof, residual))
                    expected = checks['expectedVerticalForceN']
                    self.assertLessEqual(abs(checks['assembledVerticalForceN'] - expected), 1e-8 + 1e-11 * abs(expected))
                self.assertGreater(sparse_summary['numericalChecks']['factorNonzeros'], 0)

    def test_load_superposition(self):
        data = cases()['overlapping']
        _, combined = self.solve(data)
        separate = []
        for i in range(2):
            single = copy.deepcopy(data)
            single['loads'] = {key: [values[i]] for key, values in data['loads'].items()}
            separate.append(self.solve(single)[1])
        self.assert_fields(combined, {key: separate[0][key] + separate[1][key] for key in combined})

    def test_pressure_scaling_and_zero_load(self):
        data = cases()['nonuniform']
        _, initial = self.solve(data)
        data['loads']['q'][0] *= 3
        _, scaled = self.solve(data)
        self.assert_fields(scaled, {key: 3 * value for key, value in initial.items()})
        _, zero = self.solve(cases()['zero'])
        for key, values in zero.items():
            np.testing.assert_array_equal(values, np.zeros_like(values), err_msg=key)

    def test_constant_field_manufactured_solution_under_refinement(self):
        # Algebraic consistency test: a uniform pressure and foundation admit
        # constant w=q/Kz, zero rotations, and zero curvature in this formulation.
        # This does not validate foundation units/signs against an external source.
        for count in (3, 5, 9):
            data = cases()['uniform']
            data['nodes'] = dict(x=np.linspace(0, 1000, count).tolist(), y=np.linspace(0, 1000, count).tolist())
            _, arrays = self.solve(data, 'sparse')
            np.testing.assert_allclose(arrays['w'], 0.7, atol=1e-8, rtol=1e-8)
            for key in ('theta_x', 'theta_y', 'sxx_top', 'syy_top', 'sxy_top'):
                np.testing.assert_allclose(arrays[key], 0, atol=tolerance(key)[0])

    def test_element_constant_field_and_load_resultant(self):
        for a, b, thickness in ((500, 500, 200), (170, 280, 100), (550, 520, 175)):
            stiffness = Klocal(30000, 0.2, a, b, thickness, 1, 1, 1)
            force = Flocal(a, b, 0, a, 0, b, 0.7)
            self.assertEqual(stiffness.shape, (20, 20))
            displacement = np.zeros(20); displacement[0::5] = 0.7
            difference = stiffness @ displacement - force
            for offset in range(5):
                scale = 0.7 * a * b * (max(a, b) if offset in (1, 2) else 1)
                self.assertLess(np.max(np.abs(difference[offset::5])) / scale, 1e-11)
            self.assertAlmostEqual(float(np.sum(force[0::5])), 0.7 * a * b, places=7)


if __name__ == '__main__':
    unittest.main()
