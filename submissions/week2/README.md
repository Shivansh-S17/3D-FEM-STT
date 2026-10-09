# Week 2 — Editable C3D8 model and brick mesh

**Submission date:** 1 October 2026

Week 2 adds an editable C3D8 model, schema-2 project save/open, pressure-patch editing, exact brick connectivity and node/element inspection. C3D8 production execution and computed solid result viewing remain pending in this version.

| Artifact | Purpose |
|---|---|
| [Progress report](WEEK2_REPORT.md) | Tasks, contributions, meetings, build and QA results |
| [Source and build instructions](code/README.md) | Application source, dependencies and commands |
| [Example project](examples/demo-c3d8.femproject.json) | Eight-brick C3D8 project for editor and inspection checks |
| [Build and test evidence](evidence/BUILD_AND_TEST.md) | Recorded Release build, CTest and native GUI checks |
| [Progress diagram](visuals/01-progress-map.svg) | Features added since Week 1 |
| [Coordinate editor](visuals/01-coordinate-editor.png) | Native coordinate view |
| [Material editor](visuals/02-material-editor.png) | Native material view |
| [Brick mesh](visuals/03-brick-mesh.png) | Native mesh view |
| [Foundation editor](visuals/04-foundation-editor.png) | Native foundation view |
| [Pressure patches](visuals/05-pressure-patches.png) | Native load view |
| [Build output](visuals/06-clean-build.png) | Rendered build transcript |
| [CTest output](visuals/07-ctest-results.png) | Rendered test transcript |

`evidence/capture-native-visuals.ps1` runs the focused GUI capture test against the example project. Generated build, package and run directories are excluded from version control.
