# Week 1 build and test evidence

**Date/time:** 25 September 2026, clean build and package capture at approximately 23:35–23:38 IST (`UTC+05:30`)\
**Source:** Week 1 `code/`, with C3D8 input-preview panels, slab mesh and `C:\Program1` build-tool fallbacks\
**Platform:** Windows, MSVC 19.44.35228, Qt 6.11.2, VTK 9.7.0, CMake 4.0.2, Ninja

## Original build — 25 September 2026

From `submissions/week1/code`:

```powershell
.\build.ps1 -Config Release
```

Observed: CMake configured and generated successfully; Ninja completed **10/10 clean build steps** and linked `FemGui.exe`. Exit code `0`. The recorded executable's SHA-256 was:

```text
84813DDEB65C988A1946EC75646AD574899482F63B3C9373AB15B0E0DAFFC717
```

The build script includes a `C:\Program1` Visual Studio path fallback for the recorded development setup.

## Standalone package

From `submissions/week1/code`:

```powershell
.\packaging\package-release.ps1
```

Observed: package creation completed and produced `code/dist/FemGui.exe` with Qt, VTK and MSVC runtime DLLs. The packaging script also accepts the installed `C:\Program1` redistributable location. The generated package is Git-ignored; run the packaging command to recreate it.

The exact output is saved as `build-2026-09-25.log` and `package-2026-09-25.log`. `visuals/screenshots/build-evidence.png` and `visuals/screenshots/distribution-evidence.png` are rendered images of those logs for readable presentation. They are not photos of a live terminal window. Both PNGs and both source logs are in the packet.

The versioned `code/examples/received-c3d8-input.json` contains the same JSON values as the received input. Its SHA-256 is `7F5C133638D2DC031E6881C9F258E69FF6FA84548D8F2E265823758841422954`; line endings differ from the reference source. Its fields informed the Week 1 panel labels. The preview coordinates are a smaller subset of the supplied arrays.

## Tests

Run from `submissions/week1`:

```powershell
.\evidence\smoke-launch.ps1
```

The launch smoke check sets local process environment paths for Qt/VTK, starts the Release executable with `QT_QPA_PLATFORM=offscreen`, waits three seconds and checks that the process remains alive. Observed: **passed**. The script stops its own process afterward.

Run the package check from `submissions/week1`:

```powershell
.\evidence\smoke-package.ps1
```

It starts `code/dist/FemGui.exe` with only Windows and package paths available. Observed: **passed**, the application stayed running for four seconds. **Total executed: 2; unsuccessful: 0.**

No automated unit, integration or numerical tests were part of this Week 1 source snapshot. The actual application view supplied under `visuals/screenshots/Latest_Dist.jpg` is separate from the two rendered command-output images.

## Maintenance verification — 1 October 2026

The corrected Week 1 source was rebuilt from a fresh Release directory. Ninja completed **10/10** steps. Packaging succeeded, and both launch checks passed again. The corrected build and packaged executable have SHA-256 `F550BD74894B7591228CF8DE6EBCD2848C4B22FEE4A911E7144F2D9F42579659`.

The build script now preserves the MSVC environment when PATH appears with different casing and selects MSVC explicitly. Packaging validates its output location and preserves an existing package in a backup. The Results-tab descriptions now correctly identify preview geometry.

Current verification records:

- [Build](build-2026-10-01.log)
- [Packaging](package-2026-10-01.log)
- [Raw launch check](smoke-launch-2026-10-01.log)
- [Package launch check](smoke-package-2026-10-01.log)

The September logs and application screenshot remain the original Week 1 evidence. The build and packaging transcript images display the October maintenance verification. Generated executables and packages are excluded from version control.
