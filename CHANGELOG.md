# Changelog

## Week 3 — 8 October 2026

- Connected the C3D8 model to a production solver process with validation, progress, cancellation, result hashes and atomic VTK publication.
- Added solid result import, field selection, sections, deformation, extrema, CSV/PNG export and run-history ownership.
- Added a guided C3D8 workflow, geometry editor, load interaction and results dashboard.
- Recorded a fresh Release build, six passing CTest suites, four passing native GUI methods and a runnable one-brick VTK example.
- Corrected reopened-result integrity checks, loaded-face pressure inspection, malformed history handling, result status wording and desktop control/legend layout.
- Removed internal implementation plans and updated submission links and source hashes.
- Kept C3D8 physical acceptance provisional; user feedback and UI polish remain for Week 4 and the formal test phases.


## Week 2 — 1 October 2026

- Added typed C3D8 project data and backend identity, with versioned save/open and plate-project migration.
- Connected coordinate, material, foundation and pressure editors to validation, revisions and undo/redo.
- Added exact brick connectivity, solver IDs, loaded/foundation face labels and solid inspection.
- Recorded received-source reference fixtures and software regression checks.
- Corrected pressure inspection so only bricks touching the loaded face report applied pressure.
- Added saved-result manifest/hash verification, malformed run-history rejection and a clear preview-size validation error.
- Improved numeric display while preserving exact double round trips.
- Made Windows compiler selection explicit and preserved the MSVC environment when PATH appears with different casing.
- Corrected Week 1 preview descriptions, packaging safeguards and submission documentation.

C3D8 production execution, computed solid result import and independent engineering acceptance remain pending in this submitted version.

## Week 1 — 25 September 2026

- Established the C++17/Qt/VTK desktop application and Windows build/package scripts.
- Added read-only C3D8 input pages and a structured preview with 27 nodes and eight bricks.
- Added camera/display controls, navigation, inspector and diagnostic panels.
- Recorded the Release build, packaging output and two launch smoke checks.

The Results tab displays preview geometry. Project editing/persistence and solver execution are not available in the Week 1 version.
