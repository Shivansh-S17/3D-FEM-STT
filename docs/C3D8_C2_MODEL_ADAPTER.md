# C2: typed model and adapter boundaries

This technical phase record describes its own implementation and verification scope. See [current implementation status](IMPLEMENTATION_STATUS.md) for the Week 3 capabilities and build/package qualification.


**Status:** implemented
**Scope:** data and backend boundaries only; solid editors, volume rendering,
production execution and result import remain in C3-C6.

## Delivered boundary

The project core now stores a `ModelDefinition`, which is exactly one of:

| Model kind | Backend | Definition | Topology |
|---|---|---|---|
| `rectangular_plate_5dof` | `plate-python-v1` | `PlateDefinition` | 4-node quad, 5 DOFs/node |
| `structured_solid_c3d8` | `c3d8-python-v1` | `SolidDefinition` | 8-node brick, 3 DOFs/node |

`ProjectDocument`, undo commands and `RunRecord::inputSnapshot` preserve that
type. A plate callback cannot mutate a solid and a solid callback cannot mutate
a plate. Physical edits advance the model revision; patch name and visibility
edits remain undoable without making numerical results stale.

`SolverAdapterRegistry` selects an adapter by model kind. Both adapters reject
the other model type. The C3D8 adapter validates ordered finite X/Y/Z axes,
material and foundation ranges, load bounds, the recorded coordinate
convention and full integration. It emits the received solver's exact five
sections: `nodes`, `material`, `subgrade`, `loads`, and `options`. Disabled and
zero-pressure patches do not become solver load rows.

Reduced integration remains stored so an incoming project is represented
faithfully, but validation rejects it because C1 found scaling and rank defects
in the received implementation.

## Project schema 2

Schema 2 replaces the plate-only root with a typed model:

```json
{
  "schemaVersion": 2,
  "modelKind": "structured_solid_c3d8",
  "solver": { "backendId": "c3d8-python-v1" },
  "model": {
    "xNodes": [0, 500, 1000],
    "yNodes": [0, 500, 1000],
    "zNodes": [0, 150, 300],
    "material": { "Emod": 24000, "nu": 0.2 },
    "foundation": { "Kx": 1, "Ky": 2, "Kz": 3 },
    "patches": [],
    "reducedIntegration": false,
    "coordinateConvention": "source-z-positive-toward-foundation"
  }
}
```

Run snapshots use the same identity tuple and typed model. Loading rejects an
unknown or mismatched model/backend pair, including a mismatched saved run,
before changing the in-memory document. Loading a schema-1 plate project keeps
it as a plate and infers plate identities for its old snapshots. Its next save
writes schema 2; it is never reinterpreted as a solid.

At the C2 checkpoint, the schema serializer could load and save solid projects
while `ProjectController` declined to present one to the plate-only desktop UI.
C3 removed that temporary gate after adding solid-safe editors, mesh viewports
and inspection. Direct solid solver-input export remains covered by tests.

## Verification

The C2 integration tests cover:

- exact C3D8 JSON and patch-row provenance;
- wrong-model, reduced-integration, coordinate, foundation and load rejection;
- registry capability identities;
- typed solid revisions and undo/redo;
- solid model and immutable run-snapshot schema-2 round trips;
- transactional rejection of mismatched run/backend data and result ownership;
- schema-1 plate load followed by schema-2 save without reinterpretation;
- preservation of every existing plate workflow.

The Debug build and all five configured CTest suites passed after this phase:
`Integration`, `SolverContract`, `NumericalQualification`, `C3D8Reference`, and
`C3D8ReferenceRuntime`. C3D8 reference tests establish software-regression
agreement with the received full-integration code. They do not close the open
author-convention or independent physical-validation gates.
