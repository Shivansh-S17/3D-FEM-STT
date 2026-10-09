# FEM Analysis GUI — Week 3

C++17/Qt Widgets desktop application with VTK visualization and separate Python C3D8/plate solver backends.

## Features

- Direct C3D8 creation and strict solver-input import; versioned project save/open and plate migration.
- Geometry generation/paste, material/foundation editing, pressure patches, validation and undo/redo.
- Exact C3D8 brick meshes, node/element inspection and loaded/foundation face identification.
- Background solving with progress, cancellation, immutable input snapshots and atomic result publication.
- Geometry/field validation plus manifest, artifact-hash and snapshot verification when results are opened or reopened.
- Fifteen solid scalar arrays, a displacement vector and derived magnitude. The field selector contains 16 scalar entries.
- Grid-aligned sections, deformation, extrema, CSV/PNG export and run history.

Full integration is supported. Reduced integration is rejected. Numerical software checks do not establish physical convergence or independent engineering acceptance; units, source-Z conventions, foundation interpretation and independent benchmark acceptance remain provisional.

See [implementation status](docs/IMPLEMENTATION_STATUS.md), [solid solver](docs/C3D8_C4_PRODUCTION_WRAPPER.md), [result profile](docs/C3D8_C5_RESULTS.md), [qualification](docs/C3D8_C8_QUALIFICATION.md) and the [plate contract](docs/SOLVER_PROCESS_CONTRACT.md).

## Dependencies and build

The Windows presets use MSVC 2022 x64, CMake 3.25 or newer, Ninja, Qt 6.11.2 and VTK 9.7.0 with Qt support. Tests require Qt Test and Python 3.12 with NumPy, SciPy and Matplotlib. The separate unchanged-reference runtime needs the dependencies in `packaging/c3d8-reference-lock.txt`.

The presets use `D:/Softwares/QT/6.11.2/msvc2022_64` and `D:/Softwares/VTK-9.7.0/install`; adjust paths for another machine. `build.ps1` initializes the configured MSVC toolchain under `C:/Program1` or `C:/Program` and adds Qt's Ninja directory.

```powershell
.\build.ps1 -Config Release
ctest --test-dir .\build\release --output-on-failure
```

A fresh build directory gives a clean build. The optional `-Clean` flag removes the selected generated build directory.

```powershell
.\build.ps1 -Config Debug
```

The Debug preset uses **RelWithDebInfo**, with symbols, optimization enabled and the release `/MD` runtime matching the installed VTK runtime.

## Running

```powershell
$env:PATH = "D:\Softwares\QT\6.11.2\msvc2022_64\bin;D:\Softwares\VTK-9.7.0\install\bin;$env:PATH"
.\build\release\FemGui.exe
```

Use New C3D8 Project or open the included Week 3 example. The five workflow stages guide model setup, material/foundation, loads, validation/run and results. F5 validates; Ctrl+R starts the analysis. Backend runtime paths are under Advanced.

## Test scope

Default CTest registers Integration, SolverContract, NumericalQualification, C3D8Reference, C3D8Production and C3D8Qualification. Optional original-driver and frozen-runtime checks require their corresponding interpreter/runtime configuration. Offscreen CTest skips native GUI methods.

```powershell
$env:QT_QPA_PLATFORM = 'windows'
$env:FEM_TEST_RENDER = '1'
.\build\release\FemGuiTests.exe nativeGuidedC3D8Workspace captureWeek3SolidResults nativeRenderingAndPicking nativeRectangleDrawing
```

These native methods exercise the guided setup, real solid results, retained plate rendering/picking and rectangle drawing. Source fixtures and analytical comparisons are bounded software evidence. The Week 3 evidence guide records actual executed checks and skips.

## Packaging and artifacts

```powershell
.\packaging\package-release.ps1
```

Packaging builds a shared `dist/solver/FemSolver/FemSolver.exe` runtime for both backends and deploys Qt/VTK/MSVC dependencies. Build requirements are recorded in `packaging/solver-build-requirements.txt`. Keep the entire `dist` folder together. The current Week 3 submission includes source and solved examples; a verified current-build distribution remains a separate handoff task.

Each run stores input JSON, project snapshot, GUI run metadata and stdout/stderr logs. Completed output is published under `output_python_square/` for both backends, containing copied input, VTK and a completion manifest. Plate input has four numerical sections; C3D8 input has five. Saved projects use a neighboring `runs` directory; unsaved projects use the application data directory.

Mesh construction and reopening saved results are synchronous and can delay the interface on large inputs. Solver execution and fresh-result import run separately from the GUI thread. Generated build, distribution and run directories are Git-ignored.
