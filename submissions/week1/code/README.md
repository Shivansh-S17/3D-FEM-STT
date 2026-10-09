# FEM Analysis GUI — Week 1

The Week 1 application provides a C++17/Qt Widgets desktop interface with an embedded VTK viewport.

## Features

- Read-only pages for X/Y/Z coordinates, Emod/nu, Kx/Ky/Kz, pressure rectangles and the integration option.
- A structured slab preview with 27 nodes, eight C3D8 bricks and 81 translation DOFs.
- Model, Mesh and Results tabs, camera presets, display modes, axes, inspector and log panels.
- Window and splitter layout persistence.

The Results tab displays preview geometry only. Project editing, save/open, picking, solver execution and computed result fields are not implemented in this version. Run Analysis is disabled.

The received input example is [examples/received-c3d8-input.json](examples/received-c3d8-input.json). Preview geometry uses a subset of its coordinate values.

## Build and package

The supplied Windows presets use MSVC 2022 x64, Qt 6.11.2 and VTK 9.7.0 with Qt support. CMake 3.25 or newer and Ninja must be available. Configure the dependency paths in `CMakePresets.json` and the toolchain paths in `build.ps1` for your machine.

```powershell
.\build.ps1 -Config Release
.\packaging\package-release.ps1
```

Packaging produces `dist/FemGui.exe` with Qt, VTK and MSVC runtime files. Keep the entire `dist` folder together. Build and distribution files are generated and Git-ignored.

See the [build and launch evidence](../evidence/BUILD_AND_TEST.md) for recorded commands and results. Week 1 has no registered CTest suites.
