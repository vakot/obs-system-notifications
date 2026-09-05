# Local development flow

This flow uses the supplied portable OBS installation and never targets the normal OBS installation. Keep OBS stopped while building or installing the plugin.

## Prerequisites

- Windows x64.
- Visual Studio 2022 Build Tools with the x64 C++ workload.
- CMake from the Visual Studio installation.
- The repository-local `obs-dev` directory.
- The `Sync_Replay_Dev` profile for runtime checks.

Configure once after the native plugin skeleton lands:

```powershell
cmake --preset windows-x64
```

## Build, install, and start

```powershell
Set-Location 'C:\Users\vakot\Documents\GitHub\obs-system-notifications'
& .\scripts\start.ps1
```

The script closes only processes whose executable path is the repository's `obs-dev/bin/64bit/obs64.exe`, builds `RelWithDebInfo`, installs the current plugin into `obs-dev`, registers a user-local Start-menu shortcut targeting that same OBS executable with AppUserModelID `OBS Studio`, and launches that shortcut for one portable OBS instance with `Sync_Replay_Dev`. Launching through the shortcut preserves the identity needed for in-process toast activation. The plugin also creates or repairs this identity shortcut during backend startup, so direct/manual OBS launches exercise the same production path. The plugin does not spawn or install a helper process.

The script intentionally does not close a separately installed OBS instance.

## Inspect startup

```powershell
Get-CimInstance Win32_Process -Filter "Name = 'obs64.exe'" |
    Select-Object ProcessId,ExecutablePath,CommandLine

Get-ChildItem .\obs-dev\config\obs-studio\logs -File |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1 |
    ForEach-Object {
        Get-Content $_.FullName |
            Select-String 'obs-system-notifications|Startup complete|Recording|Replay|Screenshot'
    }
```

Logs belong to the portable fixture under `obs-dev/config/obs-studio/logs`.

## MVP manual matrix

With the standalone OBS window open:

- Start recording, pause, resume, and stop. Verify the three state notifications and the saved notification.
- Confirm notifications are silent and display the event-specific emoji fallback in the title.
- Click the saved recording notification and verify Explorer selects the exact output file, rather than opening an unrelated default folder. The development harness uses the OBS-targeting shortcut so this exercises the in-process activation path.
- Start Replay Buffer, save two replays, and stop it. Verify the start/stop/save notifications and click each saved notification independently.
- Take a screenshot and click its notification to reveal the exact screenshot.
- Delete a saved file before clicking its notification. Verify a warning log and no OBS crash.
- Restart the portable OBS instance and repeat one state transition. Verify one notification per event and no duplicate callback logs.

If notifications do not appear, confirm the identity bridge is present:

```powershell
Get-StartApps | Where-Object Name -eq 'OBS System Notifications'
```

The result should show AppID `OBS Studio`. The shortcut is user-local and is recreated by the plugin when notifications initialize. Remove it only when you intentionally want to reset the local toast identity:

```powershell
& .\scripts\register-dev-toast-identity.ps1 `
    -ObsExecutable (Resolve-Path .\obs-dev\bin\64bit\obs64.exe).Path `
    -Remove
```

## Teardown

Close the portable OBS window before rebuilding. Confirm that no process remains for the portable executable:

```powershell
$obsExe = (Resolve-Path .\obs-dev\bin\64bit\obs64.exe).Path
$portable = @(Get-CimInstance Win32_Process -Filter "Name = 'obs64.exe'" |
    Where-Object ExecutablePath -eq $obsExe)
if ($portable.Count -ne 0) { throw 'Portable OBS is still running.' }
```

Do not use a process-name-only stop command because another OBS installation may be running.
