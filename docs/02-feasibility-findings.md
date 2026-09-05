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

This is a real platform constraint. The MVP resolution is to use a shortcut that targets the existing OBS executable and carries the OBS notification identity. The development harness may create that registration for the portable fixture; a production install/update path must provide the equivalent OBS-owned shortcut. No plugin executable, helper process, or separate notification app is allowed.

References: [Enable desktop toast notifications through an AppUserModelID](https://learn.microsoft.com/en-us/windows/win32/shell/enable-desktop-toast-with-appusermodelid), [Sending a toast notification from the desktop](https://learn.microsoft.com/en-us/windows/win32/shell/quickstart-sending-desktop-toast).

## Open experiment gates

The native backend phase must still verify, on this exact Windows fixture:

1. Toast display with the portable OBS-targeting AUMID shortcut.
2. In-process activation while OBS remains open.
3. Independent activation for two saved-file notifications.
4. Graceful no-op when a saved file is deleted before click.
5. Backend teardown while notifications are still visible.

The absence of a shortcut is not a reason to silently fall back to custom UI or a helper executable; it must remain an explicit installation error/log path.
