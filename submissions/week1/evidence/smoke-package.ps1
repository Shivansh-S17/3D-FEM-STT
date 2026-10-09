$ErrorActionPreference = 'Stop'
$weekRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $weekRoot 'code\dist\FemGui.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Package the Week 1 snapshot first: $exe"
}

$env:PATH = "$env:SystemRoot\system32;$env:SystemRoot;$(Split-Path $exe)"
$env:QT_QPA_PLATFORM = 'offscreen'
$process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -PassThru -WindowStyle Hidden
try {
    Start-Sleep -Seconds 4
    $process.Refresh()
    if ($process.HasExited) {
        throw "Package smoke test failed: application exited early with code $($process.ExitCode)"
    }
    Write-Output 'Package smoke test passed: app stayed alive for 4 seconds with clean PATH.'
} finally {
    $process.Refresh()
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id
    }
}
