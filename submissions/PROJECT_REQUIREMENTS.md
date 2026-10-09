# Project Requirements — FEM Analysis GUI

**Project:** GUI for a three-dimensional finite element analysis program\
**Target platform:** Windows 10/11, x86-64

## 1. Objective

Provide a desktop interface for defining a structured C3D8 solid, preparing the Python solver input, running an analysis and inspecting its results. The interface should expose the solver's supported inputs and explain validation failures before a run begins.

## 2. Supported input model

- Ordered X, Y and Z coordinate arrays defining a structured grid of eight-node bricks.
- Homogeneous isotropic material: Young's modulus `Emod` and Poisson's ratio `nu`.
- Foundation stiffness values `Kx`, `Ky` and `Kz`.
- Rectangular pressure patches: `q`, `x1`, `x2`, `y1` and `y2`.
- A stored integration option. Reduced integration requires numerical review before execution can be supported.

The application must preserve coordinate precision, solver node/element ordering and the distinction between project metadata and numerical inputs. Arbitrary supports, unstructured meshes, multiple materials, contact and transient analysis are outside the received solver's scope.

## 3. Functional requirements

| ID | Requirement |
|---|---|
| FR-01 | Display the supported input groups in a navigable desktop interface. |
| FR-02 | Create or open a project and edit coordinates, material, foundation and pressure patches. |
| FR-03 | Validate finite values, ordered axes, material/foundation ranges and pressure bounds. |
| FR-04 | Save and reopen a versioned project without reinterpreting its model/backend identity. |
| FR-05 | Export the received solver's JSON input schema. |
| FR-06 | Display a deterministic C3D8 mesh with original node and element IDs. |
| FR-07 | Inspect node coordinates, translation DOFs, brick connectivity, dimensions and loaded/foundation face membership. |
| FR-08 | Run the solver in a separate process, with progress, logs, cancellation and clear failure handling. |
| FR-09 | Import displacement, stress and strain fields with geometry and provenance validation. |
| FR-10 | Display scalar results, deformation and a legend, and export useful reports. |
| FR-11 | Prevent model changes during a run and identify results from a previous model revision. |
| FR-12 | Preserve the window layout and provide a layout reset action. |

## 4. Quality requirements

- **Responsiveness:** solver execution must stay off the GUI thread. Mesh construction and result loading should be moved to workers where their cost warrants it.
- **Data integrity:** edits must use the authoritative project model; failed loads must leave the current project intact.
- **Reliability:** process failures must be reported without losing the project, and cancelled or incomplete output must not be attached as successful results.
- **Maintainability:** keep presentation, controllers, domain data, solver adapters and file services separate.
- **Numerical evidence:** distinguish regression agreement with received source from independent physical validation. Record unsupported integration paths and unresolved conventions.
- **Deployment:** document dependencies and provide reproducible Windows build and packaging commands.

## 5. Milestone acceptance

| Milestone | Acceptance scope |
|---|---|
| Week 1 | Desktop shell, read-only C3D8 input pages, structured preview and launch evidence. |
| Week 2 | Editable typed solid, project save/open, exact brick meshing, inspection and regression evidence. |
| Subsequent integration | Production C3D8 process contract, computed solid results, complete analysis workflow and engineering qualification. |

Weekly reports record the implemented subset, executed checks and remaining defects. A successful build or regression run does not by itself establish engineering accuracy.
