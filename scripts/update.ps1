[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string] $ArtifactPath,

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string] $ObsRoot = (Join-Path $PSScriptRoot '..\obs-dev'),

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string] $Profile = 'Sync_Replay_Dev',

    [Parameter()]
    [switch] $Start
)

$ErrorActionPreference = 'Stop'
$resolvedObsRoot = (Resolve-Path -LiteralPath $ObsRoot).Path
$artifact = (Resolve-Path -LiteralPath $ArtifactPath).Path
$checksumPath = "$artifact.sha256"
$executable = (Resolve-Path -LiteralPath (Join-Path $resolvedObsRoot 'bin\64bit\obs64.exe')).Path
$pluginDestination = Join-Path $resolvedObsRoot 'obs-plugins\64bit\obs-system-notifications.dll'
$toastIdentityScript = Join-Path $PSScriptRoot 'register-dev-toast-identity.ps1'
$toastShortcut = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\OBS System Notifications.lnk'
$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) "obs-system-notifications-update-$([guid]::NewGuid().ToString('N'))"

if ([System.IO.Path]::GetExtension($artifact) -ine '.zip') {
    throw "Update artifact must be a ZIP archive: $artifact"
}

if (Test-Path -LiteralPath $checksumPath -PathType Leaf) {
    $checksumLine = (Get-Content -LiteralPath $checksumPath -TotalCount 1).Trim()
    $expectedHash = ($checksumLine -split '\s+')[0].ToLowerInvariant()
    if ($expectedHash -notmatch '^[0-9a-f]{64}$') {
        throw "Update checksum file is invalid: $checksumPath"
    }

    $actualHash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "Update artifact checksum does not match: $artifact"
    }

    Write-Host "Verified SHA-256 checksum: $checksumPath"
}

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
        throw 'Portable OBS is still running; refusing to replace the plugin.'
    }
}

Stop-PortableObs
New-Item -ItemType Directory -Path $temporaryDirectory -Force | Out-Null
try {
    Expand-Archive -LiteralPath $artifact -DestinationPath $temporaryDirectory -Force
    $pluginSource = Join-Path $temporaryDirectory 'obs-plugins\64bit\obs-system-notifications.dll'
    if (-not (Test-Path -LiteralPath $pluginSource -PathType Leaf)) {
        throw "Update artifact is missing the plugin DLL: $pluginSource"
    }

    New-Item -ItemType Directory -Path (Split-Path -Parent $pluginDestination) -Force | Out-Null
    Copy-Item -LiteralPath $pluginSource -Destination $pluginDestination -Force
    Write-Host "Updated portable OBS plugin: $pluginDestination"

    if ($Start) {
        & $toastIdentityScript -ObsExecutable $executable -Profile $Profile
        if (-not (Test-Path -LiteralPath $toastShortcut -PathType Leaf)) {
            throw "Toast identity shortcut was not created: $toastShortcut"
        }

        $started = Start-Process -FilePath $toastShortcut -PassThru
        Write-Host "Started standalone OBS (PID $($started.Id)) with profile '$Profile'."
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryDirectory) {
        Remove-Item -LiteralPath $temporaryDirectory -Recurse -Force
    }
}
