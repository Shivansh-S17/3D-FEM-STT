# C3D8 C7 — solid interaction and reports

This technical phase record describes its own implementation and verification scope. See [current implementation status](IMPLEMENTATION_STATUS.md) for the Week 3 capabilities and build/package qualification.


**Status:** complete

Solid result views now provide a Section selector with the complete volume and
every solver Z-node layer. Loaded and foundation faces are named, and internal
layers can be exposed without inventing display-only solver entities.

Each grid-aligned section:

- uses original solver nodes and their complete point-result arrays;
- carries `solver_element_id` and `section_source_element_id` on every face;
- assigns an internal face to the source volume cell above the plane (the
  foundation face uses the cell below);
- preserves IDs and fields through deformation;
- states its Z coordinate, boundary context, ownership rule and that no
  interpolation is used.

This deliberately avoids labeling interpolated cut points as solver nodes.
Node and element picks on the section resolve back to the full run mesh, so the
inspector continues to show source IDs, coordinates, connectivity, DOFs and
loaded/foundation membership.

The existing solid pressure overlay is placed outside the source-loaded z-min
face. Patch selection, visibility and Undo retain their existing UUID and model
revision rules; display-only visibility does not stale results.

Nodal CSV exports now include backend ID, model kind, coordinate-frame and
boundary convention, integration rule, engineering-shear convention, recovery
rule and source-result path in addition to run/revision/stale state, source node
IDs, coordinates, units and every scalar field. PNG reports add backend/model
identity, source frame, full-volume or section context, deformation scale,
association, units and field averaging semantics.

Automated coverage uses an asymmetric, nonuniform, multilayer solid. It checks
the internal section’s exact Z geometry, source element IDs, node and element
selection, deformed-section ID preservation, full-volume reset, solid CSV
metadata, loaded-face overlay direction, visibility and Undo behavior.

Next phase: C8 numerical and performance qualification. Its independent
physical-reference and author-convention gates remain open.
