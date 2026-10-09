# C3D8 C11 — package optimization

This technical phase record describes its own implementation and verification scope. See [current implementation status](IMPLEMENTATION_STATUS.md) for the Week 3 capabilities and build/package qualification.


**Status:** package optimization implemented; historical local package verification recorded below. A current Week 3 distribution has not been verified.

The Windows distribution now freezes the plate and C3D8 production entry
points into one `solver/FemSolver/FemSolver.exe` runtime. The GUI derives the
backend from the typed project and launches the executable with either
`--backend plate` or `--backend c3d8`. Backend checks use the same selector.

This removes the second embedded Python interpreter and duplicate NumPy,
SciPy, Matplotlib, and support libraries. One combined license tree and build
environment manifest cover the shared runtime. `PACKAGE-INFO.json` gives both
backend records the same executable path and hash and marks the package with
`sharedSolverRuntime: true`.

Measured on the C11 Release package:

| Artifact | Before C11 | After C11 | Change |
|---|---:|---:|---:|
| Complete `dist` | 494.3 MiB / 1,142 files | 331.1 MiB / 755 files | -163.2 MiB / -387 files (33.0% smaller) |
| Frozen solver payload | 345.9 MiB | 182.7 MiB | -163.2 MiB (47.2% smaller) |

The source-tree Python drivers remain separate, which keeps direct development
and qualification commands stable. The packaged plate and C3D8 tests exercise
both selectors with Python discovery disabled. Release integration tests also
run both GUI-controller workflows against the same executable.

Final C11 verification passed:

- Debug integration: 1/1 test passed.
- Release CTest with both bundled-solver variables set to `FemSolver.exe`: 6/6
  suites passed, including both controller workflows (57.84 seconds).
- Frozen plate backend: 3/3 tests passed with system Python unavailable.
- Frozen C3D8 backend: 2/2 tests passed with system Python unavailable.
- Packaged GUI: opened responsively and closed normally with a Windows-only
  `PATH`.

The numerical algorithms, accepted model-size envelope, result contracts, and
provisional engineering status do not change in this phase. External approval
of units, Z direction, foundation convention, tolerances, and an independent
nonuniform benchmark remains the C8 release gate.
