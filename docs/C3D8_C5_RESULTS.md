# C5: solid results and inspection

This technical phase record describes its own implementation and verification scope. See [current implementation status](IMPLEMENTATION_STATUS.md) for the Week 3 capabilities and build/package qualification.


**Status:** implemented

Results now retain their `ModelKind` and select a backend-specific field
catalog. The C3D8 catalog contains u/v/w, six stresses, six engineering strains,
the displacement vector and derived displacement magnitude, with units and
nodal-averaging semantics stated explicitly.

`ResultImporter` validates either quads or hexahedra according to the immutable
run snapshot. It checks counts, finite coordinates and arrays, exact coordinate
and connectivity order against the matching mesh builder, scalar/vector
consistency, complete required fields, and explicit solver node/element IDs.
`ProjectController` can safely restore C3D8 results only when run, backend,
snapshot and current project kinds agree.

The asymmetric nonuniform fixture verifies all 17 catalog entries (16 selectable scalars and the displacement vector),
source ID 17, X-Z-Y coordinate order, extrema and displacement magnitude.
Focused corruptions prove that a missing strain field and reordered brick are
rejected. The existing viewport, inspector, deformation, extrema and CSV/PNG
paths consume the result's own catalog rather than the plate catalog.

The subsequent [GUI workflow](C3D8_C6_GUI_SOLVE.md) selects the driver by backend and verifies edit-run-reopen-stale ownership.
