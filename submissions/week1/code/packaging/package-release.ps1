# Builds a standalone Release package that can be double-clicked (or zipped
# and handed to someone) without Qt Creator, Visual Studio, or PATH changes.
# Usage: .\packaging\package-release.ps1
param(
    [string]$OutDir = "$PSScriptRoot\..\dist"
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path "$PSScriptRoot\.."
$projectPath = [IO.Path]::GetFullPath($root.Path)
$OutDir = [IO.Path]::GetFullPath($OutDir)
if ($OutDir -ne (Join-Path $projectPath "dist") -and -not $OutDir.StartsWith((Join-Path $projectPath "build\package-"), [StringComparison]::OrdinalIgnoreCase)) {
    throw "OutDir must be the project dist directory or build/package-<name>."
}
if ((Test-Path -LiteralPath $OutDir) -and (Get-Item -LiteralPath $OutDir).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
    throw "Refusing to replace a linked package directory."
}

# 1. Build Release.
& "$root\build.ps1" -Config Release
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

# 2. Preserve an existing package before creating a new output folder.
if (Test-Path -LiteralPath $OutDir) {
    $backup = $OutDir + ".previous-" + [Guid]::NewGuid().ToString("N")
    Move-Item -LiteralPath $OutDir -Destination $backup
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
$redist = @(
    "C:\Program\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT",
    "C:\Program1\VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $redist) { throw "MSVC redistributable DLLs were not found" }
Copy-Item "$redist\*.dll" "$OutDir\" -Force

Write-Host "Package ready: $OutDir\FemGui.exe" -ForegroundColor Green
Write-Host "Zip the '$OutDir' folder to hand it off; FemGui.exe inside it can be double-clicked directly." -ForegroundColor Green
