**Do not come here and open issues about problems with installation, crashes with bug check 0x109: CRITICAL_STRUCTURE_CORRUPTION or questions on how to disable PatchGuard. I will permanently ban you from the issue tracker. If you don't know how to properly install the tool you don't know enough to use it responsibly and you should use something else like [ScyllaHide](https://github.com/x64dbg/ScyllaHide).

# Overview

TitanHide is a driver intended to hide debuggers from certain processes. This variant routes the existing Nt* hiding handlers through InfinityHook syscall tracing/interception instead of SSDT table hooks or code-cave hook transport. To hide a process, you must pass a simple structure with a ProcessID and the hiding option(s) to enable, to the driver. The user-mode API, GUI, and debugger plugin usage remain unchanged. A console front end is also included as `TitanHideCLI.exe`.

This InfinityHook variant is x64-only and does not include an SSDT/Hooklib runtime fallback.

The idea for this project was thought of together with cypher, shoutout man!

# Features

- ProcessDebugFlags (NtQueryInformationProcess)
- ProcessDebugPort (NtQueryInformationProcess)
- ProcessDebugObjectHandle (NtQueryInformationProcess)
- DebugObject (NtQueryObject)
- SystemKernelDebuggerInformation (NtQuerySystemInformation)
- SystemDebugControl (NtSystemDebugControl)
- NtClose (STATUS_INVALID_HANDLE/STATUS_HANDLE_NOT_CLOSABLE exceptions)
- ThreadHideFromDebugger (NtSetInformationThread)
- Protect DRx (HW BPs) (NtGetContextThread/NtSetContextThread)

# Test environments

- Windows 11 x64
- Windows 10 x64

# Compiling

1. Install Visual Studio 2022.
2. Install the [WDK10](https://go.microsoft.com/fwlink/?linkid=2128854).
3. Open `TitanHide.sln` and build an x64 configuration.

To build from the command line, run:

```sh
.\build.bat
```

`build.bat` invokes `build.ps1`, builds the x64 solution configuration, builds the Win32 x32dbg plugin, and verifies the expected outputs, including:

```text
x64\Release\TitanHide.sys
x64\Release\TitanHideGUI.exe
x64\Release\TitanHideCLI.exe
x64\Release\TitanHideTest.exe
x64\Release\plugins\
```

## CLI

`TitanHideCLI.exe` is the console counterpart of the GUI. It can hide or unhide a PID, clear all active hiding entries, and load, unload, or inspect the driver service. The executable requests administrator privileges through its manifest.

Commands:

```sh
TitanHideCLI.exe status
TitanHideCLI.exe load [--driver-path <TitanHide.sys>]
TitanHideCLI.exe hide <pid> [--safe | --options <list>]
TitanHideCLI.exe unhide <pid> [--safe | --options <list>]
TitanHideCLI.exe unhide-all
TitanHideCLI.exe unload
TitanHideCLI.exe options
```

Useful options:

- `--driver-name <name>`: driver, service, and DOS device name. The default is `InfinityHookHide`. The driver derives its device name from the service name, so use the same custom name here and when creating the service.
- `--driver-path <path>`: driver image used by `load`. If omitted, the CLI looks for `TitanHide.sys` next to the executable and in the common build-output directories.
- `--safe`: enable every feature except `ThreadHideFromDebugger`.
- `--options <list>`: comma-separated feature names, such as `ntclose,get-context-thread,set-context-thread`, or a hexadecimal mask such as `0x3FF`.

Examples:

```sh
TitanHideCLI.exe hide 1234
TitanHideCLI.exe hide 1234 --safe
TitanHideCLI.exe hide 1234 --options ntclose,get-context-thread
TitanHideCLI.exe unhide 1234 --options ntclose
TitanHideCLI.exe status --driver-name NotTitanHide
```

Omitting `--safe` and `--options` selects every supported feature (`0xFFFFFFFF`). For `unhide`, this removes every hiding feature for the PID; passing `--options` removes only the listed features.

# Requirements

**You need to disable PatchGuard and driver signing enforcement (DSE) before using this driver.**

To disable PatchGuard you can try one of the following projects:

- [EfiGuard](https://github.com/Mattiwatti/EfiGuard)
- [SandboxBootkit](https://github.com/thesecretclub/SandboxBootkit)
- [Shark](https://github.com/9176324/Shark)
- [UPGDSED](https://github.com/hfiref0x/UPGDSED) (archived in 2019)

To load the driver you can enable test signing:

```sh
bcdedit /set testsigning on
```

# Installation

1. Copy `TitanHide.sys` to `%systemroot%\system32\drivers`.
2. Run the command `sc create TitanHide binPath= %systemroot%\system32\drivers\TitanHide.sys type= kernel` to create the TitanHide service.
3. Run the command `sc start TitanHide` to start the TitanHide service.
4. Run the command `sc query TitanHide` to check if TitanHide is running.

To check if TitanHide is working correctly, use [DebugView](https://technet.microsoft.com/en-us/sysinternals/debugview.aspx) or check `C:\TitanHide.log`.

You can also manage the service from an elevated command prompt:

```sh
TitanHideCLI.exe load --driver-name TitanHide --driver-path %systemroot%\system32\drivers\TitanHide.sys
TitanHideCLI.exe status --driver-name TitanHide
TitanHideCLI.exe unload --driver-name TitanHide
```

## Hiding

For VMProtect 3.9.4 and above you need to change the service name to something else. For example `sc create NotTitanHide`, which will bypass their latest 'detection'. After changing the service name you will need to configure the plugin with the following command in x64dbg:

```
TitanHideName NotTitanHide
```

# Remarks

- When using x64dbg, you can use the TitanHide plugin (available on the download page).
- This variant is intended for Windows 10/11 x64 VM testing and has no SSDT/Hooklib runtime fallback.
- **NEVER RUN THIS DRIVER ON A PRODUCTION SYSTEM, ALWAYS USE A VM!**

