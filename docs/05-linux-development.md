# Linux development flow

Linux builds use the system OBS development packages and the freedesktop notification service on the user's D-Bus session.

## Prerequisites

- OBS Studio development files providing `libobs` and `obs-frontend-api` CMake packages.
- D-Bus development files providing the `dbus-1` pkg-config module (for Debian-based systems: `libdbus-1-dev`).
- A C++17 compiler, CMake 3.28 or newer, and a desktop notification service.

## Build, test, and install

From the repository root:

```sh
cmake --preset linux
cmake --build --preset linux
ctest --test-dir build_linux --output-on-failure
cmake --install build_linux
```

The preset installs into `build_linux/install` by default. For a system or distribution package, pass the package's staging prefix to CMake instead of installing directly into the host filesystem.

Runtime notifications are sent through `org.freedesktop.Notifications` on the session bus. Saved-file notification actions use `org.freedesktop.FileManager1.ShowItems`, so the exact output file is handed to the desktop file manager without launching a shell command. If either service is unavailable, the plugin continues loading and logs the backend failure.
