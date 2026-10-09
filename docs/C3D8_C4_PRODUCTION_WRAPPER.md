# C4: production solid solver wrapper

This technical phase record describes its own implementation and verification scope. See [current implementation status](IMPLEMENTATION_STATUS.md) for the Week 3 capabilities and build/package qualification.


**Status:** implemented

`solver/c3d8/c3d8_solver.py` provides the strict process boundary used by the
plate backend: absolute input/output paths, duplicate-key detection, exact
schema validation, JSONL progress, cooperative cancellation, distinct exit
codes, staged output, hashes and atomic final-directory publication.

`c3d8_analysis.py` makes the received full-integration formulation callable.
It preserves X-Z-Y numbering, element/foundation matrices, patch integration,
sparse solution and arithmetic nodal recovery. `C3D8local.py` is a byte-for-byte
working copy of the received element module; tests enforce that equality while
the original `C3D8v/` snapshot stays unchanged. Reduced integration is rejected.

The output is dependency-light legacy VTK with exact brick connectivity,
`solver_node_id`, `solver_element_id`, u/v/w, six stresses, six engineering
strains and the displacement vector. The manifest records backend/model IDs,
runtime versions, input/result hashes, source hashes and counts.

`C3D8Production` verifies backend discovery, four captured cases, tight array
agreement, strict invalid-input behavior, cancellation and atomic publication.
The large supplied case remains a runtime/performance gate rather than a normal
C4 test. See [solid results](C3D8_C5_RESULTS.md) for the field catalog and VTK import.
