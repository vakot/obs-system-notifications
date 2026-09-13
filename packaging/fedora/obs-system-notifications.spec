Name:           obs-system-notifications
Version:        0.0.2
Release:        1%{?dist}
Summary:        Native system notifications for OBS Studio
License:        GPL-2.0-or-later
URL:            https://github.com/vakot/obs-system-notifications
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.28
BuildRequires:  dbus-devel
BuildRequires:  gcc-c++
BuildRequires:  ninja-build
BuildRequires:  obs-studio-devel >= 32.1.2
Requires:       obs-studio%{?_isa} >= 32.1.2

%description
OBS Studio plugin that displays recording, replay-buffer, and screenshot
events through the desktop notification service. Saved-file notifications
can reveal the exact output file in the desktop file manager.

%prep
%autosetup

%build
%cmake -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo
%cmake_build

%check
ctest --test-dir %{_vpath_builddir} --output-on-failure

%install
%cmake_install

%files
%{_libdir}/obs-plugins/obs-system-notifications.so

%changelog
* Sun Sep 13 2026 vakot <vakot@users.noreply.github.com> - 0.0.2-1
- Add Fedora and Nobara RPM packaging
