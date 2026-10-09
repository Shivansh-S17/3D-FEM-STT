# Numerical and deployment qualification — 9 September 2026

The automated checks establish software consistency for the cases below.
They do not establish independent engineering accuracy, confirm the author's
foundation units/rotation conventions, or prove absence of bugs. Physical
benchmark acceptance and nonconstant mesh-convergence acceptance remain open.

## Numerical coverage

`tests/test_numerical_qualification.py` runs five test methods covering:

- Dense/sparse equivalence on symmetric, asymmetric nonuniform, overlapping,
  zero-pressure, whole-domain pressure, and element-edge-aligned patches.
  The asymmetric patch cuts through elements. Inputs are reproducible in
  `tests/numerical_cases.py`.
- Additivity of two overlapping loads and linear pressure scaling by three.
- Exactly zero result arrays for a stable zero-load model.
- Uniform-pressure manufactured solution: constant `w=q/Kz=0.7`, zero
  rotations and bending stress, on 3x3, 5x5, and 9x9 node grids. This tests
  algebraic consistency under refinement; it is not a physical convergence
  study against an independent reference.
- Element stiffness/load consistency for a constant displacement on three
  rectangular element geometries, including unequal side lengths/thicknesses.

Raw NumPy arrays are compared before VTK rounding. Provisional **software**
tolerances are `abs(a-b) <= atol + rtol*abs(b)`:

| Quantity | Absolute tolerance | Relative tolerance |
|---|---:|---:|
| Displacements and displacement vector | 1e-8 mm | 1e-6 |
| Rotations | 1e-11 rad | 1e-6 |
| Stress components | 1e-7 MPa | 1e-6 |

These are not instructor- or solver-author-approved acceptance tolerances.
Mixed force/moment and displacement/rotation vectors are not compared with
a single dimensional tolerance.

Each successful solve records `r=K*U-F`. For each of the five DOF groups,
the reported residual is `max(abs(r[group])) / max((abs(K)*abs(U)+abs(F))[group])`.
A zero denominator uses the absolute numerator. Tests require each group's
ratio <=1e-10. This is a backward-residual check, not a condition estimate or
a bound on forward displacement error. Matrix symmetry is measured after
diagonal scaling, `S=D^-1*K*D^-1`, with `D=sqrt(abs(diag(K)))`, and requires
`max(abs(S-S.T))/max(abs(S)) < 1e-12`. The assembled vertical force must match
the analytic sum of rectangular patch forces within `1e-8 N + 1e-11*abs(F)`.

The existing contract suite additionally rejects all-zero foundation with
no supports, injects singular and nonfinite solves, checks cancellation and
atomic publication, and compares the tiny dense VTK output exactly with the
saved regression fixture. Other foundation choices are not thereby qualified.

## Sparse experiment and measured envelope

The dense reference remains the GUI/CLI default, retaining five dense solves.
`--linear-solver sparse` is an explicit CLI experiment. It factors the assembled
CSC matrix once and reuses that factorization for all five load increments,
using [SciPy's documented splu API](https://docs.scipy.org/doc/scipy/reference/generated/scipy.sparse.linalg.splu.html).
The manifest identifies the selected solver and records phase, factorization,
solve, and total timings, sparse nonzeros, and estimated dense matrix bytes.

[Raw measurements](qualification-measurements.json) were collected with
`python packaging/qualify-solver.py --output docs/qualification-measurements.json`.
The harness uses one fresh Python child per row, 20 ms memory polling,
Windows peak working set when available, and a 90-second/1-GiB termination
guard. Wall time includes child startup/import/export. These are single
observations, not repeated statistical estimates or frozen-executable timings.

Machine: Windows 11 x64, Intel Family 6 Model 154, 16 logical CPUs, 16.9 GB
physical RAM. Python 3.12.2, NumPy 1.26.3, SciPy 1.13.0. Thread-count environment
variables were unset; the raw artifact records this explicitly. Each case is
a 1000x1000 plate with a central 500x500 pressure patch and positive foundation.

| Nodes per axis | Nodes / DOFs | Dense seconds / peak MiB | Sparse seconds / peak MiB |
|---|---|---|---|
| 5 | 25 / 125 | 0.76 / 67.5 | 0.66 / 66.7 |
| 9 | 81 / 405 | 0.95 / 71.4 | 0.76 / 68.4 |
| 17 | 289 / 1445 | 1.48 / 105.4 | 1.05 / 73.9 |
| 25 | 625 / 3125 | 2.91 / 231.2 | 1.71 / 87.0 |
| 33 | 1089 / 5445 | 6.92 / 542.4 | 2.36 / 101.2 |

All ten processes completed. The measured envelope extends through **33x33
nodes / 5445 DOFs for this model on this machine**. It does not qualify every
geometry/material at that node count. The GUI warns above 1089 nodes; the GUI
and CLI retain the separate hard guard against allocating more than 1 GiB
for one dense matrix. Passing that guard is not a peak-memory guarantee.
Larger grids, thin/high-aspect-ratio elements, near-incompressible material,
negative foundation and ill-conditioned cases need further qualification.

## Windows deployment

`packaging/fem-solver.spec` freezes Python, both production backend entry
points, and their numerical dependencies into one `FemSolver.exe` package using
[PyInstaller's spec-file workflow](https://pyinstaller.org/en/stable/spec-files.html).
Agg is the only plotting backend included; unrelated interactive/test/ML
packages are excluded. The three source files are bundled for provenance.
`packaging/solver-build-requirements.txt` records the tested principal build
versions; `licenses/build-environment.json` records installed dependency
versions and copied notices. A fully locked transitive build environment is
not yet provided.

`dist/FemGui.exe` discovers `solver/FemSolver/FemSolver.exe` beside it and
selects the plate or C3D8 entry point from the active model.
Existing built-in backend settings migrate to this local bundle. Custom
executable backends must implement the same structured contract and
`--check-backend`; custom scripts retain their existing compatibility mode.
Python-path settings are ignored for executable backends. Keep the entire
`dist` directory together when copying or zipping the application.

Reproduce package checks after packaging:

```powershell
python tests/test_packaged_solver.py
$env:FEM_TEST_BUNDLED_SOLVER = "$PWD\dist\solver\FemSolver\FemSolver.exe"
$env:FEM_TEST_BUNDLED_C3D8_SOLVER = $env:FEM_TEST_BUNDLED_SOLVER
ctest --test-dir build/release --output-on-failure
```

The Python package checks remove Python from child PATH and set invalid
PYTHONHOME/PYTHONPATH, exercise Unicode/space paths, dense and sparse solving,
full plot diagnostics, source/result hashes, invalid input and cancellation.
The Qt `bundledSolverWorkflow` test uses a nonexistent Python executable,
checks the bundle, launches a real solve, imports its fields, and compares
them with the reference fixture. An actually clean Windows machine remains
a separate deployment acceptance check.

Recorded verification for this package:

- Debug CTest: all three suites passed (17.36 seconds). The optional bundle
  workflow and native desktop cases were skipped in this run.
- Release CTest with `FEM_TEST_BUNDLED_SOLVER`: all three suites passed
  (19.21 seconds); the Qt report contains 53 records, zero failures, and two
  native-desktop skips. The real bundled solve passed.
- `tests/test_packaged_solver.py`: all three methods passed (10.27 seconds).
- `packaging/smoke-package.ps1`: the packaged GUI created a responsive window
  and closed normally with exit zero, using Windows-only PATH. This desktop
  check is recorded in `build/phase7-package-launch.json`.

The native rendering/drawing regression cases passed during Phase 6; they
were not repeated for these backend changes. No new renderer code was required.

## Remaining acceptance work

Obtain an independent benchmark and approved per-field tolerances, confirm
foundation units and rotation signs with the solver author, and compare a
nonconstant solution under mesh refinement. No supplied reference currently
resolves these requirements. Sparse default promotion and broader model-size
support should follow that review rather than these software comparisons alone.
