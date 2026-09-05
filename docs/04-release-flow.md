# Local update and release flow

The repository intentionally has no GitHub Actions workflow. Builds, packaging, and release uploads run from a Windows development machine with the required OBS and Visual Studio tooling installed.

## Prerequisites

- Clean, up-to-date `master` checkout.
- GitHub CLI (`gh`) authenticated for `vakot/obs-system-notifications`.
- Visual Studio 2022 Build Tools with the x64 C++ workload.
- The repository-local `obs-dev` runtime for local update testing.
- A configured CMake build directory, or permission to run the first-time dependency setup.

## Update a portable OBS installation

The update script installs a previously built release ZIP. It stops only the OBS process whose executable is inside the selected `ObsRoot`; another OBS installation is not touched.

```powershell
Set-Location 'C:\Users\vakot\Documents\GitHub\obs-system-notifications'
& .\scripts\update.ps1 `
    -ArtifactPath .\artifacts\obs-system-notifications-v0.0.1-windows-x64.zip `
    -Start
```

When the matching `.zip.sha256` sidecar is next to the artifact, the update script verifies it before replacing the DLL.

Without `-Start`, the script leaves OBS stopped after replacing the DLL. The ZIP must contain the standard OBS plugin layout:

```text
obs-plugins/
└── 64bit/
    └── obs-system-notifications.dll
```

No code signature is required for this local reinstall. The target OBS process must be stopped so Windows is not holding the DLL open.

## Create and publish a release

`release.ps1` reads the version from `buildspec.json`. For the first release it therefore operates on `v0.0.1`.

```powershell
Set-Location 'C:\Users\vakot\Documents\GitHub\obs-system-notifications'
git switch master
git pull --ff-only
& .\scripts\release.ps1
```

The command performs the complete local flow:

1. verifies clean, up-to-date `master`;
2. creates and pushes an annotated `v0.0.1` tag;
3. creates a draft GitHub release with GitHub-generated notes;
4. builds the `Release` configuration locally;
5. installs into a temporary staging directory;
6. packages the plugin ZIP and SHA-256 sidecar under `artifacts/`;
7. uploads both assets to the draft release; and
8. publishes the release.

The draft release remains available if local build or upload fails, so the failure can be diagnosed without publishing an incomplete release. The tag is not rewritten automatically.

The generated assets are:

```text
artifacts/obs-system-notifications-v0.0.1-windows-x64.zip
artifacts/obs-system-notifications-v0.0.1-windows-x64.zip.sha256
```

## Signing decision

This project produces an unpackaged OBS plugin DLL inside a ZIP, not an MSIX package or installer. OBS documents manual plugin installation by placing the DLL in its plugin directory ([OBS Plugins Guide](https://obsproject.com/kb/plugins-guide)); that local replacement does not require a code signature. The release flow therefore does not require a certificate and intentionally publishes unsigned artifacts for this MVP.

For wider public Windows distribution, Authenticode-signing the DLL and any future installer is recommended because unsigned downloads can receive SmartScreen or Smart App Control warnings ([Microsoft SmartScreen guidance](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)). Adding signing should be a separate release-readiness change once a certificate and protected key-storage process are available; private signing keys must not be committed or passed through plain-text script arguments.
