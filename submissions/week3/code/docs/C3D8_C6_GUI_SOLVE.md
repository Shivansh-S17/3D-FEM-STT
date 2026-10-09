# C3D8 C6 — first complete GUI solve

This technical phase record describes its own implementation and verification scope. See [current implementation status](IMPLEMENTATION_STATUS.md) for the Week 3 capabilities and build/package qualification.


**Status:** complete

The desktop run path now selects its driver and backend settings from the
typed project model. Plate projects retain `Backend/DriverPath`; solid projects
use `Backend/C3D8DriverPath` and discover `solver/c3d8/c3d8_solver.py` in a
development checkout or the shared `solver/FemSolver/FemSolver.exe` runtime in
a package. The launcher supplies `--backend c3d8` for the latter.

The C3D8 workflow now performs the complete sequence through the same GUI
controller used by the application:

1. Validate the typed solid and create an immutable run snapshot.
2. Export the exact C3D8 input schema into a fresh run directory.
3. Launch the structured production process and consume JSONL progress.
4. Enforce completion, hashes, backend ID and model-kind identity from the
   solver manifest.
5. Build the exact snapshot mesh and strictly import all solid result fields.
6. Attach results to the launch revision, persist run history, save/reopen the
   project and mark the restored result stale after a physical solid edit.

The end-to-end integration fixture solves a single brick and compares every
non-derived displacement, stress and engineering-strain value with the saved
production baseline. It also checks backend discovery, dependency checking,
progress events, provenance hashes, result ownership and reload behavior.

Testing exposed a shutdown lifetime defect: destruction of a document with
undo history could emit undo-stack signals after its analysis controller had
already been deleted. `ProjectController` now disconnects the undo stack and
explicitly tears down the owned pair in a safe order.

Debug verification passed all six CTest suites, including the original plate
real-run regression and the new complete C3D8 GUI run. This phase establishes
software workflow equivalence; author-approved conventions and an independent
physical benchmark remain open under C8.

See [solid interaction and reports](C3D8_C7_INTERACTION_REPORTS.md) for the subsequent implementation.
