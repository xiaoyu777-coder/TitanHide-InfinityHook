#include "syscall_resolver.h"
#include "undocumented.h"
#include "ntdll.h"
#include "log.h"

struct SYSCALL_SERVICE_TABLE
{
    LONG* ServiceTable;
    PVOID CounterTable;
#ifdef _WIN64
    ULONGLONG NumberOfServices;
#else
    ULONG NumberOfServices;
#endif
    PCHAR ArgumentTable;
};

static SYSCALL_SERVICE_TABLE* FindServiceTable()
{
    static SYSCALL_SERVICE_TABLE* serviceTable = NULL;
    if(serviceTable)
        return serviceTable;

#ifndef _WIN64
    UNICODE_STRING routineName;
    RtlInitUnicodeString(&routineName, L"KeServiceDescriptorTable");
    serviceTable = (SYSCALL_SERVICE_TABLE*)MmGetSystemRoutineAddress(&routineName);
#else
    ULONG kernelSize = 0;
    ULONG_PTR kernelBase = (ULONG_PTR)Undocumented::GetKernelBase(&kernelSize);
    if(kernelBase == 0 || kernelSize == 0)
        return NULL;

    PIMAGE_NT_HEADERS ntHeaders = RtlImageNtHeader((PVOID)kernelBase);
    if(!ntHeaders)
        return NULL;

    PIMAGE_SECTION_HEADER textSection = NULL;
    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(ntHeaders);
    for(ULONG i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++)
    {
        CHAR sectionName[IMAGE_SIZEOF_SHORT_NAME + 1] = {};
        RtlCopyMemory(sectionName, section->Name, IMAGE_SIZEOF_SHORT_NAME);
        if(strncmp(sectionName, ".text", sizeof(".text") - sizeof(CHAR)) == 0)
        {
            textSection = section;
            break;
        }
        section++;
    }
    if(!textSection)
        return NULL;

    const UCHAR kiSystemServiceStartPattern[] = { 0x8B, 0xF8, 0xC1, 0xEF, 0x07, 0x83, 0xE7, 0x20, 0x25, 0xFF, 0x0F, 0x00, 0x00 };
    const ULONG signatureSize = sizeof(kiSystemServiceStartPattern);
    bool found = false;
    ULONG kiSystemServiceStartOffset = 0;
    for(; kiSystemServiceStartOffset < textSection->Misc.VirtualSize - signatureSize; kiSystemServiceStartOffset++)
    {
        if(RtlCompareMemory((PUCHAR)kernelBase + textSection->VirtualAddress + kiSystemServiceStartOffset, kiSystemServiceStartPattern, signatureSize) == signatureSize)
        {
            found = true;
            break;
        }
    }
    if(!found)
        return NULL;

    ULONG_PTR address = kernelBase + textSection->VirtualAddress + kiSystemServiceStartOffset + signatureSize;
    LONG relativeOffset = 0;
    if((*(PUCHAR)address == 0x4c) && (*(PUCHAR)(address + 1) == 0x8d) && (*(PUCHAR)(address + 2) == 0x15))
        relativeOffset = *(LONG*)(address + 3);
    if(relativeOffset == 0)
        return NULL;

    serviceTable = (SYSCALL_SERVICE_TABLE*)(address + relativeOffset + 7);
#endif

    return serviceTable;
}

PVOID SyscallResolver::GetFunctionAddress(const char* ApiName)
{
    SYSCALL_SERVICE_TABLE* serviceTable = FindServiceTable();
    if(!serviceTable)
    {
        Log("[TITANHIDE] Syscall service table not found...\r\n");
        return NULL;
    }

    ULONG_PTR serviceTableBase = (ULONG_PTR)serviceTable->ServiceTable;
    if(!serviceTableBase)
    {
        Log("[TITANHIDE] Syscall service table base not found...\r\n");
        return NULL;
    }

    int syscallIndex = NTDLL::GetExportSsdtIndex(ApiName);
    if(syscallIndex == -1)
        return NULL;
    if((ULONGLONG)syscallIndex >= serviceTable->NumberOfServices)
    {
        Log("[TITANHIDE] Invalid syscall index for %s...\r\n", ApiName);
        return NULL;
    }

#ifdef _WIN64
    return (PVOID)((serviceTable->ServiceTable[syscallIndex] >> 4) + serviceTableBase);
#else
    return (PVOID)serviceTable->ServiceTable[syscallIndex];
#endif
}
