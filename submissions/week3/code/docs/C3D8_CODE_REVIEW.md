# C3D8 source intake review

Date: 12 September 2026. Scope: supplied source, existing GUI
coupling, syntax checks and small numerical probes. No supplied solver files
or application implementation were changed. The current implementation status
is [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md).

## Conclusion

This is a different finite-element formulation, not a revision of the plate
driver. It solves a structured three-dimensional elastic solid with eight-node
bricks and three translations per node. Retain the desktop shell and process
architecture; add a separately identified C3D8 model/backend. Existing plate
results and tests remain plate results and tests, not C3D8 acceptance evidence.

## Supplied files

| File | Role |
|---|---|
| `C3D8v/python/C3D8-slab.py` | Mesh enumeration, JSON readers, volume/foundation assembly, rectangular surface loads, sparse solve, nodal stress/strain recovery and exporters |
| `C3D8v/python/C3D8local.py` | Generated 24x24 full/reduced solid and foundation matrices, 24-entry force vector, 6x24 B matrix and 6x6 isotropic constitutive matrix |
| `C3D8v/python/inputs.json` | Full-integration example: 21x21x5 nodes, 2205 nodes, 1600 elements, 6615 DOFs; full-domain pressure with resultant 9,800,000 in consistent force units |
| `C3D8v/python/gf-val.py` | Hardcoded GetFEM comparison candidate using the example's dimensions/material/foundation, with its own coordinate and load conventions |
| `C3D8v/python/WxMaxima/` | Three symbolic notebooks, generated matrix/vector text, and an index-conversion shell script; retain as derivation/provenance sources |

The directory contains no saved C3D8/GetFEM result pair, numerical acceptance
tolerances, or dependency lockfile. The notebooks were inventoried and hashed;
their symbolic derivations were not independently re-executed.

## Material differences

| Area | Existing plate backend | Supplied C3D8 backend |
|---|---|---|
| Geometry | X/Y Cartesian grid on Z=0; scalar thickness | X/Y/Z Cartesian grid; thickness and layers follow Z coordinates |
| Element | Four-node rectangle, 20 DOFs | Eight-node axis-aligned brick, 24 DOFs; not arbitrary distorted hexahedra |
| DOFs | `[w, theta_x, theta_y, u, v]` | `[u, v, w]` at each node |
| Enumeration | X then Y | X then Z then Y |
| Input material | `slab.Emod`, `slab.nu`, `slab.t` | `material.Emod`, `material.nu`; no independent `t` |
| Input loads | Five synchronized arrays | List of `{q,x1,x2,y1,y2}` objects |
| Foundation | Part of the plate element formulation | Separate surface matrix on the last Z layer's high-Z face |
| Pressure | Plate rectangle | First Z layer's low-Z face, positive raw Z force |
| Analysis | Five equal static increments; dense default | One static sparse `spsolve`; full/reduced option changes both solid and foundation integration |
| Recovered fields | Translations, rotations, surface bending and membrane stresses | Translations, six total solid stress components and six strain components |
| Active output | Validated quad VTK, manifest, logs, summaries | `output-c3d8.csv`: coordinates, translations and three normal stresses only |
| Dormant output | — | Rectilinear VTK writer with 15 scalars, no displacement vector or explicit solver IDs; call is commented out |
| Process contract | Strict CLI, JSONL events, failure/cancel handling, atomic artifacts | Positional input, plain prints, fixed output filename, no manifest or cooperative cancellation |
| Runtime | Bundled NumPy/SciPy/Matplotlib | Original driver additionally imports PyVista at startup and pandas for CSV; GetFEM is a separate comparison dependency |

The new source is linear static elasticity. It does not provide contact,
plasticity, transient frames, arbitrary supports, unstructured mesh import,
per-element materials, reactions, or hourglass stabilization.

## Findings requiring action

**1. Current integration cannot run or import it.** The original entry point
fails here with `ModuleNotFoundError: No module named 'pyvista'`. NumPy, SciPy
and pandas are available; Python VTK, PyVista and GetFEM are absent. Native
C++ VTK DLLs do not supply Python VTK. The active export call is
[`C3D8-slab.py:496`](../C3D8v/python/C3D8-slab.py#L496), and the VTK call just
above it is disabled. This is a reproducible environment/output incompatibility,
not evidence of a completed or failed numerical solution.

**2. Reduced integration needs mathematical review.** Across sizes
`(175,175,50)`, `(170,280,63)` and `(1,2,3)`, the supplied `KReducedlocal`
equals `B(center).T * D * B(center) * volume / 8` to rounding precision.
The supplied `KsReducedlocal` similarly equals the midpoint surface matrix
divided by four. This conflicts with volume/area-scaled midpoint integration
of the same B/D and face interpolation. The extra factors are visible in
[`C3D8local.py:1396`](../C3D8v/python/C3D8local.py#L1396) and
[`C3D8local.py:1978`](../C3D8v/python/C3D8local.py#L1978). Confirm the symbolic
weights before correcting a working copy; preserve the original formulas.

The full solid element has rank 18/24 (six rigid modes) in the probes; reduced
solid has rank 6/24. Adding the matching positive foundation gives full rank
24/24 for full integration but only 9/24 for reduced integration. A 3x3x3-node
assembly gives full rank 81/81 versus reduced rank 58/81. Rank uses eigenvalue
threshold `1e-10 * max(abs(eigenvalues))`. No stabilization is assembled in
the driver. Correcting scale alone would not eliminate these zero-energy
modes. Keep reduced integration disabled in the first supported release.

**3. Full integration passed bounded algebraic checks.** Generated full
stiffness matched 2x2x2 integration of the supplied B/D at relative maximum
entry error below `5e-16` for the three shapes. Matrices were symmetric and
had no negative eigenvalues beyond the rank threshold. A cut-patch force
summed to `q*area`. These are useful consistency checks, not independent
physical validation or exhaustive verification of every generated expression.

**4. Top/bottom and sign conventions differ.** C3D8 loads local nodes 0–3
at the low-Z boundary and places foundation on local nodes 4–7 at the high-Z
boundary. Positive `q` produces positive raw `w` force. `gf-val.py:47–48`
selects top by outward +Z and bottom by outward −Z, and declares `q=-0.8`.
Thus row-by-row or same-coordinate comparison is not meaningful without
reconciling the physical coordinate frame. A Z reflection requires transforming
vectors and stress/strain tensors, not just negating a plotted displacement.

**5. Dormant VTK geometry and result order disagree.** The mesh's ID formula
is `ix + nx*iz + nx*nz*iy`. `export_to_vtk` instead constructs
`RectilinearGrid(ynodes,xnodes,znodes)` and assigns solver-order arrays directly.
PyVista treats those arguments as X/Y/Z; VTK orders points with its first
index fastest, then second, then third. The unequal-axis probes demonstrate
coordinate/tuple mismatches. This conclusion uses the supplied code and
[PyVista's argument definition](https://docs.pyvista.org/api/core/_autosummary/pyvista.RectilinearGrid.html)
and [VTK's documented ordering](https://vtk.org/doc/nightly/html/classvtkRectilinearGrid.html).
Do not enable this writer as-is. Prefer explicit hexahedral connectivity in
solver order, or a fully tested rectilinear permutation.

**6. Strain shear labels need interpretation.** The B matrix produces
engineering shear strains. An affine-field probe returned
`[2,5,11,3,7,13]` for normal strains and summed cross derivatives. Thus exported
`exy/eyz/ezx` are gamma components, while GetFEM computes tensor strain with
`(Grad_u+Grad_u.T)/2`. Divide engineering shear by two when forming a strain
tensor; stress shear does not receive that factor. Both C3D8 stress and strain
are arithmetic averages of element-corner evaluations, not integration-point
values or plate top/bottom bending components.

**7. Input and failure handling are prototype-level.** Reader exceptions
call bare `exit()`, which can signal successful process exit. Assertions handle
some load checks, including `q>0`, but strict coordinate/material/foundation/
options validation is missing. `getRectangleBoundsInElement` checks X overlap
twice instead of checking Y (`C3D8-slab.py:214`). Out-of-domain rectangle indexing
can select unintended elements. Singular-warning escalation, finite-array
validation, cancellation and atomic publication must be added to the wrapper.
Individual zero-pressure patches are currently rejected; an empty load list
instead assembles a zero vector. Define and test the production policy explicitly.

**8. GetFEM is a candidate reference, not an accepted oracle.** It hardcodes
the case, executes at import, assumes reshaped DOFs align with mesh point order,
and exports only translations and normal stresses. It uses `add_linear_term`
for a constant pressure expression; the current
[GetFEM API documentation](https://getfem.org/python/cmdref_Model.html)
distinguishes matrix terms from source terms. Verify its assembled nonzero RHS,
resultant and solve before trusting it; no GetFEM runtime was available to
confirm that behavior here. Align loading faces, load sign, shear convention,
node positions and recovery methods before numerical comparisons.

## Impact on the actual application

The planned adapter abstraction is still plate-typed in today's code:
`ISolverAdapter` accepts `PlateDefinition`; `AnalysisController` owns a
`PlateSolverAdapter`; `ProjectDocument`, undo commands and `RunRecord` snapshots
contain plates. `ProjectSerializer` accepts schema 1 and a fixed backend/model
identity. `ResultImporter` requires quads and the plate field catalog. The
viewport, inspector and reporting code also consult that catalog.

Consequently, replacing a Python path or extending one input serializer is
insufficient. Generalize these specific boundaries for two typed model kinds,
retain old-project loading, add a hex mesh/result profile, and propagate backend
identity through snapshots and provenance. The old 1089-node measured warning
and dense 1-GiB allocation rule must remain plate-specific.

## Reproduction and limits

Run `python -B docs/review/c3d8_intake_probe.py`. The probe reads original
element functions and extracts driver definitions with AST to avoid its
unavailable plotting dependency and main execution. It assembles only small
matrices, with at most 81 global DOFs. The only original CLI attempt stopped
at the missing import. It never claims to have run the supplied 6615-DOF solve
or the GetFEM comparison. Results, input counts and original-file SHA-256 hashes
are in [C3D8_INTAKE_AUDIT.json](C3D8_INTAKE_AUDIT.json).
