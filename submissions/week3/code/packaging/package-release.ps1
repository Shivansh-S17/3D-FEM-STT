# Builds a standalone Release package that can be double-clicked (or zipped
# and handed to someone) without Qt Creator, Visual Studio, or PATH changes.
# Usage: .\packaging\package-release.ps1
param(
    [string]$OutDir = "$PSScriptRoot\..\dist"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\.."
$packagePath = [IO.Path]::GetFullPath($OutDir)
$projectPath = [IO.Path]::GetFullPath($root.Path)
$defaultPackage = Join-Path $projectPath 'dist'
$packageBuildPrefix = (Join-Path $projectPath 'build\package-')
if ($packagePath -ne $defaultPackage -and -not $packagePath.StartsWith($packageBuildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutDir must be the project dist directory or build/package-<name>.'
}
if (Test-Path -LiteralPath $packagePath) {
    if ((Get-Item -LiteralPath $packagePath).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
        throw 'Refusing to replace a linked package directory.'
    }
}
$OutDir = $packagePath

# 1. Build Release.
& "$root\build.ps1" -Config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# Freeze the solver before replacing the previous working desktop package.
$env:PYINSTALLER_CONFIG_DIR = "$root\build\pyinstaller-cache"
& python -m PyInstaller --noconfirm --workpath "$root\build\solver-freeze" --distpath "$root\build\solver-dist" "$root\packaging\fem-solver.spec"
if ($LASTEXITCODE -ne 0) { throw "Solver packaging failed" }
& python "$root\packaging\collect-solver-licenses.py" "$root\build\solver-dist\FemSolver\licenses" fem-solver
if ($LASTEXITCODE -ne 0) { throw "Solver license collection failed" }

# 2. Preserve the prior package before creating a fresh one.
if (Test-Path -LiteralPath $OutDir) {
    $previousPackage = $OutDir + '.previous-' + [Guid]::NewGuid().ToString('N')
    if (-not $previousPackage.StartsWith($projectPath + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected backup destination.' }
    Move-Item -LiteralPath $OutDir -Destination $previousPackage
    Write-Host "Previous package preserved: $previousPackage"
}
New-Item -ItemType Directory -Path $OutDir | Out-Null
Copy-Item "$root\build\release\FemGui.exe" "$OutDir\FemGui.exe"

# 3. Qt runtime + platform plugins.
$windeployqt = "D:\Softwares\QT\6.11.2\msvc2022_64\bin\windeployqt6.exe"
& $windeployqt --release --no-translations --no-system-d3d-compiler --no-opengl-sw "$OutDir\FemGui.exe"
if ($LASTEXITCODE -ne 0) { throw "windeployqt6 failed" }

Remove-Item "$OutDir\vc_redist.x64.exe" -ErrorAction SilentlyContinue # we ship the raw CRT DLLs instead

# 4. VTK runtime DLLs (windeployqt doesn't know about these).
Copy-Item "D:\Softwares\VTK-9.7.0\install\bin\*.dll" "$OutDir\" -Force

# 5. MSVC redistributable DLLs, so the package runs on a machine without
#    Visual Studio / Build Tools installed.
$vsRoot = "C:\Program1"
if (-not (Test-Path $vsRoot)) { $vsRoot = "C:\Program" }
$redist = Get-ChildItem "$vsRoot\VC\Redist\MSVC" -Directory | Select-Object -First 1
Copy-Item "$($redist.FullName)\x64\Microsoft.VC143.CRT\*.dll" "$OutDir\" -Force

# 6. Ship one frozen numerical runtime shared by both model backends.
Copy-Item -LiteralPath "$root\build\solver-dist\FemSolver" -Destination "$OutDir\solver\FemSolver" -Recurse
Copy-Item -LiteralPath "$root\packaging\README-PACKAGE.txt" -Destination "$OutDir\README.txt"

$revision = (& git -C $root rev-parse HEAD).Trim()
$packageManifest = [ordered]@{
    builtAtUtc = [DateTime]::UtcNow.ToString('o')
    gitRevision = $revision
    status = 'C3D8 provisional: external physical acceptance open'
    guiSha256 = (Get-FileHash -Algorithm SHA256 "$OutDir\FemGui.exe").Hash.ToLowerInvariant()
    sharedSolverRuntime = $true
    solvers = [ordered]@{
        plate = [ordered]@{ backendId='plate-python-v1'; path='solver/FemSolver/FemSolver.exe'; sha256=(Get-FileHash -Algorithm SHA256 "$OutDir\solver\FemSolver\FemSolver.exe").Hash.ToLowerInvariant() }
        c3d8 = [ordered]@{ backendId='c3d8-python-v1'; modelKind='structured_solid_c3d8'; path='solver/FemSolver/FemSolver.exe'; sha256=(Get-FileHash -Algorithm SHA256 "$OutDir\solver\FemSolver\FemSolver.exe").Hash.ToLowerInvariant(); integration='full' }
    }
}
$packageManifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$OutDir\PACKAGE-INFO.json" -Encoding utf8

$env:FEM_TEST_BUNDLED_SOLVER = "$OutDir\solver\FemSolver\FemSolver.exe"
$env:FEM_TEST_BUNDLED_C3D8_SOLVER = $env:FEM_TEST_BUNDLED_SOLVER
& python "$root\tests\test_packaged_solver.py"
if ($LASTEXITCODE -ne 0) { throw "Frozen plate solver verification failed" }
& python "$root\tests\test_packaged_c3d8_solver.py"
if ($LASTEXITCODE -ne 0) { throw "Frozen C3D8 solver verification failed" }

Write-Host "Package ready: $OutDir\FemGui.exe" -ForegroundColor Green
Write-Host "Zip the '$OutDir' folder to hand it off; FemGui.exe inside it can be double-clicked directly." -ForegroundColor Green
Write-Host "Plate and C3D8 share the bundled solver runtime selected automatically by model type." -ForegroundColor Green
