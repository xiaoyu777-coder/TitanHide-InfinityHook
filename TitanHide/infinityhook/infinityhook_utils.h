#ifndef _INFINITYHOOK_UTILS_H
#define _INFINITYHOOK_UTILS_H

#include "infinityhook_defs.h"
#include "hde/hde64.h"

class InfinityHookUtils
{
public:
    static ULONG GetSystemBuildNumber();
    static ULONG_PTR GetModuleAddress(const char* Name, PULONG Size = NULL);
    static ULONG_PTR FindPattern(ULONG_PTR Address, ULONG Size, const char* Pattern, const char* Mask);
    static ULONG_PTR FindPatternImage(ULONG_PTR Address, const char* Pattern, const char* Mask, const char* Section = ".text");
    static ULONG_PTR GetImageSectionAddress(ULONG_PTR Base, const char* SectionName, PULONG Size = NULL);
    static ULONG_PTR ResolveRipRelative(ULONG_PTR Instruction);
    static PVOID GetSyscallEntry(ULONG_PTR KernelBase);
    static void Sleep(LONG Milliseconds);

private:
    static bool PatternCheck(const char* Data, const char* Pattern, const char* Mask);
};

#endif
