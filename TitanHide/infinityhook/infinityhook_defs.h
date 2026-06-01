#ifndef _INFINITYHOOK_DEFS_H
#define _INFINITYHOOK_DEFS_H

#include "../_global.h"
#include <intrin.h>
#include <evntrace.h>
#include <wmistr.h>

typedef void (__fastcall *INFINITY_HOOK_CALLBACK)(ULONG CallIndex, PVOID* CallAddress);
typedef __int64 (*HVL_GET_QPC_BIAS)();
typedef LONG_PTR (FASTCALL *OBF_DEREFERENCE_OBJECT)(PVOID Object);
typedef LONG_PTR (NTAPI *OB_DEREFERENCE_OBJECT)(PVOID Object);

typedef enum _INFINITY_ETWP_TRACE_TYPE
{
    InfinityEtwpStartTrace = 1,
    InfinityEtwpStopTrace = 2,
    InfinityEtwpQueryTrace = 3,
    InfinityEtwpUpdateTrace = 4,
    InfinityEtwpFlushTrace = 5
} INFINITY_ETWP_TRACE_TYPE;

typedef struct _INFINITY_EVENT_TRACE_PROPERTIES
{
    WNODE_HEADER Wnode;
    ULONG BufferSize;
    ULONG MinimumBuffers;
    ULONG MaximumBuffers;
    ULONG MaximumFileSize;
    ULONG LogFileMode;
    ULONG FlushTimer;
    ULONG EnableFlags;
    union
    {
        LONG AgeLimit;
        LONG FlushThreshold;
    } DUMMYUNIONNAME;
    ULONG NumberOfBuffers;
    ULONG FreeBuffers;
    ULONG EventsLost;
    ULONG BuffersWritten;
    ULONG LogBuffersLost;
    ULONG RealTimeBuffersLost;
    HANDLE LoggerThreadId;
    ULONG LogFileNameOffset;
    ULONG LoggerNameOffset;
} INFINITY_EVENT_TRACE_PROPERTIES, *PINFINITY_EVENT_TRACE_PROPERTIES;

typedef struct _INFINITY_CKCL_TRACE_PROPERTIES : INFINITY_EVENT_TRACE_PROPERTIES
{
    ULONG64 Unknown[3];
    UNICODE_STRING ProviderName;
} INFINITY_CKCL_TRACE_PROPERTIES, *PINFINITY_CKCL_TRACE_PROPERTIES;

typedef struct _INFINITY_SYSTEM_MODULE_INFORMATION_ENTRY
{
    HANDLE Section;
    PVOID MappedBase;
    PVOID Base;
    ULONG Size;
    ULONG Flags;
    USHORT Index;
    USHORT Unknown;
    USHORT LoadCount;
    USHORT ModuleNameOffset;
    CHAR ImageName[256];
} INFINITY_SYSTEM_MODULE_INFORMATION_ENTRY, *PINFINITY_SYSTEM_MODULE_INFORMATION_ENTRY;

typedef struct _INFINITY_SYSTEM_MODULE_INFORMATION
{
    ULONG_PTR ModuleCount;
    INFINITY_SYSTEM_MODULE_INFORMATION_ENTRY Modules[1];
} INFINITY_SYSTEM_MODULE_INFORMATION, *PINFINITY_SYSTEM_MODULE_INFORMATION;

extern "C"
NTSTATUS NTAPI NtTraceControl(
    ULONG FunctionCode,
    PVOID InBuffer,
    ULONG InBufferLen,
    PVOID OutBuffer,
    ULONG OutBufferLen,
    PULONG ReturnLength);

#endif
