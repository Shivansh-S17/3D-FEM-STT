# Implementation status — Week 2, 1 October 2026

The actual C3D8 source has arrived. The supported Week 2 workflow is described in
[C3D8_C3_EDITORS_MESH.md](C3D8_C3_EDITORS_MESH.md).
Intake C0 and C1's executable/software baseline are complete. Five
original full-integration cases pass, including analytical compression and
zero load; raw fixtures and reference tests are recorded in
[C3D8_BASELINE.md](C3D8_BASELINE.md). Author-approved conventions and a runnable
GetFEM/accepted external comparison remain pending. C2 (typed model and adapter
boundaries) is complete and documented in
[C3D8_C2_MODEL_ADAPTER.md](C3D8_C2_MODEL_ADAPTER.md). C3 solid editors and exact
volume meshing are also complete; see
[C3D8_C3_EDITORS_MESH.md](C3D8_C3_EDITORS_MESH.md). C4 production solver wrapping
is next. No GUI C3D8 execution, computed solid result viewing or new C3D8 package has been
produced. The received source remains unchanged.

The C3D8 model uses eight-node bricks, three translations per node and a full
3D stress/strain output model. It requires new typed model, mesh, adapter and
result support. The [code review](C3D8_CODE_REVIEW.md) records reduced-integration
scale/rank findings, output-order issues, missing runtime dependencies and
reference-frame differences. The following phase table describes the existing
plate implementation only; it is not C3D8 completion status.

The C3D8 solve and result workflow is not yet implemented. Automated regression checks
and a working desktop package cannot prove that all bugs are absent.

| Plate capability | Current state |
|---|---|
| 0 — Backend confirmation | Element module and tiny baseline are present and runnable. Author-approved conventions, physical benchmarks, and acceptance tolerances remain unresolved. |
| 1 — Project data | Typed model, strict project loading, save/open, immutable run snapshots, solver export, and session undo/redo for plate edits are implemented. |
| 2 — Plate model/editors | Coordinate/material/foundation/patch editors and deterministic quad mesh are implemented and regression-tested. |
| 3 — Selection/results | Single node/element/pressure-patch picking, inspector, all 14 scalars, and deformation work. Advanced selection remains outside this single-entity workflow. |
| 4 — Complete real run | Asynchronous solve, validation/import, run history, stale-result ownership, failure/cancel handling, and reopening results are implemented. |
| 5 — Production contract | Implemented: strict CLI, structured events, distinct exits, cooperative Stop with kill fallback, optional diagnostics, atomic result publication, versioned manifest, runtime/source provenance, and GUI enforcement. |
| 6 — Interaction/reporting | Implemented: rectangle preview, validated patch form, undoable Apply, Escape cancellation, table/viewport patch highlighting and inspection, visibility independent of analysis, extrema navigation, atomic CSV/PNG exports with provenance, and scrollable editors. |
| 7 — Numerical/performance qualification | Automated dense/sparse comparisons, asymmetric/overlapping/zero cases, element/manufactured-solution checks, scaled equilibrium checks, local time/memory measurements and frozen Python deployment are implemented. Independent physical benchmarks/convergence acceptance, author-approved conventions/tolerances, and a clean-machine deployment check remain open. |

Phase 5 refactors orchestration into a callable API while retaining the
reference element mathematics, assembly, five force increments, and stress
recovery. The tiny reference VTK output matches the pre-existing fixture
exactly. The reference script no longer creates output on import or silently
falls back to legacy input files.

Testing exposed a zero-foundation case that returned finite values without a
singular-matrix exception. GUI validation and the production CLI now explicitly
reject that unsupported configuration. This does not qualify all other
foundation choices or establish numerical conditioning.

Run `ctest --test-dir build/debug --output-on-failure` and the corresponding
Release command. `Integration` covers model, file, process, and GUI wiring;
`SolverContract` covers the production Python contract; `NumericalQualification`
covers pre-VTK numerical comparisons. Native rendering and
mouse-picking checks require the separate Windows-desktop command in the source README.
Packaging produces `dist/FemGui.exe`; Python and its numerical
dependencies are bundled in `dist/solver/PlateSolver`. See the
[qualification report](NUMERICAL_QUALIFICATION.md) for methods, measurements,
package checks, and remaining acceptance work.

The Week 2 [build and test record](../../evidence/BUILD_AND_TEST.md) records four passing Release CTest suites and one native GUI capture check. The optional original-driver runtime suite and frozen-package checks are not included in that count.
