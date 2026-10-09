# Week 2 build and test evidence

**Date:** 1 October 2026 (IST)\
**Source:** Week 2 `code/`, including the native visual capture test\
**Build environment:** Windows 11, MSVC 19.44.35228, Qt 6.11.2, VTK 9.7.0, CMake 4.0.2 and Python 3.12.2

## Clean Release build

From `submissions/week2/code`, using a fresh build directory:

```powershell
.\build.ps1 -Config Release
```

Observed: successful CMake configure and **31/31** Ninja build steps. The command produced `code/build/release/FemGui.exe` and `FemGuiTests.exe`. The application executable SHA-256 was `1313FB697EE3758A0E8F8F77410CAAF99A60A907C16EF880077446CD319AB960`. Exact output: `build-2026-10-01.log`.

## Automated suites

```powershell
ctest --test-dir .\build\release --output-on-failure
```

Observed: **4/4** CTest suites passed, **0** unsuccessful: Integration, SolverContract, NumericalQualification and C3D8Reference. Exact output: `ctest-2026-10-01.log`.

The optional `C3D8ReferenceRuntime` suite was not configured in the recorded Release configuration, so it is not included in the reported count. The CTest software regression results do not substitute for independent physical validation.

## Native example and visual check

`capture-native-visuals.ps1` launched the Release test executable with the Windows Qt platform, opened `examples/demo-c3d8.femproject.json` through `ProjectController`, confirmed it is a solid project and saved five native application views. The focused QtTest run passed with **0 failures**. Exact output: `native-visual-capture.log`.

The standalone GUI capture check is counted once, alongside the four CTest suites, for **5 executed checks and 0 unsuccessful**. PNG images `06-clean-build.png` and `07-ctest-results.png` are rendered from the exact text logs; they are labeled as recorded output and are not live terminal screenshots.

## Integration detail

The included [QtTest JUnit report](integration-results-2026-10-01.xml) records 62 records, zero failures/errors and 4 opt-in skips: `captureNativeWeek2Visuals`, `bundledSolverWorkflow`, `nativeRectangleDrawing`, `nativeRenderingAndPicking`. The capture case was subsequently run separately with the Windows platform. Original-driver runtime, native drawing/picking and frozen plate-solver checks are not counted as executed in the four-suite run.
