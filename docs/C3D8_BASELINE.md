# C1: executable C3D8 reference baseline

Date: 12 September 2026. Scope: received-source reference qualification.

The unchanged received full-integration driver now runs in an isolated Windows
Python environment. Five cases pass direct execution, captured-array comparison,
scaled equilibrium, spring force balance and moment balance. An analytical
compression solution independently checks displacement, stress and strain.
This completes the executable/software baseline portion of C1. Author-confirmed
units/conventions and an accepted external solver comparison remain open.

## Captured cases

| Case | Nodes / elements / DOFs | Direct time | Observed process-tree peak |
|---|---|---:|---:|
| Single brick | 8 / 1 / 24 | 1.78 s | 154.4 MiB |
| Nonuniform asymmetric overlapping patches | 18 / 4 / 54 | 2.16 s | 154.5 MiB |
| Analytical compression, nu=0 | 18 / 4 / 54 | 3.07 s | 154.2 MiB |
| Stable zero load | 18 / 4 / 54 | 2.46 s | 154.2 MiB |
| Supplied 21x21x5 grid | 2205 / 1600 / 6615 | 12.24 s | 184.8 MiB |

The original CLI runs as a child with its real imports, assembly, sparse solve,
recovery and CSV export. A second execution uses `runpy` with `__main__` to
inspect the original globals after execution and save coordinates, connectivity,
displacement, force, six strains and six stresses without changing source code.
Direct and captured CSV values agree within 1e-12 absolute/relative tolerance.

The supplied input's assembled load is +9,800,000 along source Z. Across all
cases, maximum scaled residual was below 6.2e-16, relative force-balance error
below 2.2e-15 and relative moment-balance error below 1.2e-15. Spring reactions
were integrated independently with bilinear face interpolation and a 2x2
quadrature rule, rather than read back from the source's foundation matrix.
These residuals demonstrate equilibrium consistency; they do not bound errors
in an ill-conditioned solution or establish physical accuracy for every model.

The performance observations use fresh child processes but warm OS caches,
with 20-ms sampling. Windows' venv interpreter launches a child Python process;
the harness measures and bounds the complete owned process tree. Reported
memory is the maximum observed sum of per-process peak working sets, with an
RSS fallback on other platforms; it is conservative and is not a simultaneous
private-memory measurement. Each process tree has a 120-second/1536-MiB guard.
The earlier launcher-only measurements were discarded and are not published.
This is one observation per case, not a statistical performance qualification.

Machine/runtime details, dependency versions, hashes and measurements are in
[C3D8_BASELINE_MEASUREMENTS.json](C3D8_BASELINE_MEASUREMENTS.json). Baselines are
stored under [tests/fixtures/c3d8](../tests/fixtures/c3d8), with raw CSV, arrays,
logs, checks and per-case provenance. These are software regression fixtures,
not instructor-approved engineering benchmarks.

## Analytical check

For homogeneous nu=0 material, full-face pressure q and foundation Kz, zero
horizontal motion and uniform normal compression give, in the source frame:

`u=v=0; w(z)=q/Kz + q*(z_max-z)/E; szz=-q; ezz=-q/E`.

All other stress/strain components vanish. The nonuniform 18-node case matches
this solution with displacement tolerance `1e-10 mm + 1e-9*abs(expected)`, stress
absolute tolerance `1e-8 MPa`, and strain absolute tolerance `1e-12`. These units
assume the proposed mm–N–MPa convention. The zero-load case has exactly zero
force, displacement, stress and strain arrays.

## Convention decisions and unresolved acceptance

| Subject | Code-established behavior / development decision | External acceptance |
|---|---|---|
| Coordinates | Preserve raw X/Y/Z; loaded face is z_min, foundation face z_max. Source +Z points from load toward foundation. | Author confirmation of intended physical frame pending |
| Load direction | Positive q produces positive source-Z force; compression gives negative szz. | Use these observed semantics, not GetFEM's face labels, until frames are reconciled |
| Material/units | E and q must have consistent stress units; geometry uses one length unit. Surface K has stress/length units. Proposed mm–N–MPa implies N/mm³ foundation. | Author's actual unit declaration pending |
| Strain | `[exx,eyy,ezz,gamma_xy,gamma_yz,gamma_zx]`; source names last three exy/eyz/ezx. | Tensor off-diagonals require gamma/2; do not divide stress shear |
| Recovery | Arithmetic average of element-corner B/D evaluations | Match recovery/evaluation locations in any external comparison |
| Integration | Full integration only for the first supported backend | Reduced scale/stability findings require separate mathematical review |
| Zero load | Original driver permits an empty load list; a stable empty-RHS case passed | Production wrapper can normalize explicit zero-pressure patches to no contribution and record it |

No production formula correction or GUI integration has been made in C1.
The original source remains byte-for-byte intact, protected by source-hash
tests and `.gitattributes` rules preventing checkout newline conversion.

## Received-code defects now reproduced

- The inactive PyVista exporter writes all 15 scalar arrays, but actual saved
  grid coordinates do not align with solver-order tuples on the unequal-axis
  case. This was verified with PyVista/VTK, beyond the earlier source inspection.
- Malformed input `{}` prints an error and exits **zero** without an output
  CSV. This must become a nonzero production-contract failure in C4.
- Reduced solid stiffness equals the volume midpoint matrix divided by eight;
  reduced foundation equals its surface midpoint matrix divided by four.
  The supported single-brick reduced matrix has rank 9/24. Tests preserve these
  as known defects of received source, not as requirements for a corrected solver.
- The full element matches 2x2x2 quadrature and the expected six rigid modes
  without foundation. Full positive foundation makes the tested brick stable.

## GetFEM status

The supplied `gf-val.py` was not executed. The isolated Windows Python 3.12
environment has no GetFEM binding. The official
[GetFEM download page](https://www.getfem.org/download.html) lists an older
Windows Python 3.7 package and source-build alternatives; that is not the
current reference runtime. An existing Ubuntu WSL registration was inspected,
but starting it failed because `S:\Ubuntu\ext4.vhdx` is missing. No WSL repair,
new distribution or system-package installation was attempted.

The next external comparison still needs a working compatible GetFEM runtime
or an author-approved result pair. Verify GetFEM's actual RHS for its
`add_linear_term` pressure expression before trusting it, then align faces,
coordinate reflection, DOF order, shear conventions and recovery. The analytical
case above supplies an independent limited check; it is not a substitute for
the planned nonuniform full-material benchmark and mesh-convergence study.

## Reproduction

Create the reference environment with Python 3.12.2 and install the locked
versions in `packaging/c3d8-reference-lock.txt`. The principal requested
versions are also recorded in `packaging/c3d8-reference-requirements.txt`:
NumPy 1.26.3, SciPy 1.13.0, Matplotlib 3.8.3, pandas 2.2.3, PyVista 0.44.1,
VTK 9.3.1 and psutil 5.9.8. This environment is separate from system Python
and from the existing frozen plate solver. It is not an end-user package.

```powershell
python -m venv build/c3d8-reference-env
.\build\c3d8-reference-env\Scripts\python.exe -m pip install -r packaging/c3d8-reference-lock.txt
.\build\c3d8-reference-env\Scripts\python.exe packaging/qualify-c3d8-reference.py --output build/c3d8-reference-new-run
python tests/test_c3d8_reference.py
.\build\c3d8-reference-env\Scripts\python.exe tests/test_c3d8_reference_runtime.py
```

The qualification output must be a fresh directory. The fixture recorder
`packaging/record-c3d8-fixtures.py` checks successful status and source/artifact
hashes, and refuses an existing fixture destination. Ordinary tests never
regenerate or overwrite approved baseline files. The lock records exact package
versions; it is not a platform-independent wheel/hash lock.

`C3D8Reference` is part of normal CTest (NumPy only). Configure the optional
runtime suite with `-DC3D8_REFERENCE_PYTHON=<absolute-reference-python-path>`.
The core suite has seven tests; the real-runtime suite has three. GetFEM and
author acceptance are not silently counted as passing tests.

Reference verification: the Debug build completed and all five CTest suites passed
in 33.57 seconds: existing Integration, SolverContract and NumericalQualification,
plus C3D8Reference and C3D8ReferenceRuntime. Native-rendering and optional frozen
plate workflow checks were not repeated for this reference-only change.
