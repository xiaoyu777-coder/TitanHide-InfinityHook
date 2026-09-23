#include <windows.h>
#include <winsvc.h>
#include <stdio.h>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string>

#include "..\TitanHide\TitanHide.h"

static const char* kDefaultDriverName = "InfinityHookHide";

struct HIDE_OPTION
{
    const char* Name;
    HIDE_TYPE Type;
    const char* Description;
};

static const HIDE_OPTION kHideOptions[] =
{
    {"process-debug-flags", HideProcessDebugFlags, "NtQueryInformationProcess(ProcessDebugFlags)"},
    {"process-debug-port", HideProcessDebugPort, "NtQueryInformationProcess(ProcessDebugPort)"},
    {"process-debug-object-handle", HideProcessDebugObjectHandle, "NtQueryInformationProcess(ProcessDebugObjectHandle)"},
    {"debug-object", HideDebugObject, "NtQueryObject(DebugObject)"},
    {"system-debugger-information", HideSystemDebuggerInformation, "NtQuerySystemInformation(SystemKernelDebuggerInformation)"},
    {"ntclose", HideNtClose, "NtClose protected-handle exceptions"},
    {"thread-hide-from-debugger", HideThreadHideFromDebugger, "Undo NtSetInformationThread(ThreadHideFromDebugger)"},
    {"get-context-thread", HideNtGetContextThread, "Protect debug registers in NtGetContextThread"},
    {"set-context-thread", HideNtSetContextThread, "Protect debug registers in NtSetContextThread"},
    {"system-debug-control", HideNtSystemDebugControl, "NtSystemDebugControl"}
};

struct DRIVER_STATE
{
    bool ServiceInstalled;
    bool ServiceRunning;
    bool DevicePresent;
    DWORD ServiceState;
};

struct COMMAND_LINE
{
    std::string Command;
    ULONG Pid;
    bool HavePid;
    std::string DriverName;
    std::string DriverPath;
    bool DriverPathSpecified;
    bool UseSafeOptions;
    bool OptionsSpecified;
    std::string Options;
};

static std::string ToLower(std::string value)
{
    for(size_t i = 0; i < value.size(); i++)
        value[i] = (char)tolower((unsigned char)value[i]);
    return value;
}

static std::string Trim(const std::string& value)
{
    size_t begin = 0;
    size_t end = value.size();
    while(begin < end && isspace((unsigned char)value[begin]))
        begin++;
    while(end > begin && isspace((unsigned char)value[end - 1]))
        end--;
    return value.substr(begin, end - begin);
}

static std::string NormalizeOptionName(std::string value)
{
    value = ToLower(Trim(value));
    for(size_t i = 0; i < value.size(); i++)
    {
        if(value[i] == '_')
            value[i] = '-';
    }
    return value;
}

static std::string NormalizeDriverName(std::string value)
{
    value = Trim(value);
    const char* prefixes[] = {"\\\\.\\", "\\\\??\\"};
    for(size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
    {
        std::string prefix = prefixes[i];
        if(value.compare(0, prefix.size(), prefix) == 0)
        {
            value.erase(0, prefix.size());
            break;
        }
    }
    return Trim(value);
}

static void PrintUsage()
{
    printf(
        "TitanHide CLI\n\n"
        "Usage:\n"
        "  TitanHideCLI hide <pid> [options]\n"
        "  TitanHideCLI unhide <pid> [options]\n"
        "  TitanHideCLI unhide-all [options]\n"
        "  TitanHideCLI load [options]\n"
        "  TitanHideCLI unload [options]\n"
        "  TitanHideCLI status [options]\n"
        "  TitanHideCLI options\n\n"
        "PID may be decimal, hexadecimal, or 'current' for the CLI process.\n\n"
        "Options:\n"
        "  --driver-name <name>   Driver/service/device name (default: %s)\n"
        "  --driver-path <path>   Driver image for 'load'\n"
        "  --pid <pid>            Alternative to the positional PID\n"
        "  --all                  Use every supported hide option (default)\n"
        "  --safe                 Use every option except ThreadHideFromDebugger\n"
        "  --options <list>       Comma-separated option names or a 0x mask\n"
        "  --help                 Show this help\n\n"
        "Examples:\n"
        "  TitanHideCLI hide 1234\n"
        "  TitanHideCLI hide 1234 --safe --driver-name NotTitanHide\n"
        "  TitanHideCLI unhide 1234 --options ntclose,get-context-thread\n"
        "  TitanHideCLI load --driver-path C:\\drivers\\TitanHide.sys\n",
        kDefaultDriverName);
}

static void PrintOptions()
{
    printf("Supported hide options (mask 0xFFFFFFFF uses all of them):\n");
    for(size_t i = 0; i < sizeof(kHideOptions) / sizeof(kHideOptions[0]); i++)
        printf("  %-30s 0x%08lX  %s\n", kHideOptions[i].Name, (ULONG)kHideOptions[i].Type, kHideOptions[i].Description);
}

static std::string GetWin32ErrorMessage(DWORD error)
{
    char* message = NULL;
    DWORD length = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                  NULL, error, 0, (LPSTR)&message, 0, NULL);
    std::string result;
    if(length != 0 && message != NULL)
    {
        result.assign(message, length);
        while(!result.empty() && (result[result.size() - 1] == '\r' || result[result.size() - 1] == '\n'))
            result.erase(result.size() - 1);
    }
    else
    {
        char fallback[64] = "";
        _snprintf_s(fallback, sizeof(fallback), _TRUNCATE, "Win32 error 0x%08lX", error);
        result = fallback;
    }
    if(message != NULL)
        LocalFree(message);
    return result;
}

static bool ReportWin32Error(const char* action, DWORD error)
{
    fprintf(stderr, "%s failed: %s (0x%08lX)\n", action, GetWin32ErrorMessage(error).c_str(), error);
    return false;
}

static bool ParsePid(const std::string& text, ULONG* pid)
{
    std::string lower = ToLower(Trim(text));
    if(lower == "current" || lower == "self")
    {
        *pid = GetCurrentProcessId();
        return true;
    }

    errno = 0;
    char* end = NULL;
    unsigned long parsed = strtoul(lower.c_str(), &end, 0);
    if(errno == ERANGE || end == lower.c_str() || *end != '\0')
    {
        fprintf(stderr, "Invalid PID: '%s'\n", text.c_str());
        return false;
    }
    if(parsed == 0 || parsed > (ULONG)-1)
    {
        fprintf(stderr, "PID is outside the supported range: '%s'\n", text.c_str());
        return false;
    }
    *pid = (ULONG)parsed;
    return true;
}

static bool ParseOptions(const std::string& text, ULONG* options)
{
    std::string value = Trim(text);
    if(value.empty())
    {
        fprintf(stderr, "The options list is empty.\n");
        return false;
    }

    if(value.compare(0, 2, "0x") == 0 || value.compare(0, 2, "0X") == 0)
    {
        errno = 0;
        char* end = NULL;
        unsigned long parsed = strtoul(value.c_str(), &end, 0);
        if(errno == ERANGE || end == value.c_str() || *end != '\0' || parsed == 0)
        {
            fprintf(stderr, "Invalid or empty options mask: '%s'\n", text.c_str());
            return false;
        }
        *options = (ULONG)parsed;
        return true;
    }

    ULONG parsedOptions = 0;
    size_t begin = 0;
    while(begin <= value.size())
    {
        size_t end = value.find(',', begin);
        if(end == std::string::npos)
            end = value.size();
        std::string item = NormalizeOptionName(value.substr(begin, end - begin));
        if(!item.empty())
        {
            if(item == "all")
                parsedOptions = 0xFFFFFFFF;
            else
            {
                bool found = false;
                for(size_t i = 0; i < sizeof(kHideOptions) / sizeof(kHideOptions[0]); i++)
                {
                    if(item == kHideOptions[i].Name)
                    {
                        parsedOptions |= (ULONG)kHideOptions[i].Type;
                        found = true;
                        break;
                    }
                }
                if(!found)
                {
                    fprintf(stderr, "Unknown hide option: '%s'. Run 'TitanHideCLI options' for the list.\n", item.c_str());
                    return false;
                }
            }
        }
        if(end == value.size())
            break;
        begin = end + 1;
    }

    if(parsedOptions == 0)
    {
        fprintf(stderr, "No valid hide options were selected.\n");
        return false;
    }
    *options = parsedOptions;
    return true;
}

static bool ParseCommandLine(int argc, char** argv, COMMAND_LINE* commandLine)
{
    commandLine->Command = ToLower(Trim(argc > 1 ? argv[1] : ""));
    commandLine->DriverName = kDefaultDriverName;
    commandLine->Pid = 0;
    commandLine->HavePid = false;
    commandLine->UseSafeOptions = false;
    commandLine->OptionsSpecified = false;
    commandLine->DriverPathSpecified = false;

    if(commandLine->Command.empty() || commandLine->Command == "help" || commandLine->Command == "--help" ||
       commandLine->Command == "-h" || commandLine->Command == "/?")
    {
        PrintUsage();
        return false;
    }
    if(commandLine->Command == "options" || commandLine->Command == "list-options")
    {
        PrintOptions();
        return false;
    }
    if(commandLine->Command != "hide" && commandLine->Command != "unhide" && commandLine->Command != "unhide-all" &&
       commandLine->Command != "load" && commandLine->Command != "unload" && commandLine->Command != "status")
    {
        fprintf(stderr, "Unknown command: '%s'\n\n", argv[1]);
        PrintUsage();
        return false;
    }

    bool needPid = commandLine->Command == "hide" || commandLine->Command == "unhide";
    for(int i = 2; i < argc; i++)
    {
        std::string argument = argv[i];
        std::string inlineValue;
        size_t separator = argument.find('=');
        bool hasInlineValue = argument.compare(0, 2, "--") == 0 && separator != std::string::npos;
        if(hasInlineValue)
        {
            inlineValue = argument.substr(separator + 1);
            argument = argument.substr(0, separator);
        }

        auto requireValue = [&](const char* option, std::string* value) -> bool
        {
            if(hasInlineValue)
            {
                *value = inlineValue;
                return true;
            }
            if(i + 1 >= argc)
            {
                fprintf(stderr, "Missing value for %s\n", option);
                return false;
            }
            *value = argv[++i];
            return true;
        };

        if(argument == "--driver-name")
        {
            if(!requireValue("--driver-name", &commandLine->DriverName))
                return false;
        }
        else if(argument == "--driver-path")
        {
            if(!requireValue("--driver-path", &commandLine->DriverPath))
                return false;
            commandLine->DriverPathSpecified = true;
        }
        else if(argument == "--pid")
        {
            std::string value;
            if(!requireValue("--pid", &value) || !ParsePid(value, &commandLine->Pid))
                return false;
            if(commandLine->HavePid)
            {
                fprintf(stderr, "PID was specified more than once.\n");
                return false;
            }
            commandLine->HavePid = true;
        }
        else if(argument == "--safe")
            commandLine->UseSafeOptions = true;
        else if(argument == "--options" || argument == "--mask")
        {
            if(!requireValue(argument.c_str(), &commandLine->Options))
                return false;
            commandLine->OptionsSpecified = true;
        }
        else if(argument == "--help" || argument == "-h" || argument == "/?")
        {
            PrintUsage();
            return false;
        }
        else if(argument[0] != '-' && argument[0] != '/' && !commandLine->HavePid)
        {
            if(!ParsePid(argument, &commandLine->Pid))
                return false;
            commandLine->HavePid = true;
        }
        else
        {
            fprintf(stderr, "Unknown option: '%s'\n\n", argv[i]);
            PrintUsage();
            return false;
        }
    }

    if((commandLine->UseSafeOptions ? 1 : 0) + (commandLine->OptionsSpecified ? 1 : 0) > 1)
    {
        fprintf(stderr, "Use only one of --all, --safe, or --options.\n");
        return false;
    }
    if(needPid && !commandLine->HavePid)
    {
        fprintf(stderr, "The '%s' command requires a PID.\n", commandLine->Command.c_str());
        return false;
    }
    if(!needPid && commandLine->HavePid)
    {
        fprintf(stderr, "The '%s' command does not accept a PID.\n", commandLine->Command.c_str());
        return false;
    }

    commandLine->DriverName = NormalizeDriverName(commandLine->DriverName);
    if(commandLine->DriverName.empty() || commandLine->DriverName.size() > 255)
    {
        fprintf(stderr, "The driver name is empty or too long.\n");
        return false;
    }
    if(commandLine->DriverPathSpecified && commandLine->DriverPath.empty())
    {
        fprintf(stderr, "The driver path is empty.\n");
        return false;
    }
    return true;
}

static bool FileExists(const char* path)
{
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static std::string GetModuleDirectory()
{
    char modulePath[MAX_PATH] = "";
    if(GetModuleFileNameA(NULL, modulePath, sizeof(modulePath)) == 0)
        return ".";
    std::string directory = modulePath;
    size_t separator = directory.find_last_of("\\/");
    if(separator == std::string::npos)
        return ".";
    directory.erase(separator);
    return directory;
}

static bool GetAbsoluteExistingDriverPath(std::string specified, std::string* result)
{
    if(!specified.empty())
    {
        char fullPath[MAX_PATH] = "";
        if(GetFullPathNameA(specified.c_str(), sizeof(fullPath), fullPath, NULL) == 0)
            return ReportWin32Error("GetFullPathName", GetLastError());
        if(!FileExists(fullPath))
        {
            fprintf(stderr, "Driver file was not found: %s\n", fullPath);
            return false;
        }
        *result = fullPath;
        return true;
    }

    const std::string baseDirectory = GetModuleDirectory();
    const char* relativeCandidates[] =
    {
        "TitanHide.sys",
        "..\\TitanHide.sys",
        "..\\Release\\TitanHide.sys",
        "..\\x64\\Release\\TitanHide.sys"
    };
    char fullPath[MAX_PATH] = "";
    for(size_t i = 0; i < sizeof(relativeCandidates) / sizeof(relativeCandidates[0]); i++)
    {
        char candidate[MAX_PATH] = "";
        _snprintf_s(candidate, sizeof(candidate), _TRUNCATE, "%s\\%s", baseDirectory.c_str(), relativeCandidates[i]);
        if(GetFullPathNameA(candidate, sizeof(fullPath), fullPath, NULL) != 0 && FileExists(fullPath))
        {
            *result = fullPath;
            return true;
        }
    }

    fprintf(stderr, "Unable to find TitanHide.sys. Pass --driver-path explicitly.\n");
    return false;
}

static bool IsDevicePresent(const std::string& driverName)
{
    char target[512] = "";
    if(QueryDosDeviceA(driverName.c_str(), target, sizeof(target)) != 0)
        return true;
    return GetLastError() == ERROR_INSUFFICIENT_BUFFER;
}

static bool QueryDriverState(const std::string& driverName, DRIVER_STATE* state)
{
    state->ServiceInstalled = false;
    state->ServiceRunning = false;
    state->DevicePresent = IsDevicePresent(driverName);
    state->ServiceState = SERVICE_STOPPED;

    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if(scm == NULL)
        return false;

    SC_HANDLE service = OpenServiceA(scm, driverName.c_str(), SERVICE_QUERY_STATUS);
    if(service != NULL)
    {
        state->ServiceInstalled = true;
        SERVICE_STATUS_PROCESS status = {};
        DWORD bytesNeeded = 0;
        if(QueryServiceStatusEx(service,
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

static const char* GetServiceStateText(DWORD state)
{
    switch(state)
    {
    case SERVICE_STOPPED:
        return "stopped";
    case SERVICE_START_PENDING:
        return "start-pending";
    case SERVICE_STOP_PENDING:
        return "stop-pending";
    case SERVICE_RUNNING:
        return "running";
    case SERVICE_CONTINUE_PENDING:
        return "continue-pending";
    case SERVICE_PAUSE_PENDING:
        return "pause-pending";
    case SERVICE_PAUSED:
        return "paused";
    default:
        return "unknown";
    }
}

static bool WaitForServiceState(SC_HANDLE service, DWORD expectedState)
{
    for(int i = 0; i < 50; i++)
    {
        SERVICE_STATUS_PROCESS status = {};
        DWORD bytesNeeded = 0;
        if(!QueryServiceStatusEx(service,
                                 SC_STATUS_PROCESS_INFO,
                                 reinterpret_cast<LPBYTE>(&status),
                                 sizeof(status),
                                 &bytesNeeded))
            return false;
        if(status.dwCurrentState == expectedState)
            return true;
        Sleep(100);
    }
    return false;
}

static bool SendHideCommand(const COMMAND_LINE& commandLine, HIDE_COMMAND command, ULONG options)
{
    std::string devicePath = "\\\\.\\" + commandLine.DriverName;
    HANDLE device = CreateFileA(devicePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if(device == INVALID_HANDLE_VALUE)
        return ReportWin32Error("Open driver device", GetLastError());

    HIDE_INFO hideInfo = {};
    hideInfo.Command = command;
    hideInfo.Type = options;
    hideInfo.Pid = commandLine.Pid;

    // The legacy driver completes IRP_MJ_WRITE successfully without setting
    // IoStatus.Information, so WriteFile can report zero bytes even after a
    // successful request. Treat the boolean result as authoritative.
    BOOL succeeded = WriteFile(device, &hideInfo, sizeof(hideInfo), NULL, NULL);
    DWORD error = GetLastError();
    CloseHandle(device);
    if(!succeeded)
        return ReportWin32Error("Write to driver device", error);
    return true;
}

static bool GetSelectedOptions(const COMMAND_LINE& commandLine, ULONG* options)
{
    if(commandLine.OptionsSpecified)
        return ParseOptions(commandLine.Options, options);
    if(commandLine.UseSafeOptions)
    {
        *options = 0xFFFFFFFF & ~(ULONG)HideThreadHideFromDebugger;
        return true;
    }
    *options = 0xFFFFFFFF;
    return true;
}

static int CommandStatus(const COMMAND_LINE& commandLine)
{
    DRIVER_STATE state = {};
    if(!QueryDriverState(commandLine.DriverName, &state))
    {
        ReportWin32Error("Query service manager", GetLastError());
        return 1;
    }

    const char* serviceText = state.ServiceInstalled ? GetServiceStateText(state.ServiceState) : "not installed";
    printf("Driver: %s\nService: %s\nDevice: %s\n",
           commandLine.DriverName.c_str(),
           serviceText,
           state.DevicePresent ? "present" : "missing");
    return 0;
}

static int CommandLoad(const COMMAND_LINE& commandLine)
{
    std::string driverPath;
    if(!GetAbsoluteExistingDriverPath(commandLine.DriverPathSpecified ? commandLine.DriverPath : std::string(), &driverPath))
        return 1;

    DRIVER_STATE state = {};
    if(!QueryDriverState(commandLine.DriverName, &state))
    {
        ReportWin32Error("Query service manager", GetLastError());
        return 1;
    }
    if(state.ServiceRunning)
    {
        if(!state.DevicePresent)
        {
            fprintf(stderr, "The service is running, but device \\\\.\\%s is missing.\n", commandLine.DriverName.c_str());
            return 1;
        }
        printf("Driver is already loaded: %s\n", driverPath.c_str());
        return 0;
    }

    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if(scm == NULL)
    {
        ReportWin32Error("Open service manager", GetLastError());
        return 1;
    }

    const DWORD serviceRights = SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP | SERVICE_CHANGE_CONFIG | DELETE;
    SC_HANDLE service = OpenServiceA(scm, commandLine.DriverName.c_str(), serviceRights);
    if(service == NULL)
    {
        DWORD error = GetLastError();
        if(error != ERROR_SERVICE_DOES_NOT_EXIST)
        {
            CloseServiceHandle(scm);
            ReportWin32Error("Open driver service", error);
            return 1;
        }

        service = CreateServiceA(scm,
                                 commandLine.DriverName.c_str(),
                                 commandLine.DriverName.c_str(),
                                 serviceRights,
                                 SERVICE_KERNEL_DRIVER,
                                 SERVICE_DEMAND_START,
                                 SERVICE_ERROR_NORMAL,
                                 driverPath.c_str(),
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL);
        if(service == NULL)
        {
            DWORD createError = GetLastError();
            CloseServiceHandle(scm);
            ReportWin32Error("Create driver service", createError);
            return 1;
        }
    }
    else if(!ChangeServiceConfigA(service,
                                   SERVICE_NO_CHANGE,
                                   SERVICE_NO_CHANGE,
                                   SERVICE_NO_CHANGE,
                                   driverPath.c_str(),
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL,
                                   NULL,
                                   commandLine.DriverName.c_str()))
    {
        DWORD changeError = GetLastError();
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        ReportWin32Error("Update driver service", changeError);
        return 1;
    }

    if(!StartServiceA(service, 0, NULL))
    {
        DWORD startError = GetLastError();
        if(startError != ERROR_SERVICE_ALREADY_RUNNING)
        {
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            ReportWin32Error("Start driver service", startError);
            return 1;
        }
    }
    if(!WaitForServiceState(service, SERVICE_RUNNING))
    {
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        fprintf(stderr, "The driver service did not reach the running state.\n");
        return 1;
    }
    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if(!IsDevicePresent(commandLine.DriverName))
    {
        fprintf(stderr, "Driver service started, but device \\\\.\\%s is missing.\n", commandLine.DriverName.c_str());
        return 1;
    }
    printf("Driver loaded successfully: %s\n", driverPath.c_str());
    return 0;
}

static int CommandUnload(const COMMAND_LINE& commandLine)
{
    DRIVER_STATE state = {};
    if(!QueryDriverState(commandLine.DriverName, &state))
    {
        ReportWin32Error("Query service manager", GetLastError());
        return 1;
    }
    if(!state.ServiceInstalled)
    {
        printf("Driver service '%s' is not installed.\n", commandLine.DriverName.c_str());
        return 0;
    }

    SC_HANDLE scm = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if(scm == NULL)
    {
        ReportWin32Error("Open service manager", GetLastError());
        return 1;
    }
    SC_HANDLE service = OpenServiceA(scm, commandLine.DriverName.c_str(), SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE);
    if(service == NULL)
    {
        DWORD error = GetLastError();
        CloseServiceHandle(scm);
        ReportWin32Error("Open driver service", error);
        return 1;
    }

    if(state.ServiceState != SERVICE_STOPPED)
    {
        SERVICE_STATUS serviceStatus = {};
        if(!ControlService(service, SERVICE_CONTROL_STOP, &serviceStatus))
        {
            DWORD stopError = GetLastError();
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            ReportWin32Error("Stop driver service", stopError);
            return 1;
        }
        if(!WaitForServiceState(service, SERVICE_STOPPED))
        {
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            fprintf(stderr, "The driver service did not reach the stopped state.\n");
            return 1;
        }
    }

    if(!DeleteService(service))
    {
        DWORD deleteError = GetLastError();
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        ReportWin32Error("Delete driver service", deleteError);
        return 1;
    }
    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    printf("Driver unloaded and service entry removed.\n");
    return 0;
}

static int CommandHide(const COMMAND_LINE& commandLine, HIDE_COMMAND command)
{
    ULONG options = 0;
    if(!GetSelectedOptions(commandLine, &options))
        return 1;
    if(!IsDevicePresent(commandLine.DriverName))
    {
        fprintf(stderr, "Driver device \\\\.\\%s is missing. Run 'TitanHideCLI load' first.\n", commandLine.DriverName.c_str());
        return 1;
    }
    if(!SendHideCommand(commandLine, command, options))
        return 1;
    printf("%s PID %lu with options 0x%08lX.\n",
           command == HidePid ? "Hid" : "Unhid",
           commandLine.Pid,
           options);
    return 0;
}

static int CommandUnhideAll(const COMMAND_LINE& commandLine)
{
    if(!IsDevicePresent(commandLine.DriverName))
    {
        fprintf(stderr, "Driver device \\\\.\\%s is missing. Run 'TitanHideCLI load' first.\n", commandLine.DriverName.c_str());
        return 1;
    }
    if(!SendHideCommand(commandLine, UnhideAll, 0))
        return 1;
    printf("Removed every TitanHide entry.\n");
    return 0;
}

int main(int argc, char** argv)
{
    COMMAND_LINE commandLine = {};
    if(!ParseCommandLine(argc, argv, &commandLine))
        return 0;

    if(commandLine.Command == "status")
        return CommandStatus(commandLine);
    if(commandLine.Command == "load")
        return CommandLoad(commandLine);
    if(commandLine.Command == "unload")
        return CommandUnload(commandLine);
    if(commandLine.Command == "hide")
        return CommandHide(commandLine, HidePid);
    if(commandLine.Command == "unhide")
        return CommandHide(commandLine, UnhidePid);
    if(commandLine.Command == "unhide-all")
        return CommandUnhideAll(commandLine);
    return 1;
}



