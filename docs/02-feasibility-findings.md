# Feasibility findings

Observed on 2026-09-05 while preserving the separately installed OBS process.

## Runtime baseline

- OS: Windows 11 Pro 25H2, x64.
- Portable runtime: `obs-dev/bin/64bit/obs64.exe`, OBS Studio 32.2.1.
- Fixture profile: `Sync_Replay_Dev`.
- The portable process reached `==== Startup complete ===============================================`.
- The fixture initialized Display Capture, the configured WASAPI microphone, NVENC, and Replay Buffer support.
- A Steam-installed OBS process was already running from `C:\Program Files (x86)\Steam\...\obs64.exe`; it was not stopped or modified.

The runtime baseline proves that the supplied portable build is usable for plugin load testing. It does not prove the plugin behavior because no plugin DLL exists yet.

## OBS API investigation

The official OBS Studio 32.2.1 source was checked out separately and matched to the runtime executable version. `frontend/api/obs-frontend-api.h` exposes:

| Requirement | API/event confirmed |
| --- | --- |
| Recording lifecycle | `OBS_FRONTEND_EVENT_RECORDING_STARTED`, `RECORDING_STOPPED` |
| Recording pause | `OBS_FRONTEND_EVENT_RECORDING_PAUSED`, `RECORDING_UNPAUSED` |
| Replay lifecycle | `OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTED`, `REPLAY_BUFFER_STOPPED` |
| Replay completion | `OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED` |
| Screenshot completion | `OBS_FRONTEND_EVENT_SCREENSHOT_TAKEN` |
| Final recording path | `obs_frontend_get_last_recording()` |
| Final replay path | `obs_frontend_get_last_replay()` |
| Final screenshot path | `obs_frontend_get_last_screenshot()` |

The OBS frontend source updates the final recording/replay/screenshot path before dispatching the corresponding completion event. The router can therefore use the final-path APIs and must still validate the returned path before creating a clickable notification.

Reference source: [OBS Studio 32.2.1 `obs-frontend-api.h`](https://github.com/obsproject/obs-studio/blob/32.2.1/frontend/api/obs-frontend-api.h).

## Windows notification identity finding

Microsoft's desktop-toast documentation requires a valid AppUserModelID on a Start-menu/All Programs shortcut. `Get-StartApps` did not report an OBS entry on this machine; the existing startup shortcut is not an AUMID-backed OBS app registration. A portable executable by itself is therefore not a sufficient desktop-toast identity.

This is a real platform constraint. The MVP resolution is to use a shortcut that targets the existing OBS executable and carries the OBS notification identity. The plugin now creates or repairs that user-local shortcut when the Windows backend starts, so a manually installed release DLL has the same identity bridge as the portable fixture. No plugin executable, helper process, or separate notification app is allowed.

References: [Enable desktop toast notifications through an AppUserModelID](https://learn.microsoft.com/en-us/windows/win32/shell/enable-desktop-toast-with-appusermodelid), [Sending a toast notification from the desktop](https://learn.microsoft.com/en-us/windows/win32/shell/quickstart-sending-desktop-toast).

## Backend experiment results

On 2026-09-05, the native backend compiled and loaded in the portable OBS runtime with the repository's Visual Studio/CMake toolchain. The harness registered an OBS-targeting shortcut with AppUserModelID `OBS Studio`; `Get-StartApps` reported the expected app identity while the shortcut target remained the existing `obs-dev/bin/64bit/obs64.exe`.

The OBS UI was exercised through the portable process: recording start/save and replay start/save/stop each produced the expected notification payload logs, including the exact final recording and replay paths. Native toasts were visible with the `OBS System Notifications` attribution, and clicking a saved-recording toast while OBS remained open produced the in-process activation log and opened Explorer for the exact output path. No toast creation exception or backend failure log was emitted.

The hardening pass rebuilt and reinstalled the plugin through `scripts/start.ps1`, restarted the portable process, and repeated recording/replay smoke events without a crash or delivery-failure log. A direct launch without the harness shortcut also created the user-local identity shortcut before `CreateToastNotifier`, confirming the production/manual-install path. Backend initialization is non-fatal to plugin load; activation contexts are removed on click, dismissal, teardown, and bounded overflow.

Review feedback follow-up: notifications request silent audio in the toast XML. The native unpackaged plugin does not carry a packaged image-asset URI for a Lucide icon, so the display title uses stable Unicode emoji as the lightweight icon fallback. File activation uses Shell's PIDL-based selection API instead of Explorer command-line parsing, so the callback targets the exact saved file.

## Open experiment gates

The native backend phase must still verify, on this exact Windows fixture:

1. Toast display with the portable OBS-targeting AUMID shortcut — verified for recording and replay events.
2. In-process activation while OBS remains open — verified for a saved recording.
3. Independent activation for two saved-file notifications — remaining manual matrix coverage.
4. Graceful no-op when a saved file is deleted before click — remaining manual matrix coverage.
5. Backend teardown while notifications are still visible — remaining manual matrix coverage.

If shortcut creation fails, the backend remains non-fatal and records an explicit identity-setup error; it does not silently fall back to custom UI or a helper executable.
