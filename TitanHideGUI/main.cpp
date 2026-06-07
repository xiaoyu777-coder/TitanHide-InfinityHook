#include <windows.h>
#include <commctrl.h>
#include <winsvc.h>
#include <stdio.h>
#include <string>
#include <utility>
#include "resource.h"
#include "..\TitanHide\TitanHide.h"

static HINSTANCE hInst;
static char modulePath[MAX_PATH];
static char iniPath[MAX_PATH];
static const char* kIniSection = "TitanHide";
static const char* kDefaultDriverName = "InfinityHookHide";

struct DRIVER_STATE
{
    bool ServiceInstalled;
    bool ServiceRunning;
    bool DevicePresent;
    DWORD ServiceState;
};

static std::pair<int, HIDE_TYPE> gOptions[] =
{
    { IDC_CHK_PROCESSDEBUGFLAGS, HideProcessDebugFlags },
    { IDC_CHK_PROCESSDEBUGPORT, HideProcessDebugPort },
    { IDC_CHK_PROCESSDEBUGOBJECTHANDLE, HideProcessDebugObjectHandle },
    { IDC_CHK_DEBUGOBJECT, HideDebugObject },
    { IDC_CHK_SYSTEMDEBUGGERINFORMATION, HideSystemDebuggerInformation },
    { IDC_CHK_NTCLOSE, HideNtClose },
    { IDC_CHK_THREADHIDEFROMDEBUGGER, HideThreadHideFromDebugger },
    { IDC_CHK_NTGETCONTEXTTHREAD, HideNtGetContextThread },
    { IDC_CHK_NTSETCONTEXTTHREAD, HideNtSetContextThread },
    { IDC_CHK_NTSYSTEMDEBUGCONTROL, HideNtSystemDebugControl },
};

static ULONG GetTypeDword(HWND hwndDlg)
{
    ULONG Option = 0;
    for (const auto& option : gOptions)
    {
        if (IsDlgButtonChecked(hwndDlg, option.first))
            Option |= (ULONG)option.second;
    }
    return Option;
}

static void SetOptionChecks(HWND hwndDlg, ULONG Options)
{
    for (const auto& option : gOptions)
    {
        CheckDlgButton(hwndDlg, option.first, (Options & (ULONG)option.second) ? BST_CHECKED : BST_UNCHECKED);
    }
}

static ULONG GetAllOptions()
{
    ULONG Options = 0;
    for (const auto& option : gOptions)
    {
        Options |= (ULONG)option.second;
    }
    return Options;
}

static std::string GetDlgItemTextString(HWND hwndDlg, int controlId)
{
    int length = GetWindowTextLengthA(GetDlgItem(hwndDlg, controlId));
    if (length <= 0)
    {
        return std::string();
    }
    std::string value(length + 1, '\0');
    GetWindowTextA(GetDlgItem(hwndDlg, controlId), &value[0], length + 1);
    value.resize(length);
    return value;
}

static void NormalizeDriverName(std::string& driverName)
{
    while (!driverName.empty() && (driverName[0] == ' ' || driverName[0] == '\t'))
    {
        driverName.erase(0, 1);
    }
    while (!driverName.empty() && (driverName[driverName.size() - 1] == ' ' || driverName[driverName.size() - 1] == '\t'))
    {
        driverName.erase(driverName.size() - 1, 1);
    }
    if (driverName.compare(0, 4, "\\\\.\\") == 0)
    {
        driverName.erase(0, 4);
    }
}

static std::string GetDriverName(HWND hwndDlg)
{
    std::string driverName = GetDlgItemTextString(hwndDlg, IDC_EDT_DRIVER);
    NormalizeDriverName(driverName);
    return driverName;
}

static std::string GetDriverPath(HWND hwndDlg)
{
    return GetDlgItemTextString(hwndDlg, IDC_EDT_DRIVER_PATH);
}

static void SaveDriverSettings(HWND hwndDlg)
{
    std::string driverName = GetDriverName(hwndDlg);
    std::string driverPath = GetDriverPath(hwndDlg);
    WritePrivateProfileStringA(kIniSection, "DriverName", driverName.c_str(), iniPath);
    WritePrivateProfileStringA(kIniSection, "DriverPath", driverPath.c_str(), iniPath);
}

static bool FileExists(const char* path)
{
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static std::string GetModuleDirectory()
{
    std::string directory = modulePath;
    auto separator = directory.find_last_of("\\/");
    if (separator == std::string::npos)
    {
        return ".";
    }
    directory.erase(separator);
    return directory;
}

static void SetDefaultDriverPath(HWND hwndDlg)
{
    char driverPath[MAX_PATH] = "";
    GetPrivateProfileStringA(kIniSection, "DriverPath", "", driverPath, sizeof(driverPath), iniPath);
    if (driverPath[0] != '\0')
    {
        SetWindowTextA(GetDlgItem(hwndDlg, IDC_EDT_DRIVER_PATH), driverPath);
        return;
    }

    const std::string baseDirectory = GetModuleDirectory();
    const char* relativeCandidates[] =
    {
        "TitanHide.sys",
        "..\\TitanHide.sys",
        "..\\Release\\TitanHide.sys",
        "..\\x64\\Release\\TitanHide.sys"
    };

    for (const auto& relative : relativeCandidates)
    {
        char candidate[MAX_PATH] = "";
        _snprintf_s(candidate, sizeof(candidate), _TRUNCATE, "%s\\%s", baseDirectory.c_str(), relative);
        char fullPath[MAX_PATH] = "";
        if (GetFullPathNameA(candidate, sizeof(fullPath), fullPath, NULL) != 0 && FileExists(fullPath))
        {
            SetWindowTextA(GetDlgItem(hwndDlg, IDC_EDT_DRIVER_PATH), fullPath);
            return;
        }
    }

    char fallback[MAX_PATH] = "";
    _snprintf_s(fallback, sizeof(fallback), _TRUNCATE, "%s\\TitanHide.sys", baseDirectory.c_str());
    SetWindowTextA(GetDlgItem(hwndDlg, IDC_EDT_DRIVER_PATH), fallback);
}

static void ShowWin32Error(HWND hwndDlg, const char* title, DWORD error)
{
    char* systemMessage = NULL;
    DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                  NULL,
                                  error,
                                  0,
                                  (LPSTR)&systemMessage,
                                  0,
                                  NULL);
    char message[1024] = "";
    if (length != 0 && systemMessage != NULL)
    {
        _snprintf_s(message, sizeof(message), _TRUNCATE, "%s (0x%08lX)", systemMessage, error);
    }
    else
    {
        _snprintf_s(message, sizeof(message), _TRUNCATE, "Win32 error 0x%08lX", error);
    }
    MessageBoxA(hwndDlg, message, title, MB_ICONERROR);
    if (systemMessage != NULL)
    {
        LocalFree(systemMessage);
    }
}

static bool IsDevicePresent(const char* driverName)
{
    char target[512] = "";
    if (QueryDosDeviceA(driverName, target, sizeof(target)) != 0)
    {
        return true;
    }

    DWORD error = GetLastError();
    return error == ERROR_INSUFFICIENT_BUFFER;
}

static bool QueryDriverState(const char* driverName, DRIVER_STATE* state)
{
    state->ServiceInstalled = false;
    state->ServiceRunning = false;
    state->DevicePresent = IsDevicePresent(driverName);
    state->ServiceState = SERVICE_STOPPED;

    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL)
    {
        return false;
    }

    SC_HANDLE service = OpenServiceA(scm, driverName, SERVICE_QUERY_STATUS);
    if (service != NULL)
    {
        state->ServiceInstalled = true;
        SERVICE_STATUS_PROCESS status = {};
        DWORD bytesNeeded = 0;
        if (QueryServiceStatusEx(service,
                                 SC_STATUS_PROCESS_INFO,
                                 reinterpret_cast<LPBYTE>(&status),
                                 sizeof(status),
                                 &bytesNeeded))
        {
            state->ServiceState = status.dwCurrentState;
            state->ServiceRunning = status.dwCurrentState == SERVICE_RUNNING;
        }
        CloseServiceHandle(service);
    }

    CloseServiceHandle(scm);
    return true;
}

static void UpdateDriverStatus(HWND hwndDlg)
{
    std::string driverName = GetDriverName(hwndDlg);
    if (driverName.empty())
    {
        SetWindowTextA(GetDlgItem(hwndDlg, IDC_TXT_DRIVER_STATUS), "Driver: empty name");
        return;
    }

    DRIVER_STATE state = {};
    if (!QueryDriverState(driverName.c_str(), &state))
    {
        SetWindowTextA(GetDlgItem(hwndDlg, IDC_TXT_DRIVER_STATUS), "Driver: cannot query SCM");
        return;
    }

    char statusText[256] = "";
    const char* serviceText = state.ServiceInstalled ? (state.ServiceRunning ? "running" : "stopped") : "not installed";
    const char* deviceText = state.DevicePresent ? "present" : "missing";
    _snprintf_s(statusText,
                sizeof(statusText),
                _TRUNCATE,
                "Driver %s | service: %s | device: %s",
                driverName.c_str(),
                serviceText,
                deviceText);
    SetWindowTextA(GetDlgItem(hwndDlg, IDC_TXT_DRIVER_STATUS), statusText);
}

static bool WaitForServiceState(SC_HANDLE service, DWORD expectedState)
{
    for (int i = 0; i < 50; i++)
    {
        SERVICE_STATUS_PROCESS status = {};
        DWORD bytesNeeded = 0;
        if (!QueryServiceStatusEx(service,
                                  SC_STATUS_PROCESS_INFO,
                                  reinterpret_cast<LPBYTE>(&status),
                                  sizeof(status),
                                  &bytesNeeded))
        {
            return false;
        }
        if (status.dwCurrentState == expectedState)
        {
            return true;
        }
        Sleep(100);
    }
    return false;
}

static void LoadDriver(HWND hwndDlg)
{
    std::string driverName = GetDriverName(hwndDlg);
    std::string driverPath = GetDriverPath(hwndDlg);
    if (driverName.empty())
    {
        MessageBoxA(hwndDlg, "Driver name is empty.", "Load driver", MB_ICONERROR);
        return;
    }
    if (driverPath.empty())
    {
        MessageBoxA(hwndDlg, "Driver path is empty.", "Load driver", MB_ICONERROR);
        return;
    }

    char fullPath[MAX_PATH] = "";
    if (GetFullPathNameA(driverPath.c_str(), sizeof(fullPath), fullPath, NULL) == 0 || !FileExists(fullPath))
    {
        MessageBoxA(hwndDlg, "Driver .sys file was not found.", "Load driver", MB_ICONERROR);
        return;
    }
    SetWindowTextA(GetDlgItem(hwndDlg, IDC_EDT_DRIVER_PATH), fullPath);

    DRIVER_STATE state = {};
    if (!QueryDriverState(driverName.c_str(), &state))
    {
        ShowWin32Error(hwndDlg, "Load driver", GetLastError());
        return;
    }

    if (state.ServiceRunning)
    {
        if (state.DevicePresent)
        {
            MessageBoxA(hwndDlg, "Driver is already loaded and the device exists.", "Load driver", MB_ICONINFORMATION);
        }
        else
        {
            MessageBoxA(hwndDlg, "Driver service is running but the device name is missing.", "Load driver", MB_ICONWARNING);
        }
        UpdateDriverStatus(hwndDlg);
        return;
    }

    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (scm == NULL)
    {
        ShowWin32Error(hwndDlg, "Load driver", GetLastError());
        return;
    }

    SC_HANDLE service = OpenServiceA(scm,
                                     driverName.c_str(),
                                     SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP | SERVICE_CHANGE_CONFIG | DELETE);
    if (service == NULL)
    {
        DWORD error = GetLastError();
        if (error != ERROR_SERVICE_DOES_NOT_EXIST)
        {
            CloseServiceHandle(scm);
            ShowWin32Error(hwndDlg, "Load driver", error);
            return;
        }

        service = CreateServiceA(scm,
                                 driverName.c_str(),
                                 driverName.c_str(),
                                 SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP | SERVICE_CHANGE_CONFIG | DELETE,
                                 SERVICE_KERNEL_DRIVER,
                                 SERVICE_DEMAND_START,
                                 SERVICE_ERROR_NORMAL,
                                 fullPath,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL);
        if (service == NULL)
        {
            DWORD createError = GetLastError();
            CloseServiceHandle(scm);
            ShowWin32Error(hwndDlg, "Load driver", createError);
            return;
        }
    }
    else if (!ChangeServiceConfigA(service,
                                   SERVICE_NO_CHANGE,
                                   SERVICE_NO_CHANGE,
                                   SERVICE_NO_CHANGE,
                                   fullPath,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL,
                                   driverName.c_str()))
    {
        DWORD changeError = GetLastError();
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        ShowWin32Error(hwndDlg, "Load driver", changeError);
        return;
    }

    if (!StartServiceA(service, 0, NULL))
    {
        DWORD startError = GetLastError();
        if (startError != ERROR_SERVICE_ALREADY_RUNNING)
        {
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            ShowWin32Error(hwndDlg, "Load driver", startError);
            return;
        }
    }

    WaitForServiceState(service, SERVICE_RUNNING);
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    SaveDriverSettings(hwndDlg);
    UpdateDriverStatus(hwndDlg);

    DRIVER_STATE refreshedState = {};
    if (!QueryDriverState(driverName.c_str(), &refreshedState))
    {
        ShowWin32Error(hwndDlg, "Load driver", GetLastError());
        return;
    }
    if (!refreshedState.DevicePresent)
    {
        char message[256] = "";
        _snprintf_s(message, sizeof(message), _TRUNCATE, "Driver service started, but device name \\\\.\\%s is missing.", driverName.c_str());
        MessageBoxA(hwndDlg, message, "Load driver", MB_ICONWARNING);
        return;
    }

    MessageBoxA(hwndDlg, "Driver loaded successfully.", "Load driver", MB_ICONINFORMATION);
}

static void UnloadDriver(HWND hwndDlg)
{
    std::string driverName = GetDriverName(hwndDlg);
    if (driverName.empty())
    {
        MessageBoxA(hwndDlg, "Driver name is empty.", "Unload driver", MB_ICONERROR);
        return;
    }

    DRIVER_STATE state = {};
    if (!QueryDriverState(driverName.c_str(), &state))
    {
        ShowWin32Error(hwndDlg, "Unload driver", GetLastError());
        return;
    }
    if (!state.ServiceInstalled)
    {
        MessageBoxA(hwndDlg, "Driver service is not installed.", "Unload driver", MB_ICONINFORMATION);
        UpdateDriverStatus(hwndDlg);
        return;
    }

    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm == NULL)
    {
        ShowWin32Error(hwndDlg, "Unload driver", GetLastError());
        return;
    }

    SC_HANDLE service = OpenServiceA(scm,
                                     driverName.c_str(),
                                     SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE);
    if (service == NULL)
    {
        DWORD error = GetLastError();
        CloseServiceHandle(scm);
        ShowWin32Error(hwndDlg, "Unload driver", error);
        return;
    }

    if (state.ServiceRunning)
    {
        SERVICE_STATUS serviceStatus = {};
        if (!ControlService(service, SERVICE_CONTROL_STOP, &serviceStatus))
        {
            DWORD stopError = GetLastError();
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            ShowWin32Error(hwndDlg, "Unload driver", stopError);
            return;
        }
        WaitForServiceState(service, SERVICE_STOPPED);
    }

    if (!DeleteService(service))
    {
        DWORD deleteError = GetLastError();
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        ShowWin32Error(hwndDlg, "Unload driver", deleteError);
        return;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    UpdateDriverStatus(hwndDlg);
    MessageBoxA(hwndDlg, "Driver unloaded and service entry removed.", "Unload driver", MB_ICONINFORMATION);
}

static void TitanHideCall(HWND hwndDlg, HIDE_COMMAND Command)
{
    std::string driverName = GetDriverName(hwndDlg);
    if (driverName.empty())
    {
        MessageBoxA(hwndDlg, "Driver name is empty.", "Driver", MB_ICONERROR);
        return;
    }
    if (!IsDevicePresent(driverName.c_str()))
    {
        MessageBoxA(hwndDlg, "Driver device name does not exist. Load the driver first.", "Driver", MB_ICONERROR);
        UpdateDriverStatus(hwndDlg);
        return;
    }

    char devicePath[260] = "\\\\.\\"; 
    strncat_s(devicePath, driverName.c_str(), _TRUNCATE);
    SaveDriverSettings(hwndDlg);
    HANDLE hDevice = CreateFileA(devicePath, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    if(hDevice == INVALID_HANDLE_VALUE)
    {
        ShowWin32Error(hwndDlg, "Driver", GetLastError());
        return;
    }
    HIDE_INFO HideInfo;
    HideInfo.Command = Command;
    HideInfo.Pid = GetDlgItemInt(hwndDlg, IDC_EDT_PID, 0, FALSE);
    HideInfo.Type = GetTypeDword(hwndDlg);
    DWORD written = 0;
    if(WriteFile(hDevice, &HideInfo, sizeof(HIDE_INFO), &written, 0))
        MessageBoxA(hwndDlg, "Data written!", "Done", MB_ICONINFORMATION);
    else
        ShowWin32Error(hwndDlg, "Driver", GetLastError());
    CloseHandle(hDevice);
}

static BOOL CALLBACK DlgMain(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch(uMsg)
    {
    case WM_INITDIALOG:
    {
        SetWindowTextA(GetDlgItem(hwndDlg, IDC_EDT_DRIVER), kDefaultDriverName);
        SetOptionChecks(hwndDlg, GetAllOptions());
        char driverName[256] = "";
        GetPrivateProfileStringA(kIniSection, "DriverName", kDefaultDriverName, driverName, sizeof(driverName), iniPath);
        SetWindowTextA(GetDlgItem(hwndDlg, IDC_EDT_DRIVER), driverName);
        SetDefaultDriverPath(hwndDlg);
        UpdateDriverStatus(hwndDlg);
    }
    return TRUE;

    case WM_CLOSE:
    {
        EndDialog(hwndDlg, 0);
    }
    return TRUE;

    case WM_COMMAND:
    {
        switch(LOWORD(wParam))
        {
        case IDC_BTN_HIDE:
        {
            TitanHideCall(hwndDlg, HidePid);
        }
        return TRUE;

        case IDC_BTN_UNHIDE:
        {
            TitanHideCall(hwndDlg, UnhidePid);
        }
        return TRUE;

        case IDC_BTN_UNHIDEALL:
        {
            TitanHideCall(hwndDlg, UnhideAll);
        }
        return TRUE;

        case IDC_BTN_SELECTALL:
        {
            SetOptionChecks(hwndDlg, GetAllOptions());
        }
        return TRUE;

        case IDC_BTN_SELECTNONE:
        {
            SetOptionChecks(hwndDlg, 0);
        }
        return TRUE;

        case IDC_BTN_SELECT_SAFE:
        {
            SetOptionChecks(hwndDlg, GetAllOptions() & ~(ULONG)HideThreadHideFromDebugger);
        }
        return TRUE;

        case IDC_BTN_LOAD_DRIVER:
        {
            LoadDriver(hwndDlg);
        }
        return TRUE;

        case IDC_BTN_UNLOAD_DRIVER:
        {
            UnloadDriver(hwndDlg);
        }
        return TRUE;

        case IDC_BTN_REFRESH_DRIVER:
        {
            SaveDriverSettings(hwndDlg);
            UpdateDriverStatus(hwndDlg);
        }
        return TRUE;
        }
    }
    return TRUE;
    }
    return FALSE;
}


int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
    hInst = hInstance;
    InitCommonControls();
    GetModuleFileNameA(hInstance, modulePath, sizeof(modulePath));
    strcpy_s(iniPath, modulePath);
    auto ext = strrchr(iniPath, '.');
    if (ext != nullptr)
    {
        *ext = '\0'; // remove the extension
    }
    strncat_s(iniPath, ".ini", _TRUNCATE); // append .ini
    return (int)DialogBox(hInst, MAKEINTRESOURCE(DLG_MAIN), NULL, (DLGPROC)DlgMain);
}
