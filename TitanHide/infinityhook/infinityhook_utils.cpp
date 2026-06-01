#include "infinityhook_utils.h"
#include "../undocumented.h"

ULONG InfinityHookUtils::GetSystemBuildNumber()
{
    RTL_OSVERSIONINFOEXW info = {};
    info.dwOSVersionInfoSize = sizeof(info);
    if(NT_SUCCESS(RtlGetVersion((PRTL_OSVERSIONINFOW)&info)))
        return info.dwBuildNumber;
    return 0;
}

ULONG_PTR InfinityHookUtils::GetModuleAddress(const char* Name, PULONG Size)
{
    ULONG length = 0;
    Undocumented::ZwQuerySystemInformation(SystemModuleInformation, &length, 0, &length);
    if(!length)
        return 0;

    PINFINITY_SYSTEM_MODULE_INFORMATION modules = (PINFINITY_SYSTEM_MODULE_INFORMATION)RtlAllocateMemory(true, length);
    if(!modules)
        return 0;

    ULONG_PTR result = 0;
    if(NT_SUCCESS(Undocumented::ZwQuerySystemInformation(SystemModuleInformation, modules, length, NULL)))
    {
        for(ULONG_PTR i = 0; i < modules->ModuleCount; i++)
        {
            PINFINITY_SYSTEM_MODULE_INFORMATION_ENTRY entry = &modules->Modules[i];
            if(strstr(entry->ImageName, Name))
            {
                result = (ULONG_PTR)entry->Base;
                if(Size)
                    *Size = entry->Size;
                break;
            }
        }
    }

    RtlFreeMemory(modules);
    return result;
}

bool InfinityHookUtils::PatternCheck(const char* Data, const char* Pattern, const char* Mask)
{
    for(SIZE_T i = 0, len = strlen(Mask); i < len; i++)
    {
        if(Mask[i] != '?' && Data[i] != Pattern[i])
            return false;
    }
    return true;
}

ULONG_PTR InfinityHookUtils::FindPattern(ULONG_PTR Address, ULONG Size, const char* Pattern, const char* Mask)
{
    ULONG patternLength = (ULONG)strlen(Mask);
    if(Size < patternLength)
        return 0;

    Size -= patternLength;
    for(ULONG i = 0; i < Size; i++)
    {
        if(PatternCheck((const char*)Address + i, Pattern, Mask))
            return Address + i;
    }
    return 0;
}

ULONG_PTR InfinityHookUtils::FindPatternImage(ULONG_PTR Address, const char* Pattern, const char* Mask, const char* Section)
{
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)Address;
    if(dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)(Address + dos->e_lfanew);
    if(nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;

    PIMAGE_SECTION_HEADER sections = IMAGE_FIRST_SECTION(nt);
    for(USHORT i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        PIMAGE_SECTION_HEADER current = &sections[i];
        if(strstr((const char*)current->Name, Section))
        {
            ULONG_PTR hit = FindPattern(Address + current->VirtualAddress, current->Misc.VirtualSize, Pattern, Mask);
            if(hit)
                return hit;
        }
    }
    return 0;
}

ULONG_PTR InfinityHookUtils::GetImageSectionAddress(ULONG_PTR Base, const char* SectionName, PULONG Size)
{
    PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)Base;
    if(dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    PIMAGE_NT_HEADERS64 nt = (PIMAGE_NT_HEADERS64)(Base + dos->e_lfanew);
    if(nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;

    PIMAGE_SECTION_HEADER sections = IMAGE_FIRST_SECTION(nt);
    for(USHORT i = 0; i < nt->FileHeader.NumberOfSections; i++)
    {
        PIMAGE_SECTION_HEADER current = &sections[i];
        if(strstr((const char*)current->Name, SectionName))
        {
            if(Size)
                *Size = current->SizeOfRawData;
            return Base + current->VirtualAddress;
        }
    }
    return 0;
}

ULONG_PTR InfinityHookUtils::ResolveRipRelative(ULONG_PTR Instruction)
{
    return Instruction + 7 + *(LONG*)(Instruction + 3);
}

PVOID InfinityHookUtils::GetSyscallEntry(ULONG_PTR KernelBase)
{
#ifndef _WIN64
    UNREFERENCED_PARAMETER(KernelBase);
    return NULL;
#else
    if(!KernelBase)
        return NULL;

    PVOID syscallEntry = (PVOID)__readmsr(0xC0000082);

    ULONG sectionSize = 0;
    ULONG_PTR kvascode = GetImageSectionAddress(KernelBase, "KVASCODE", &sectionSize);
    if(!kvascode)
        return syscallEntry;
    if(!(syscallEntry >= (PVOID)kvascode && syscallEntry < (PVOID)(kvascode + sectionSize)))
        return syscallEntry;

    hde64s hde = {};
    for(char* p = (char*)syscallEntry; ; p += hde.len)
    {
        if(!hde64_disasm(p, &hde))
            break;
        if(hde.opcode != 0xE9)
            continue;

        PVOID target = (PVOID)((LONG_PTR)p + (LONG)hde.len + (LONG)hde.imm.imm32);
        if(target >= (PVOID)kvascode && target < (PVOID)(kvascode + sectionSize))
            continue;

        return target;
    }

    return syscallEntry;
#endif
}

void InfinityHookUtils::Sleep(LONG Milliseconds)
{
    LARGE_INTEGER delay = {};
    delay.QuadPart = -10000LL * Milliseconds;
    KeDelayExecutionThread(KernelMode, FALSE, &delay);
}
