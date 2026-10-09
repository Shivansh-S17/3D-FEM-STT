# C3D8 C10 — workflow and numerical readiness

This technical phase record describes its own implementation and verification scope. See [current implementation status](IMPLEMENTATION_STATUS.md) for the Week 3 capabilities and build/package qualification.


**Status:** implemented; external engineering acceptance remains open

The GUI starts with a valid C3D8 project and creates one directly through
**File > New C3D8 Project**, without a backend chooser. **File > Import C3D8
Solver Input** reads the strict solver JSON schema transactionally without an
application-imposed model-size ceiling, and leaves the imported project unsaved
so the user can choose its project file location.

C3D8 completion now requires all four software health checks to be present,
finite, nonnegative, and within these limits:

| Check | Completion limit |
|---|---:|
| Scaled algebraic residual | `1e-10` |
| Relative stiffness asymmetry | `1e-12` |
| Relative force imbalance | `1e-9` |
| Relative moment imbalance | `1e-9` |

The Python backend enforces the limits before atomic publication and records a
passed acceptance block in the result manifest. `SolverJob` independently
checks the values and acceptance status before the GUI imports any VTK data.
These are numerical software-health limits; they do not replace the still-open
engineering tolerances and independent physical benchmark.

The document dirty state now combines non-undoable changes with
`QUndoStack`'s clean index. Saving, editing, and undoing exactly back to the
saved model therefore clears the dirty state correctly. The About text,
status-bar mesh summary, and in-app C3D8 workflow help were also updated.

Repeated C3D8 element, foundation, and load kernels are cached by geometry and
load shape while preserving assembly order and numerical results. The full
qualification suite verifies the optimized path against the recorded cases.
