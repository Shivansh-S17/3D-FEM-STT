param([string]$PackageDir = "$PSScriptRoot\..\dist")
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDir).Path
$variables = @('PATH', 'QT_QPA_PLATFORM', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH')
$previous = @{}
foreach ($name in $variables) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$process = $null
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_PLUGIN_PATH = "$package\plugins-not-on-path"
    $env:QML2_IMPORT_PATH = "$package\qml-not-on-path"
    $process = Start-Process -FilePath "$package\FemGui.exe" -WorkingDirectory $package -WindowStyle Hidden -PassThru
    if (-not $process.WaitForInputIdle(15000)) { throw 'Packaged GUI did not become responsive.' }
    $process.Refresh()
    if ($process.HasExited -or $process.MainWindowHandle -eq 0) { throw 'Packaged GUI did not create a main window.' }
    if (-not $process.CloseMainWindow()) { throw 'Packaged GUI did not accept the close request.' }
    if (-not $process.WaitForExit(10000)) { throw 'Packaged GUI did not close within ten seconds.' }
    if ($process.ExitCode -ne 0) { throw "Packaged GUI exited with code $($process.ExitCode)." }
    [PSCustomObject]@{ executable="$package\FemGui.exe"; result='Responsive window and normal close'; exitCode=$process.ExitCode; path='Windows system directories only' } |
        ConvertTo-Json | Set-Content -LiteralPath "$PSScriptRoot\..\build\phase7-package-launch.json"
    Write-Host 'Packaged GUI launch and normal close passed with Windows-only PATH.'
} finally {
    if ($null -ne $process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
