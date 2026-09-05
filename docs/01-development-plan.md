# MVP development plan

This plan turns `tmp/poc-implementation-plan.md` into independently reviewable implementation phases. The POC document is the product specification; the branch/PR sequence and local development workflow below are the repository workflow requested for this task.

## MVP contract

Version `0.0.1` is a Windows x64 native OBS plugin with one thin path:

```text
OBS frontend event -> event router -> fixed notification definition
                   -> Windows native notification -> optional file reveal
```

The MVP supports recording started/paused/resumed/saved, Replay Buffer started/stopped/saved, and screenshot saved. Saved-file notifications carry the exact path supplied by OBS and reveal that file in Explorer when clicked. There is no settings UI, configuration, localization, helper process, IPC, custom rendering, or cross-platform code.

## Platform decisions and gates

- Target the supplied standalone OBS 32.2.1 runtime on Windows 11 x64. The first supported Windows API baseline is Windows 10 2004 or newer.
- Use the exact OBS frontend APIs confirmed from the OBS 32.2.1 source tree: frontend event callbacks, `obs_frontend_get_last_recording()`, `obs_frontend_get_last_replay()`, and `obs_frontend_get_last_screenshot()`.
- Use a WinRT desktop toast backend with per-notification activation context and Shell Explorer reveal. The backend must not spawn a helper process.
- Windows desktop toasts require an AppUserModelID-backed shortcut. The plugin must keep the OBS process identity: the identity bridge may only register/repair a shortcut targeting the existing OBS executable; it must not add a plugin executable or a second app identity. The portable development harness owns the dev shortcut setup. The backend phase cannot pass until both notification display and click activation are verified with this arrangement.
- If a deployment cannot provide an OBS-owned AUMID shortcut, native Action Center toasts are not available under the POC constraints. That is a release blocker, not a reason to add a companion application.

## Branch and PR sequence

Each phase starts from the latest squashed `master`. The branch is pushed only for its PR, the PR is squash-merged, and the next branch is created after `master` is updated. No implementation work is done directly on `master`.

1. `docs/vakot/mvp-development-plan` — this plan, feasibility findings, and standalone development harness.
2. `feat/vakot/native-plugin-skeleton` — CMake project, module lifecycle, install layout, and load/unload logging.
3. `feat/vakot/obs-event-routing` — event model, OBS callbacks, fixed definitions, and final file-path resolution.
4. `feat/vakot/windows-notification-backend` — native toast display, OBS-owned identity bridge, activation, and Explorer reveal.
5. `fix/vakot/mvp-hardening` — teardown safety, failure handling, independent notification contexts, tests, documentation, and final manual matrix.

Every PR has one exit gate. Do not start the next phase while its gate is unverified.

## Phase gates

### Phase 0 — Feasibility and harness (this PR)

Record the standalone runtime, exact OBS APIs, notification identity requirement, and reproducible build/start flow.

Exit gate: `obs-dev` starts successfully without touching another OBS installation; the API and identity assumptions are documented; the next branch can build and install into the portable runtime.

### Phase 1 — Native plugin skeleton

Create the smallest Windows-only CMake module using `OBS::libobs` and `OBS::obs-frontend-api`. Add `OBS_DECLARE_MODULE`, `obs_module_load`, `obs_module_unload`, one frontend callback registration/removal, and the `[obs-system-notifications]` log prefix. Install only the DLL and required locale data under `obs-plugins/64bit` and `data/obs-plugins/obs-system-notifications`.

Exit gate: the DLL builds as x64, loads in standalone OBS, logs load and unload, and survives repeated start/stop cycles with no stale callback.

### Phase 2 — OBS event routing and payload definitions

Introduce the small internal event model and `NotificationContext`. Map frontend events to fixed title/body/click definitions. Resolve saved paths only from OBS final-path APIs after the corresponding completion event; do not reconstruct output paths or use global `lastSavedPath` state.

Exit gate: every supported event maps deterministically, state events have no file context, file events have the correct final path, and missing/invalid paths become logged no-ops.

### Phase 3 — Windows notification backend

Implement native Windows toast creation without custom rendering. Prove the AUMID-backed OBS shortcut arrangement in the portable fixture, preserve each notification's own activation context, and reveal files with Explorer. Keep backend failures non-fatal and log HRESULT/Win32 failures with the plugin prefix.

Exit gate: a state notification displays; a saved recording/replay/screenshot displays; clicking each independently reveals the exact file; two notifications remain independent; no helper process is introduced.

### Phase 4 — MVP hardening and release readiness

Make lifecycle teardown deterministic, remove callbacks before backend shutdown, invalidate activation context safely, handle deleted files, and avoid duplicate registration. Add focused tests for definitions/path guards and complete the Windows manual matrix.

Exit gate: the Definition of Done in the POC is demonstrated on the standalone fixture, failure scenarios do not crash OBS, and the local start script rebuilds and launches the fresh plugin artifact.

## Required verification evidence

- Build: Windows x64 `RelWithDebInfo` using the repository CMake configuration.
- Automated checks: focused unit tests for pure mapping/guard logic, plus CTest where configured.
- Runtime: one portable `obs-dev` process, plugin load log, recording/replay/screenshot event logs, notification display, and click-to-reveal evidence.
- Lifecycle: stop OBS before rebuild/reinstall, verify no standalone `obs64.exe` remains, and leave unrelated OBS installations untouched.
- Manual matrix: startup, recording transitions, replay transitions, screenshot, multiple saved files, delete-before-click, and restart without duplicate callbacks.

## Out of scope until a later product decision

Settings, templates, editable text, localization, custom icons, grouping, notification history, sound controls, macOS/Linux support, companion processes, and any generic event/configuration framework remain excluded.
