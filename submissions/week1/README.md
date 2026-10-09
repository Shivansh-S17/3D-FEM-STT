# Week 1 — C3D8 input preview

**Submission date:** 25 September 2026

Week 1 establishes the C++/Qt/VTK desktop interface, read-only C3D8 input pages and a structured slab preview. The Results tab is a placeholder; computed fields are not available in this version.

| Artifact | Purpose |
|---|---|
| [Progress report](WEEK1_REPORT.md) | Tasks, contributions, meetings, build and QA results |
| [Source and build instructions](code/README.md) | C++17 application and Windows build setup |
| [Build and test evidence](evidence/BUILD_AND_TEST.md) | Recorded build, packaging and launch checks |
| [Input example](code/examples/received-c3d8-input.json) | Received C3D8 JSON fields |
| [Application screenshot](visuals/screenshots/Latest_Dist.jpg) | Native application view |
| [Build output](visuals/screenshots/build-evidence.png) | Rendered build transcript |
| [Packaging output](visuals/screenshots/distribution-evidence.png) | Rendered packaging transcript |

Build and packaging commands are in the source README. Generated `code/build/` and `code/dist/` directories are excluded from version control; packaging creates `code/dist/FemGui.exe` and its required runtime files.
