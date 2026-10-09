param([string]$PackageDir = "$PSScriptRoot\..\dist")
$ErrorActionPreference = 'Stop'

if (-not ('FemGuiSmoke.NativeWindow' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace FemGuiSmoke {
    public static class NativeWindow {
        private delegate bool EnumWindowsProc(IntPtr handle, IntPtr state);
        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr state);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr handle, out uint processId);
        [DllImport("user32.dll")] private static extern int GetWindowTextLength(IntPtr handle);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr handle, StringBuilder text, int maximum);

        public static IntPtr FindTitledWindow(uint expectedProcessId) {
            IntPtr found = IntPtr.Zero;
            EnumWindows(delegate(IntPtr handle, IntPtr state) {
                uint processId;
                GetWindowThreadProcessId(handle, out processId);
                int length = GetWindowTextLength(handle);
                if (processId == expectedProcessId && length > 0) {
                    StringBuilder title = new StringBuilder(length + 1);
                    GetWindowText(handle, title, title.Capacity);
                    if (title.ToString().StartsWith("FEM Analysis GUI", StringComparison.Ordinal)) {
                        found = handle;
                        return false;
                    }
                }
                return true;
            }, IntPtr.Zero);
            return found;
        }
    }
}
'@
}

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
    $windowDeadline = [DateTime]::UtcNow.AddSeconds(15)
    $window = [IntPtr]::Zero
    do {
        $process.Refresh()
        if ($process.HasExited) { throw "Packaged GUI exited before creating a main window (code $($process.ExitCode))." }
        $window = [FemGuiSmoke.NativeWindow]::FindTitledWindow([uint32]$process.Id)
        if ($window -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $windowDeadline)
    if ($window -eq [IntPtr]::Zero) { throw 'Packaged GUI did not create a main window.' }
    [PSCustomObject]@{ executable="$package\FemGui.exe"; result='Responsive hidden main window'; path='Windows system directories only' } |
        ConvertTo-Json | Set-Content -LiteralPath "$PSScriptRoot\..\build\phase7-package-launch.json"
    Write-Host 'Packaged GUI responsive-window launch passed with Windows-only PATH.'
} finally {
    if ($null -ne $process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
