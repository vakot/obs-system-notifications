[CmdletBinding()]
param(
    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string] $Profile = 'Sync_Replay_Dev',

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string] $ObsRoot = (Join-Path $PSScriptRoot '..\obs-dev')
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$resolvedObsRoot = (Resolve-Path -LiteralPath $ObsRoot).Path
$vsDevCmd = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat'
$cmake = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$buildDirectory = Join-Path $repositoryRoot 'build_x64'
$executable = (Resolve-Path -LiteralPath (Join-Path $resolvedObsRoot 'bin\64bit\obs64.exe')).Path
$plugin = Join-Path $resolvedObsRoot 'obs-plugins\64bit\obs-system-notifications.dll'

function Get-PortableObsProcesses {
    @(Get-CimInstance Win32_Process -Filter "Name = 'obs64.exe'" |
        Where-Object { $_.ExecutablePath -eq $executable })
}

function Stop-PortableObs {
    $processes = Get-PortableObsProcesses
    if ($processes.Count -eq 0) { return }

    Write-Host "Closing $($processes.Count) portable OBS process(es)..."
    foreach ($process in $processes) {
        $windowProcess = Get-Process -Id $process.ProcessId -ErrorAction SilentlyContinue
        if ($windowProcess) { $null = $windowProcess.CloseMainWindow() }
    }

    $deadline = (Get-Date).AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 250
        $processes = Get-PortableObsProcesses
    } while ($processes.Count -gt 0 -and (Get-Date) -lt $deadline)

    if ($processes.Count -gt 0) {
        Write-Warning 'Portable OBS did not close gracefully; stopping only the portable process(es).'
        foreach ($process in $processes) { Stop-Process -Id $process.ProcessId -Force }
    }

    if ((Get-PortableObsProcesses).Count -gt 0) {
        throw 'Portable OBS is still running; refusing to rebuild or start another instance.'
    }
}

function Invoke-CMakeThroughVsDev {
    param(
        [Parameter(Mandatory)] [string] $Arguments,
        [Parameter(Mandatory)] [string] $Description
    )

    Write-Host "$Description..."
    $command = "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >NUL && `"$cmake`" $Arguments"
    cmd /d /c $command
    if ($LASTEXITCODE -ne 0) { throw "$Description failed with exit code $LASTEXITCODE." }
}

if (-not (Test-Path -LiteralPath $vsDevCmd -PathType Leaf)) { throw "Visual Studio developer environment was not found: $vsDevCmd" }
if (-not (Test-Path -LiteralPath $cmake -PathType Leaf)) { throw "CMake executable was not found: $cmake" }
if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt') -PathType Leaf)) {
    throw "CMake build directory is not configured: $buildDirectory. Run 'cmake --preset windows-x64' first."
}

Stop-PortableObs
Push-Location $repositoryRoot
try {
    Invoke-CMakeThroughVsDev '--build --preset windows-x64 --parallel 8' 'Building the latest plugin'
    Invoke-CMakeThroughVsDev "--install `"$buildDirectory`" --config RelWithDebInfo" 'Installing the latest plugin into standalone OBS'
}
finally {
    Pop-Location
}

if (-not (Test-Path -LiteralPath $plugin -PathType Leaf)) { throw "Installed plugin was not found: $plugin" }
Write-Host "Updated standalone plugin: $plugin"

$started = Start-Process -FilePath $executable `
    -WorkingDirectory (Split-Path -Parent $executable) `
    -ArgumentList @('--portable', '--profile', $Profile) `
    -PassThru

Write-Host "Started standalone OBS (PID $($started.Id)) with profile '$Profile'."
