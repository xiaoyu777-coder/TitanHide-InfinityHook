#include "plugin.h"
#include "resource.h"
#include <windows.h>
#include <stdio.h>
#include <string>
#include <utility>
#include "../TitanHide/TitanHide.h"

static DWORD pid = 0;
static bool hidden = false;
static std::string driverName = "TitanHide";

enum TITANHIDE_MENU_ENTRY
{
    MENU_OPTIONS = 1
};

static const std::pair<int, HIDE_TYPE> gOptions[] =
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

static void NormalizeDriverName(std::string& name)
{
    while (!name.empty() && (name[0] == ' ' || name[0] == '\t'))
        name.erase(0, 1);
    while (!name.empty() && (name[name.size() - 1] == ' ' || name[name.size() - 1] == '\t'))
        name.erase(name.size() - 1, 1);
    if (name.compare(0, 4, "\\\\.\\") == 0)
        name.erase(0, 4);
}

static std::string GetNormalizedDriverName()
{
    std::string name = driverName;
    NormalizeDriverName(name);
    return name;
}

static void SetDriverName(const std::string& name)
{
    driverName = name;
    BridgeSettingSet("TitanHide", "DriverName", driverName.c_str());
    BridgeSettingFlush();
}

static std::string GetDriverNameFromDialog(HWND hwndDialog)
{
    char name[256] = "";
    GetDlgItemTextA(hwndDialog, IDC_EDT_DRIVER_NAME, name, sizeof(name));
    std::string normalizedName = name;
    NormalizeDriverName(normalizedName);
    return normalizedName;
}

static ULONG GetTitanHideOptions()
{
    duint options = 0;
    if (!BridgeSettingGetUint("TitanHide", "Options", &options))
        options = 0xffffffff;
    return (ULONG)options;
}

static void SetTitanHideOptions(ULONG options)
{
    BridgeSettingSetUint("TitanHide", "Options", (duint)options);
    BridgeSettingFlush();
}

static ULONG GetTypeDword(HWND hwndDialog)
{
    ULONG options = 0;
    for (const auto& option : gOptions)
    {
        if (IsDlgButtonChecked(hwndDialog, option.first))
            options |= (ULONG)option.second;
    }
    return options;
}

static void SetOptionChecks(HWND hwndDialog, ULONG options)
{
    for (const auto& option : gOptions)
    {
        CheckDlgButton(hwndDialog, option.first, (options & (ULONG)option.second) ? BST_CHECKED : BST_UNCHECKED);
    }
}

static ULONG GetAllOptions()
{
    ULONG options = 0;
    for (const auto& option : gOptions)
        options |= (ULONG)option.second;
    return options;
}

static ULONG GetTitanHideOptions(HWND hwndDialog)
{
    if (GetTypeDword(hwndDialog) == 0)
        return 0;

    ULONG options = 0xffffffff;
    for (const auto& option : gOptions)
    {
        if (!IsDlgButtonChecked(hwndDialog, option.first))
            options &= ~(ULONG)option.second;
    }
    return options;
}

static void UpdateOptionsTip(HWND hwndDialog)
{
    char text[64] = "";
    _snprintf_s(text, sizeof(text), _TRUNCATE, "TitanHideOptions (0x%08lX)", GetTitanHideOptions(hwndDialog));
    SetWindowTextA(GetDlgItem(hwndDialog, IDC_TXT_OPTIONS_TIP), text);
}

static bool IsDriverLoaded(const std::string& name)
{
    if (name.empty())
        return false;

    char target[512] = "";
    if (QueryDosDeviceA(name.c_str(), target, sizeof(target)) != 0)
        return true;

    return GetLastError() == ERROR_INSUFFICIENT_BUFFER;
}

static void UpdateDriverStatus(HWND hwndDialog, const std::string& name)
{
    char text[256] = "";
    if (name.empty())
    {
        strcpy_s(text, "Driver: not configured");
    }
    else
    {
        _snprintf_s(text,
                    sizeof(text),
                    _TRUNCATE,
                    "Driver: %s (\\\\.\\%s)",
                    IsDriverLoaded(name) ? "loaded" : "not loaded",
                    name.c_str());
    }
    SetWindowTextA(GetDlgItem(hwndDialog, IDC_TXT_DRIVER_STATUS), text);
}

static void NotifyOptionsReloadRequired(HWND owner)
{
    _plugin_logputs("[" PLUGIN_NAME "] Options saved. Reload the debuggee for changes to take effect.");
    MessageBoxA(owner,
                "Options saved. Reload the debuggee for changes to take effect.",
                PLUGIN_NAME,
                MB_ICONINFORMATION);
}

static HINSTANCE GetPluginInstance()
{
    HMODULE module = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(&GetPluginInstance),
                       &module);
    return (HINSTANCE)module;
}

static INT_PTR CALLBACK OptionsDlgProc(HWND hwndDialog, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (uMsg)
    {
    case WM_INITDIALOG:
    {
        SetOptionChecks(hwndDialog, GetTitanHideOptions());
        SetDlgItemTextA(hwndDialog, IDC_EDT_DRIVER_NAME, GetNormalizedDriverName().c_str());
        UpdateDriverStatus(hwndDialog, GetNormalizedDriverName());
        UpdateOptionsTip(hwndDialog);
    }
    return TRUE;

    case WM_CLOSE:
    {
        EndDialog(hwndDialog, IDCANCEL);
    }
    return TRUE;

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            const std::string newDriverName = GetDriverNameFromDialog(hwndDialog);
            if (newDriverName.empty())
            {
                MessageBoxA(hwndDialog, "TitanHideName cannot be empty.", PLUGIN_NAME, MB_ICONERROR);
                SetFocus(GetDlgItem(hwndDialog, IDC_EDT_DRIVER_NAME));
                return TRUE;
            }

            const std::string oldDriverName = GetNormalizedDriverName();
            if (newDriverName != oldDriverName)
            {
                SetDriverName(newDriverName);
                _plugin_logprintf("[" PLUGIN_NAME "] New driver name: '%s'\n", driverName.c_str());
            }
            UpdateDriverStatus(hwndDialog, newDriverName);

            const ULONG oldOptions = GetTitanHideOptions();
            const ULONG newOptions = GetTitanHideOptions(hwndDialog);
            SetTitanHideOptions(newOptions);
            _plugin_logprintf("[" PLUGIN_NAME "] New options: 0x%08lX\n", newOptions);
            if (newOptions != oldOptions)
                NotifyOptionsReloadRequired(hwndDialog);
            EndDialog(hwndDialog, IDOK);
        }
        return TRUE;

        case IDCANCEL:
        {
            EndDialog(hwndDialog, IDCANCEL);
        }
        return TRUE;

        case IDC_BTN_REFRESH_DRIVER:
        {
            UpdateDriverStatus(hwndDialog, GetDriverNameFromDialog(hwndDialog));
        }
        return TRUE;

        case IDC_BTN_SELECTALL:
        {
            SetOptionChecks(hwndDialog, GetAllOptions());
            UpdateOptionsTip(hwndDialog);
        }
        return TRUE;

        case IDC_BTN_SELECTNONE:
        {
            SetOptionChecks(hwndDialog, 0);
            UpdateOptionsTip(hwndDialog);
        }
        return TRUE;

        case IDC_BTN_SELECT_SAFE:
        {
            SetOptionChecks(hwndDialog, GetAllOptions() & ~(ULONG)HideThreadHideFromDebugger);
            UpdateOptionsTip(hwndDialog);
        }
        return TRUE;
        }

        if (LOWORD(wParam) == IDC_EDT_DRIVER_NAME && HIWORD(wParam) == EN_CHANGE)
        {
            UpdateDriverStatus(hwndDialog, GetDriverNameFromDialog(hwndDialog));
            return TRUE;
        }

        for (const auto& option : gOptions)
        {
            if (LOWORD(wParam) == option.first && HIWORD(wParam) == BN_CLICKED)
            {
                UpdateOptionsTip(hwndDialog);
                return TRUE;
            }
        }
    }
    return TRUE;
    }
    return FALSE;
}

static void ShowOptionsDialog()
{
    DialogBoxA(GetPluginInstance(), MAKEINTRESOURCEA(DLG_OPTIONS), hwndDlg, OptionsDlgProc);
}

static void CBMENUENTRY(CBTYPE cbType, void* callbackInfo)
{
    UNREFERENCED_PARAMETER(cbType);

    auto info = static_cast<PLUG_CB_MENUENTRY*>(callbackInfo);
    if (info != nullptr && info->hEntry == MENU_OPTIONS)
        ShowOptionsDialog();
}

static bool TitanHideCall(HIDE_COMMAND Command)
{
    const std::string name = GetNormalizedDriverName();
    auto path = "\\\\.\\" + name;
    HANDLE hDevice = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        _plugin_logputs("[" PLUGIN_NAME "] Could not open TitanHide handle (wrong driver name?)");
        return false;
    }
    HIDE_INFO HideInfo;
    HideInfo.Command = Command;
    HideInfo.Pid = pid;
    HideInfo.Type = GetTitanHideOptions();
    DWORD written = 0;
    auto result = false;
    if (WriteFile(hDevice, &HideInfo, sizeof(HIDE_INFO), &written, 0))
    {
        _plugin_logprintf("[" PLUGIN_NAME "] Process %shidden!\n", Command == UnhidePid ? "un" : "");
        result = true;
    }
    else
    {
        _plugin_logputs("[" PLUGIN_NAME "] WriteFile error...");
    }
    CloseHandle(hDevice);
    return result;
}

static bool cbTitanHide(int argc, char* argv[])
{
    if (!hidden)
    {
        _plugin_logprintf("[" PLUGIN_NAME "] Hiding PID %X (%ud)\n", pid, pid);
        if (TitanHideCall(HidePid))
        {
            DbgCmdExecDirect("hide");
            hidden = true;
        }
    }
    return hidden;
}

static bool cbTitanUnhide(int argc, char* argv[])
{
    if (hidden)
    {
        _plugin_logprintf("[" PLUGIN_NAME "] Unhiding PID %X (%ud)\n", pid, pid);
        if (TitanHideCall(UnhidePid))
            hidden = false;
    }
    return !hidden;
}

static bool cbTitanHideOptions(int argc, char* argv[])
{
    if (argc < 2)
    {
        _plugin_logprintf("[" PLUGIN_NAME "] Options: 0x%08X\n", GetTitanHideOptions());
    }
    else
    {
        duint options = DbgValFromString(argv[1]);
        const ULONG oldOptions = GetTitanHideOptions();
        SetTitanHideOptions((ULONG)(options & 0xffffffff));
        _plugin_logprintf("[" PLUGIN_NAME "] New options: 0x%08X\n", GetTitanHideOptions());
        if (GetTitanHideOptions() != oldOptions)
            _plugin_logputs("[" PLUGIN_NAME "] Options saved. Reload the debuggee for changes to take effect.");
    }
    return true;
}

static bool cbTitanHideName(int argc, char* argv[])
{
    if (argc < 2)
    {
        _plugin_logprintf("[" PLUGIN_NAME "] Current driver name: '%s'\n", driverName.c_str());
    }
    else
    {
        std::string name = argv[1];
        NormalizeDriverName(name);
        if (!name.empty())
            SetDriverName(name);
        _plugin_logprintf("[" PLUGIN_NAME "] New driver name: '%s'\n", driverName.c_str());
    }
    return true;
}

PLUG_EXPORT void CBCREATEPROCESS(CBTYPE cbType, PLUG_CB_CREATEPROCESS* info)
{
    pid = info->fdProcessInfo->dwProcessId;
}

PLUG_EXPORT void CBATTACH(CBTYPE cbType, PLUG_CB_ATTACH* info)
{
    pid = info->dwProcessId;
}

PLUG_EXPORT void CBSYSTEMBREAKPOINT(CBTYPE cbType, PLUG_CB_SYSTEMBREAKPOINT* info)
{
    char* argv = "TitanHide";
    cbTitanHide(1, &argv);
}

PLUG_EXPORT void CBSTOPDEBUG(CBTYPE cbType, PLUG_CB_STOPDEBUG* info)
{
    char* argv = "TitanUnhide";
    cbTitanUnhide(1, &argv);
}

void TitanHideInit(PLUG_INITSTRUCT* initStruct)
{
    char setting[MAX_SETTING_SIZE] = "";
    BridgeSettingGet("TitanHide", "DriverName", setting);
    if (setting[0] != '\0')
    {
        driverName = setting;
        NormalizeDriverName(driverName);
    }

    _plugin_registercommand(pluginHandle, "TitanHide", cbTitanHide, true);
    _plugin_registercommand(pluginHandle, "TitanUnhide", cbTitanUnhide, true);
    _plugin_registercommand(pluginHandle, "TitanHideOptions", cbTitanHideOptions, false);
    _plugin_registercommand(pluginHandle, "TitanHideName", cbTitanHideName, false);
    _plugin_registercallback(pluginHandle, CB_MENUENTRY, CBMENUENTRY);
}

void TitanHideSetup(PLUG_SETUPSTRUCT* setupStruct)
{
    _plugin_menuaddentry(setupStruct->hMenu, MENU_OPTIONS, "&Options...");
}

void TitanHideStop()
{
    if (hMenu)
        _plugin_menuclear(hMenu);
    _plugin_unregistercallback(pluginHandle, CB_MENUENTRY);
    _plugin_unregistercommand(pluginHandle, "TitanHideName");
    _plugin_unregistercommand(pluginHandle, "TitanHideOptions");
    _plugin_unregistercommand(pluginHandle, "TitanUnhide");
    _plugin_unregistercommand(pluginHandle, "TitanHide");
}
