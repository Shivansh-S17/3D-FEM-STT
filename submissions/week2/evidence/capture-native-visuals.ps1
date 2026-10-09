$ErrorActionPreference = 'Stop'
$weekRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $weekRoot 'code'
$testExe = Join-Path $sourceRoot 'build\release\FemGuiTests.exe'
$project = Join-Path $weekRoot 'examples\demo-c3d8.femproject.json'
$visuals = Join-Path $weekRoot 'visuals'
$captureLog = Join-Path $PSScriptRoot 'native-visual-capture.log'

if (-not (Test-Path -LiteralPath $testExe)) { throw "Build the Week 2 Release source first: $testExe" }
if (-not (Test-Path -LiteralPath $project)) { throw "Example project missing: $project" }

$env:WEEK2_PROJECT_PATH = [IO.Path]::GetFullPath($project)
$env:WEEK2_VISUAL_DIR = [IO.Path]::GetFullPath($visuals)
$env:QT_QPA_PLATFORM = 'windows'
$env:PATH = "D:\Softwares\QT\6.11.2\msvc2022_64\bin;D:\Softwares\VTK-9.7.0\install\bin;$env:PATH"

Push-Location $sourceRoot
try {
    & $testExe captureNativeWeek2Visuals -o "$captureLog,txt"
    if ($LASTEXITCODE -ne 0) { throw 'Native Week 2 capture failed' }
} finally {
    Pop-Location
}
