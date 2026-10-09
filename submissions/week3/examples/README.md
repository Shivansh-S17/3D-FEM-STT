# Week 3 runnable examples

- [`demo-c3d8.femproject.json`](demo-c3d8.femproject.json) is a saved eight-brick C3D8 GUI project. Open it with **File > Open Project**, validate, and run the full-integration analysis. It starts without results so the run and result import can be demonstrated live.
- [`single-brick-input.json`](single-brick-input.json) is the strict solver input used for the standalone production run.
- [`solved-single-brick/`](solved-single-brick/) is fresh 8 October 2026 output from `code/solver/c3d8/c3d8_solver.py`: copied input, VTK solid result, and a completion manifest with hashes and numerical software checks. The event stream are in [`../evidence/single-brick-run-2026-10-08.log`](../evidence/single-brick-run-2026-10-08.log).

The one-brick result contains 8 nodes, 1 element, 24 degrees of freedom, and a maximum absolute displacement of about 0.01805 mm. Its scaled residual and force/moment imbalance satisfy the implementation's software thresholds. This example does not establish independent physical validation.
