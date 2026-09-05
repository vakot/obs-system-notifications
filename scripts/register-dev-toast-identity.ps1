[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateNotNullOrEmpty()]
    [string] $ObsExecutable,

    [Parameter()]
    [ValidateNotNullOrEmpty()]
    [string] $Profile = 'Sync_Replay_Dev',

    [Parameter()]
    [switch] $Remove
)

$ErrorActionPreference = 'Stop'
$appUserModelId = 'OBS Studio'
$notificationLabel = 'OBS System Notifications'
$shortcutPath = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\${notificationLabel}.lnk"
$legacyShortcutPath = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\OBS Studio (obs-system-notifications dev).lnk'

if ($Remove) {
    foreach ($path in @($shortcutPath, $legacyShortcutPath)) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Force
            Write-Host "Removed toast shortcut: $path"
        }
    }
    return
}

if (Test-Path -LiteralPath $legacyShortcutPath) {
    Remove-Item -LiteralPath $legacyShortcutPath -Force
    Write-Host "Removed legacy toast shortcut: $legacyShortcutPath"
}

$resolvedExecutable = (Resolve-Path -LiteralPath $ObsExecutable).Path
$workingDirectory = Split-Path -Parent $resolvedExecutable
$shortcutDirectory = Split-Path -Parent $shortcutPath
New-Item -ItemType Directory -Path $shortcutDirectory -Force | Out-Null

if (-not ('DevToastShortcut.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace DevToastShortcut {
    [StructLayout(LayoutKind.Sequential)]
    public struct PROPERTYKEY {
        public Guid fmtid;
        public uint pid;
    }

    [StructLayout(LayoutKind.Explicit, Size = 16)]
    public struct PROPVARIANT {
        [FieldOffset(0)] public ushort vt;
        [FieldOffset(2)] public ushort reserved1;
        [FieldOffset(4)] public ushort reserved2;
        [FieldOffset(6)] public ushort reserved3;
        [FieldOffset(8)] public IntPtr pointerValue;
    }

    [ComImport, Guid("000214F9-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IShellLinkW {
        [PreserveSig] int GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] string fileName, int maxPath, IntPtr findData, uint flags);
        [PreserveSig] int GetIDList(out IntPtr itemIdList);
        [PreserveSig] int SetIDList(IntPtr itemIdList);
        [PreserveSig] int GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] string name, int maxName);
        [PreserveSig] int SetDescription([MarshalAs(UnmanagedType.LPWStr)] string name);
        [PreserveSig] int GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] string directory, int maxPath);
        [PreserveSig] int SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string directory);
        [PreserveSig] int GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] string arguments, int maxPath);
        [PreserveSig] int SetArguments([MarshalAs(UnmanagedType.LPWStr)] string arguments);
        [PreserveSig] int GetHotkey(out short hotkey);
        [PreserveSig] int SetHotkey(short hotkey);
        [PreserveSig] int GetShowCmd(out int showCommand);
        [PreserveSig] int SetShowCmd(int showCommand);
        [PreserveSig] int GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] string iconPath, int maxPath, out int iconIndex);
        [PreserveSig] int SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string iconPath, int iconIndex);
        [PreserveSig] int SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string path, uint reserved);
        [PreserveSig] int Resolve(IntPtr windowHandle, uint flags);
        [PreserveSig] int SetPath([MarshalAs(UnmanagedType.LPWStr)] string path);
    }

    [ComImport, Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IPropertyStore {
        [PreserveSig] int GetCount(out uint count);
        [PreserveSig] int GetAt(uint index, out PROPERTYKEY key);
        [PreserveSig] int GetValue(ref PROPERTYKEY key, out PROPVARIANT value);
        [PreserveSig] int SetValue(ref PROPERTYKEY key, ref PROPVARIANT value);
        [PreserveSig] int Commit();
    }

    [ComImport, Guid("0000010B-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    public interface IPersistFile {
        [PreserveSig] int GetClassID(out Guid classId);
        [PreserveSig] int IsDirty();
        [PreserveSig] int Load([MarshalAs(UnmanagedType.LPWStr)] string fileName, uint mode);
        [PreserveSig] int Save([MarshalAs(UnmanagedType.LPWStr)] string fileName, [MarshalAs(UnmanagedType.Bool)] bool remember);
        [PreserveSig] int SaveCompleted([MarshalAs(UnmanagedType.LPWStr)] string fileName);
        [PreserveSig] int GetCurFile([MarshalAs(UnmanagedType.LPWStr)] out string fileName);
    }

    [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
    public class ShellLink { }

    public static class NativeMethods {
        [DllImport("ole32.dll")]
        public static extern int PropVariantClear(ref PROPVARIANT propVariant);

        private static void Check(int hResult) {
            if (hResult < 0) Marshal.ThrowExceptionForHR(hResult);
        }

        public static void CreateShortcut(string shortcutPath, string targetPath, string workingDirectory, string arguments, string appUserModelId) {
            var shellLink = (IShellLinkW)new ShellLink();
            Check(shellLink.SetPath(targetPath));
            Check(shellLink.SetWorkingDirectory(workingDirectory));
            Check(shellLink.SetArguments(arguments));
            Check(shellLink.SetDescription("OBS Studio notification integration"));

            var propertyKey = new PROPERTYKEY {
                fmtid = Guid.Parse("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3"),
                pid = 5,
            };
            var propertyValue = new PROPVARIANT {
                vt = 31,
                pointerValue = Marshal.StringToCoTaskMemUni(appUserModelId),
            };
            try {
                var properties = (IPropertyStore)shellLink;
                Check(properties.SetValue(ref propertyKey, ref propertyValue));
                Check(properties.Commit());
            } finally {
                PropVariantClear(ref propertyValue);
            }

            Check(((IPersistFile)shellLink).Save(shortcutPath, true));
        }
    }
}
'@
}

[DevToastShortcut.NativeMethods]::CreateShortcut(
    $shortcutPath,
    $resolvedExecutable,
    $workingDirectory,
    "--portable --profile `"$Profile`"",
    $appUserModelId)
Write-Host "Registered toast shortcut: $shortcutPath"
Write-Host "Target: $resolvedExecutable"
Write-Host "AppUserModelID: $appUserModelId"
