# C3D8 C9 — Windows package

This technical phase record describes its own implementation and verification scope. See [current implementation status](IMPLEMENTATION_STATUS.md) for the Week 3 capabilities and build/package qualification.


**Status:** package implementation complete; historical local clean-PATH verification recorded below. Current Week 3 package handoff remains pending.

The historically verified `dist` contained the Release GUI, deployed Qt/VTK/MSVC libraries and the shared
frozen Python runtime `solver/FemSolver/FemSolver.exe`. C11 replaced the two
original C9 runtime copies with this consolidated package.

The GUI selects the backend entry point from the project model. Both backends implement
backend checking, structured progress, strict exits, atomic publication,
cancellation and source/result hashes. The runtime has one combined dependency
and license notice tree. `PACKAGE-INFO.json` records the GUI and solver hashes,
source revision, backend identities and supported integration.
`README.txt` travels with the package.

Local package verification passed:

- the plate frozen-solver suite with system Python discovery disabled;
- the C3D8 frozen-solver suite with system Python discovery disabled, including
  a Unicode-path solve exactly matching the production VTK baseline;
- invalid-input and pre-cancelled C3D8 runs publishing no result directory;
- GUI-controller end-to-end plate and C3D8 runs using the frozen executables;
- `dist/FemGui.exe` becoming responsive and closing normally with a Windows-only
  PATH and deliberately invalid Qt/QML environment paths.

The previous `dist` was preserved as a uniquely named sibling rollback folder
before publication. The package was not merged to `main`; all work remains on
`dev` as requested.

This is a provisional C3D8 package because C8 still lacks author convention
confirmation and an accepted independent nonuniform physical benchmark. The
software/package gates are complete and the existing plate workflow remains
available.
