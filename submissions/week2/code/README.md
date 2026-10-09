# FEM Analysis GUI — Week 2

C++17/Qt Widgets desktop application with VTK visualization for a structured C3D8 solid model and a retained plate backend.

## Current features

- Typed C3D8 and plate documents with schema-2 project save/open and schema-1 plate migration.
- Editable X/Y/Z coordinates, material, foundation and pressure patches, with validation and session undo/redo.
- C3D8 brick meshes in received-source order, explicit solver IDs, loaded/foundation face labels and node/element inspection.
- A working plate analysis process, plate result views and CSV/PNG reports.
- Received C3D8 reference fixtures and source-integrity checks.

C3D8 production execution and computed solid results are pending. Run Analysis is disabled for solid projects. New Project starts a plate; to inspect a solid, open the Week 2 example project through File > Open Project. Reduced integration is stored but rejected by validation.

See [implementation status](docs/IMPLEMENTATION_STATUS.md), [solid editors and mesh](docs/C3D8_C3_EDITORS_MESH.md), [reference baseline](docs/C3D8_BASELINE.md) and the [plate process contract](docs/SOLVER_PROCESS_CONTRACT.md).

## Build dependencies

The supplied presets target Windows x64 with MSVC 2022, CMake 3.25 or newer, Ninja, Qt 6.11.2 and VTK 9.7.0 built with Qt support. Test builds also require Qt Test and Python with NumPy, SciPy and Matplotlib. Running the unchanged C3D8 reference driver requires the separate dependencies in `packaging/c3d8-reference-lock.txt`.

The checked-in presets use `D:/Softwares/QT/6.11.2/msvc2022_64` and `D:/Softwares/VTK-9.7.0/install`. Adjust these paths for another machine. `build.ps1` uses the configured `C:/Program1` or `C:/Program` MSVC toolchain and Qt Ninja installation.

```powershell
.\build.ps1 -Config Release
ctest --test-dir .\build\release --output-on-failure
```

A fresh build directory gives a clean build. `-Clean` removes the selected generated build directory before configuration.

For development with symbols:

```powershell
.\build.ps1 -Config Debug
```

The Debug preset uses **RelWithDebInfo**, with the release `/MD` runtime and optimization enabled, to match the installed VTK runtime. It is not an unoptimized MSVC Debug build.

## Run the application

```powershell
$env:PATH = "D:\Softwares\QT\6.11.2\msvc2022_64\bin;D:\Softwares\VTK-9.7.0\install\bin;$env:PATH"
.\build\release\FemGui.exe
```

Raw build output needs Qt/VTK DLLs on PATH. Packaging puts the required runtime files beside the executable.

## Tests and evidence

The default CTest configuration registers Integration, SolverContract, NumericalQualification and C3D8Reference. The optional C3D8ReferenceRuntime suite requires `C3D8_REFERENCE_PYTHON` pointing to the isolated reference interpreter. Native rendering/drawing and a frozen-solver workflow are opt-in checks and are skipped by normal offscreen CTest runs.

```powershell
$env:QT_QPA_PLATFORM = 'windows'
$env:FEM_TEST_RENDER = '1'
.\build\release\FemGuiTests.exe nativeRenderingAndPicking nativeRectangleDrawing
```

Reference agreement, equilibrium and analytical checks provide bounded software evidence. Author-approved conventions, independent benchmarks and engineering acceptance remain open. See [numerical qualification](docs/NUMERICAL_QUALIFICATION.md).

## Packaging

```powershell
.\packaging\package-release.ps1
```

Packaging builds Release, freezes the **plate** solver with PyInstaller, deploys Qt/VTK/MSVC runtime files and preserves an existing package in a uniquely named backup. Build dependencies are recorded in `packaging/solver-build-requirements.txt`. Keep the whole generated `dist` folder together. This version does not package a production C3D8 solver.

## Run artifacts and limitations

Plate runs store input JSON, an immutable project snapshot, run metadata and stdout/stderr logs in a fresh run directory. Successful output includes VTK results and a result manifest. Saved projects use a neighboring `runs` directory; unsaved projects use the application's data directory.

Mesh construction and reopening saved results currently execute on the GUI thread. Large models/files can delay the interface. C3D8 previews are limited to one million nodes; validation reports this limit. These limitations do not affect the small example project.
