$ErrorActionPreference = 'Stop'
$weekRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $weekRoot 'code\build\release\FemGui.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Build the Week 1 Release snapshot first: $exe"
}

$env:PATH = "D:\Softwares\QT\6.11.2\msvc2022_64\bin;D:\Softwares\VTK-9.7.0\install\bin;$env:PATH"
$env:QT_QPA_PLATFORM = 'offscreen'
$process = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden
try {
    Start-Sleep -Seconds 3
    $process.Refresh()
    if ($process.HasExited) {
        throw "Smoke test failed: application exited early with code $($process.ExitCode)"
    }
    Write-Output 'Smoke test passed: application stayed alive for 3 seconds.'
} finally {
    $process.Refresh()
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id
    }
}
