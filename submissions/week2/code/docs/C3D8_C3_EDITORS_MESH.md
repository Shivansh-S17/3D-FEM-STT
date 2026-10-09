# C3: solid editors and exact mesh

**Completed on:** 13 September 2026\
**Depends on:** [C2 typed model and adapter boundary](C3D8_C2_MODEL_ADAPTER.md)

## Delivered workflow

The desktop application can now open a schema-2 `structured_solid_c3d8`
project and safely present it. The project panel switches its model pages by
the document's type:

- X, Y and Z coordinate tables edit the authoritative structured grid;
- Young's modulus and Poisson's ratio edit `SolidMaterialProperties`;
- thickness is read-only and derived from `zNodes.last() - zNodes.first()`;
- the mesh summary reports X/Y/Z counts, layers, derived thickness, minimum
  spacing, node/element/DOF totals and the face convention;
- Kx, Ky and Kz remain independently editable and the C3D8 editor rejects
  nonpositive values;
- pressure patch table, dialog, duplicate/delete, visibility and undo operate
  on the active typed model.

The model and mesh viewports accept either quads or bricks. For a solid,
pressure footprints and rectangle drawing use the z-min loaded face and the
Top preset looks at that face from outside the volume. Project summary, node
inspection and element inspection no longer assume a plate. Solid nodes show
`(i, j, k)`, X/Y/Z and their `(u,v,w)` DOF range. Solid elements show ordered
node IDs, three dimensions, volume, aspect ratio and loaded/foundation face
membership. Brick highlighting draws all 12 edges.

C3D8 execution is still disabled with a direct C4 message. Solid result import
and field presentation remain C5 work.

## Exact mesh contract

`C3D8MeshBuilder` builds VTK hexahedra in the received Python source order.
For `nx`, `ny`, `nz` and zero-based `(i,j,k)`:

```text
node(i,j,k) = j * (nx * nz) + k * nx + i
element(i,j,k) = j * ((nx-1) * (nz-1)) + k * (nx-1) + i
```

Each brick uses:

```text
[ (i,j,k), (i+1,j,k), (i+1,j+1,k), (i,j+1,k),
  (i,j,k+1), (i+1,j,k+1), (i+1,j+1,k+1), (i,j+1,k+1) ]
```

This is both the received `getNodesOfElement()` order and valid
`VTK_HEXAHEDRON` order. Every point has `solver_node_id`; every cell has
`solver_element_id`. The builder also records binary membership arrays:

| Association | Loaded z-min | Foundation z-max |
|---|---|---|
| Point | `loaded_face_node` | `foundation_face_node` |
| Cell touches face | `loaded_face_element` | `foundation_face_element` |

The focused mesh fixture uses unequal nonuniform axes, checks selected point
coordinates and full connectivity for separate Z layers, validates identity
arrays and checks both face labels. Invalid coordinate ordering yields an empty
preview grid rather than passing bad geometry to VTK.

## Verification

The Debug build passes all five configured suites: `Integration`,
`SolverContract`, `NumericalQualification`, `C3D8Reference`, and
`C3D8ReferenceRuntime`. The expanded Integration suite covers solid open,
editor application and rejection, volume viewport population, typed patch
editing, source-index/DOF inspection, face labels and 12-edge highlighting.
Existing plate workflows remain in the same suite.

Production execution, result import and independent engineering acceptance remain pending.
