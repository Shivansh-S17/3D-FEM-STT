# Week 3 build and test evidence

**Recorded on:** 8 October 2026 (IST)\
**Source:** the included `code/` files, identified by [`SOURCE_SHA256.csv`](SOURCE_SHA256.csv). See [`SOURCE_PROVENANCE.txt`](SOURCE_PROVENANCE.txt).\
**Environment:** Windows 11 x64, MSVC 19.44.35228, Qt 6.11.2, VTK 9.7.0, CMake 4.0.2, Python 3.12.2.

## Release build

From `code/`, run `.\build.ps1 -Config Release` with the documented Qt/VTK dependencies installed. A fresh Release build completed **33/33** steps. A subsequent legend-spacing adjustment rebuilt successfully; the final executables are `build/release/FemGui.exe` and `FemGuiTests.exe`.

The final application SHA-256 is:

```text
3896C73265C05B5386C0B20651B076F20E6CB1D659DE7ADF88E6926B28C8C924
```

See the [fresh build log](build-2026-10-08.log), [final rebuild log](rebuild-legend-2026-10-08.log), and [rendered build visual](../visuals/07-clean-build.png). Build output is generated and Git-ignored. The optional Vulkan-header discovery message is non-fatal; the application uses VTK's OpenGL rendering path.

## Automated suites

`ctest --test-dir .\build\release --output-on-failure` passed **6/6** registered suites with **0** failures: Integration, SolverContract, NumericalQualification, C3D8Reference, C3D8Production, and C3D8Qualification. Total time: **75.58 seconds**. See the [CTest log](ctest-2026-10-08.log) and [rendered visual](../visuals/08-ctest-results.png).

After the legend-spacing rebuild, the [Integration suite rerun](integration-final-2026-10-08.log) passed **1/1**. Its [JUnit record](integration-results-2026-10-08.xml) reports 72 test records, zero failures/errors and five skips: four desktop-rendering methods, executed separately below, and one optional frozen-package method. The optional reference-runtime suite is not configured in this environment. Current-build frozen-package validation remains pending.

Regression coverage includes saved plate/solid result tampering, archived-input and manifest checks, run-snapshot mismatch, rejection of malformed run history, and loaded-face pressure inspection.

## Native desktop checks and visual output

Set `FEM_TEST_RENDER=1`, `QT_QPA_PLATFORM=windows`, and put the installed Qt/VTK runtime directories on `PATH`. From `code/`, run:

```powershell
.\build\release\FemGuiTests.exe nativeGuidedC3D8Workspace captureWeek3SolidResults nativeRenderingAndPicking nativeRectangleDrawing -o ..\evidence\native-gui-2026-10-08.log,txt
```

All **4/4** named methods passed on the final build; QtTest reports **6 passed, 0 failed, 0 skipped**, including setup and cleanup. The solid capture uses the GUI controller's production solver, waits for verified completion, selects `szz`, and captures the window and viewport. See the [native log](native-gui-2026-10-08.log), [rendered visual](../visuals/09-native-gui-checks.png), and [result window](../visuals/04-c3d8-results-window.png).

The camera/display and result/deformation controls occupy separate rows at 1400 × 900. The legend title has explicit spacing above its upper tick. The unnamed-toolbar warning is resolved. One non-failing `QFont::setPointSize` warning remains in the guided-workspace check and is retained as a QA finding.

## Standalone solver output

The production C3D8 CLI completed a fresh one-brick run on 8 October with exit code 0:

```powershell
python -B solver/c3d8/c3d8_solver.py --input <absolute-input-json-path> --output-dir <new-absolute-output-directory> --events-jsonl
```

Its [input, VTK output and manifest](../examples/solved-single-brick/) record `Completed`, 8 nodes, 1 element, 24 DOFs and passed numerical software checks. See the [JSONL event stream](single-brick-run-2026-10-08.log). This example is demonstration output and is not added to the suite count. Engineering acceptance remains provisional.

The build/test PNGs are rendered from the linked text logs; application PNGs are native desktop captures. Earlier 7 October logs remain historical records and do not identify the current executable.
