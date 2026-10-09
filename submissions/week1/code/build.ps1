# Configures and builds FemGui using the MSVC 2022 x64 toolchain + Ninja.
# Usage: .\build.ps1 [-Config Debug|Release] [-Clean]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$preset = if ($Config -eq "Release") { "windows-msvc-release" } else { "windows-msvc-debug" }
$binDir = Join-Path $root "build\$($Config.ToLower())"

if ($Clean -and (Test-Path $binDir)) {
    $resolvedBuild = [IO.Path]::GetFullPath($binDir)
    $expectedBuild = [IO.Path]::GetFullPath((Join-Path $root "build\$($Config.ToLower())"))
    if ($resolvedBuild -ne $expectedBuild -or (Get-Item -LiteralPath $resolvedBuild).Attributes.HasFlag([IO.FileAttributes]::ReparsePoint)) {
        throw "Refusing to clean an unexpected or linked build directory."
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

$vcvars = @(
    "C:\Program\VC\Auxiliary\Build\vcvars64.bat",
    "C:\Program1\VC\Auxiliary\Build\vcvars64.bat"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $vcvars) {
    throw "vcvars64.bat not found in the supported Visual Studio locations"
}

# Import the MSVC x64 developer environment into this PowerShell process.
$envDump = cmd /c "`"$vcvars`" >nul 2>&1 && set"
if ($LASTEXITCODE -ne 0) { throw "MSVC environment initialization failed" }
$importedNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($line in $envDump -split "`r`n") {
    if ($line -match '^([^=]+)=(.*)$' -and $importedNames.Add($Matches[1])) {
        Set-Item -Path "Env:$($Matches[1])" -Value $Matches[2]
    }
}

$env:PATH = "D:\Softwares\QT\Tools\Ninja;$env:PATH"

& cmake --preset $preset
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

& cmake --build --preset $preset
if ($LASTEXITCODE -ne 0) { throw "CMake build failed" }

Write-Host "Build succeeded: $binDir\FemGui.exe" -ForegroundColor Green
