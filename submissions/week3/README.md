# Week 3 â€” C3D8 analysis and results

**Submission date:** 8 October 2026

Week 3 connects the editable solid model to a production solver, validates VTK results and provides a guided workflow for inspection and export. C3D8 engineering acceptance remains provisional.

| Artifact | Purpose |
|---|---|
| [Progress report](WEEK3_REPORT.md) | Tasks, contributions, meeting record, build and QA results |
| [Source and instructions](code/README.md) | C++/Qt/VTK application and Python backends |
| [Source manifest](evidence/SOURCE_SHA256.csv) | File-by-file SHA-256 record |
| [Examples](examples/README.md) | Saved GUI project, solver input and solved one-brick VTK output |
| [Build/test evidence](evidence/BUILD_AND_TEST.md) | Commands, recorded outcomes and test scope |
| [Progress diagram](visuals/00-progress-map.svg) | Weekly feature progression |
| [Guided workflow](visuals/01-guided-workflow.png) | Native model setup view |
| [Geometry editor](visuals/02-geometry-editor.png) | Native coordinate editing view |
| [Pressure patch](visuals/03-load-patch-rotated.png) | Native load view |
| [C3D8 results](visuals/04-c3d8-results-window.png) | Completed solid analysis in the GUI |
| [Result field](visuals/05-c3d8-result-field.png) | Native result viewport |
| [Plate regression](visuals/06-plate-results-regression.png) | Retained plate result workflow |

## Build and run

The [source README](code/README.md) lists dependency paths and commands. From `code/`:

```powershell
.\build.ps1 -Config Release
ctest --test-dir .\build\release --output-on-failure
```

Add the Qt/VTK runtime directories to PATH before launching `build/release/FemGui.exe`. Open the [example project](examples/demo-c3d8.femproject.json), validate and run it. Use the source manifest to verify the included files.

Generated build, distribution and run directories are excluded from version control. User feedback, independent physical comparison and a current-build package handoff remain pending.
