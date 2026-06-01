#include "infinityhook_core.h"
#include "infinityhook_utils.h"

#pragma warning(disable : 4201 4311 4302)

namespace
{
    constexpr ULONG kPoolTag = 'hfnI';
    constexpr ULONG kPerfTag = 'pfnI';

    constexpr ULONG kMagicPre23606 = 0x501802u;
    constexpr ULONG kMagicPost23606 = 0x601802u;
    constexpr USHORT kMagicTag = 0xF33;

    constexpr ULONG kCounterTypeOffset = 0xE4;
    constexpr ULONG kCounterBaseRateOffset = 0xC0;
    constexpr ULONG kCounterTypePhysical = 0x5;
    constexpr LONGLONG kCounterBaseRate = 10000000LL;
    constexpr ULONG_PTR kQpcBiasVa = 0xFFFFF780000003B8ull;

    const GUID kCkclGuid = { 0x54dea73a, 0xed1f, 0x42a4, { 0xaf, 0x71, 0x3e, 0x63, 0xd0, 0x56, 0xf1, 0x74 } };

    enum INFINITY_STATE
    {
        InfinityStateUninitialized,
        InfinityStateInitialized,
        InfinityStateStarted,
        InfinityStateStopping
    };

    INFINITY_STATE gState = InfinityStateUninitialized;
    INFINITY_HOOK_CALLBACK gCallback = NULL;
    ULONG gBuildNumber = 0;
    PVOID gSystemCallTable = NULL;
    volatile bool gDetectThreadStatus = false;

    PVOID gEtwpDebuggerData = NULL;
    PVOID gCkclWmiLoggerContext = NULL;
    PVOID* gEtwpDebuggerDataSilo = NULL;
    PVOID* gGetCpuClock = NULL;
    ULONG_PTR gOriginalGetCpuClock = 0;
    bool gGetCpuClockModified = false;

    PETHREAD gDetectThreadObject = NULL;
    HANDLE gDetectThreadHandle = NULL;
    CLIENT_ID gClientId = {};

    ULONG_PTR gHvlpReferenceTscPage = 0;
    ULONG_PTR gHvlGetQpcBias = 0;
    ULONG_PTR gHvlpGetReferenceTimeUsingTscPage = 0;
    ULONG_PTR gHalpPerformanceCounter = 0;
    ULONG_PTR gHalpOriginalPerformanceCounter = 0;
    ULONG_PTR gHalpOriginalPerformanceCounterCopy = 0;
    ULONG_PTR gOriginalHalpOriginalPerformanceCounter = 0;
    PULONG gHalpPerformanceCounterType = NULL;
    UCHAR gVmHalpPerformanceCounterType = 0;
    ULONG gOriginalHalpPerformanceCounterType = 0;
    ULONG_PTR gOriginalHvlpGetReferenceTimeUsingTscPage = 0;
    HVL_GET_QPC_BIAS gOriginalHvlGetQpcBias = NULL;
    bool gHvlGetQpcBiasModified = false;
    bool gReferenceTimeModified = false;
    bool gHalpCounterModified = false;

    PLONGLONG gQpcPointer = NULL;
    PMDL gQpcMdl = NULL;

    NTSTATUS EventTraceControl(INFINITY_ETWP_TRACE_TYPE Type)
    {
        PINFINITY_CKCL_TRACE_PROPERTIES property = (PINFINITY_CKCL_TRACE_PROPERTIES)ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, kPoolTag);
        if(!property)
            return STATUS_MEMORY_NOT_ALLOCATED;

        PWCHAR providerName = (PWCHAR)ExAllocatePoolWithTag(NonPagedPool, 256 * sizeof(WCHAR), kPoolTag);
        if(!providerName)
        {
            ExFreePoolWithTag(property, kPoolTag);
            return STATUS_MEMORY_NOT_ALLOCATED;
        }

        RtlZeroMemory(property, PAGE_SIZE);
        RtlZeroMemory(providerName, 256 * sizeof(WCHAR));
        RtlCopyMemory(providerName, L"Circular Kernel Context Logger", sizeof(L"Circular Kernel Context Logger"));
        RtlInitUnicodeString(&property->ProviderName, providerName);

        property->Wnode.BufferSize = PAGE_SIZE;
        property->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
        property->Wnode.Guid = kCkclGuid;
        property->Wnode.ClientContext = 3;
        property->BufferSize = sizeof(ULONG);
        property->MinimumBuffers = 2;
        property->MaximumBuffers = 2;
        property->LogFileMode = EVENT_TRACE_BUFFERING_MODE;

        if(Type == InfinityEtwpUpdateTrace)
            property->EnableFlags = EVENT_TRACE_FLAG_SYSTEMCALL;

        ULONG length = 0;
        NTSTATUS status = NtTraceControl(Type, property, PAGE_SIZE, property, PAGE_SIZE, &length);

        ExFreePoolWithTag(providerName, kPoolTag);
        ExFreePoolWithTag(property, kPoolTag);
        return status;
    }

    ULONG_PTR SelfGetCpuClock()
    {
        if(ExGetPreviousMode() == KernelMode)
            return __rdtsc();

        PKTHREAD thread = (PKTHREAD)__readgsqword(0x188);
        ULONG callIndex = 0;
        if(gBuildNumber <= 7601)
            callIndex = *(PULONG)((ULONG_PTR)thread + 0x1f8);
        else
            callIndex = *(PULONG)((ULONG_PTR)thread + 0x80);

        PVOID* stackTop = (PVOID*)__readgsqword(0x1a8);
        PVOID* stackFrame = (PVOID*)_AddressOfReturnAddress();

        for(PVOID* current = stackTop; current > stackFrame; --current)
        {
            PULONG marker = (PULONG)current;
            if(*marker != kMagicPre23606 && *marker != kMagicPost23606)
                continue;

            --current;

            PUSHORT tag = (PUSHORT)current;
            if(*tag != kMagicTag)
                continue;

            for(; current < stackTop; ++current)
            {
                PULONG_PTR value = (PULONG_PTR)current;
                if(!(PAGE_ALIGN(*value) >= gSystemCallTable && PAGE_ALIGN(*value) < (PVOID)((ULONG_PTR)gSystemCallTable + PAGE_SIZE * 2)))
                    continue;

                PVOID* syscallFunction = &current[9];
                if(gCallback)
                    gCallback(callIndex, syscallFunction);
                break;
            }

            break;
        }

        return __rdtsc();
    }

    extern "C" __int64 FakeHvlGetQpcBias()
    {
        SelfGetCpuClock();

        if(gHvlpReferenceTscPage && *(PULONG_PTR)gHvlpReferenceTscPage != 0)
            return *((PLONGLONG)(*(PULONG_PTR)gHvlpReferenceTscPage) + 3);

        return 0;
    }

    ULONG64 FakeGetReferenceTimeUsingTscPage()
    {
        return __rdtsc();
    }

    void ClearResolvedState()
    {
        gEtwpDebuggerData = NULL;
        gCkclWmiLoggerContext = NULL;
        gEtwpDebuggerDataSilo = NULL;
        gGetCpuClock = NULL;
        gSystemCallTable = NULL;
        gHvlpReferenceTscPage = 0;
        gHvlGetQpcBias = 0;
        gHvlpGetReferenceTimeUsingTscPage = 0;
        gHalpPerformanceCounter = 0;
        gHalpOriginalPerformanceCounter = 0;
        gHalpPerformanceCounterType = NULL;
        gVmHalpPerformanceCounterType = 0;
    }

    void ReleaseAllocatedState()
    {
        if(gQpcPointer && gQpcMdl)
        {
            MmUnmapLockedPages(gQpcPointer, gQpcMdl);
            gQpcPointer = NULL;
        }
        if(gQpcMdl)
        {
            IoFreeMdl(gQpcMdl);
            gQpcMdl = NULL;
        }
        if(gHalpOriginalPerformanceCounterCopy)
        {
            ExFreePoolWithTag((PVOID)gHalpOriginalPerformanceCounterCopy, kPerfTag);
            gHalpOriginalPerformanceCounterCopy = 0;
        }
    }

    void DereferenceDetectThread()
    {
        if(gDetectThreadObject)
        {
            UNICODE_STRING name = RTL_CONSTANT_STRING(L"ObfDereferenceObject");
            OBF_DEREFERENCE_OBJECT fn = (OBF_DEREFERENCE_OBJECT)MmGetSystemRoutineAddress(&name);
            if(fn)
            {
                fn(gDetectThreadObject);
            }
            else
            {
                name = RTL_CONSTANT_STRING(L"ObDereferenceObject");
                OB_DEREFERENCE_OBJECT fallback = (OB_DEREFERENCE_OBJECT)MmGetSystemRoutineAddress(&name);
                if(fallback)
                    fallback(gDetectThreadObject);
            }
            gDetectThreadObject = NULL;
        }
    }

    void DetectThreadRoutine(PVOID)
    {
        while(gDetectThreadStatus)
        {
            InfinityHookUtils::Sleep(1000);

            if(gBuildNumber <= 18363 && gCallback)
            {
                if(gGetCpuClock && MmIsAddressValid(gGetCpuClock) && MmIsAddressValid(*gGetCpuClock))
                {
                    if((PVOID)SelfGetCpuClock != *gGetCpuClock)
                    {
                        if(NT_SUCCESS(InfinityHookCore::Initialize(gCallback)))
                            InfinityHookCore::Start();
                    }
                }
                else
                {
                    InfinityHookCore::Initialize(gCallback);
                }
            }

            KeQueryPerformanceCounter(NULL);
        }

        PsTerminateSystemThread(STATUS_SUCCESS);
    }

    NTSTATUS ResolveModernBuildTargets(ULONG_PTR kernelBase)
    {
        ULONG_PTR address = InfinityHookUtils::FindPatternImage(kernelBase,
            "\x48\x8b\x05\x00\x00\x00\x00\x48\x8b\x40\x00\x48\x8b\x0d\x00\x00\x00\x00\x48\xf7\xe2",
            "xxx????xxx?xxx????xxx");
        if(!address)
            return STATUS_NOT_FOUND;
        gHvlpReferenceTscPage = InfinityHookUtils::ResolveRipRelative(address);
        if(!gHvlpReferenceTscPage)
            return STATUS_NOT_FOUND;

        address = InfinityHookUtils::FindPatternImage(kernelBase,
            "\x48\x8b\x05\x00\x00\x00\x00\x48\x85\xc0\x74\x00\x48\x83\x3d\x00\x00\x00\x00\x00\x74",
            "xxx????xxxx?xxx?????x");
        if(!address)
        {
            address = InfinityHookUtils::FindPatternImage(kernelBase,
                "\x48\x8b\x05\x00\x00\x00\x00\xe8\x00\x00\x00\x00\x48\x03\xd8\x48\x89\x1f",
                "xxx????x????xxxxxx");
        }
        if(!address)
            return STATUS_NOT_FOUND;
        gHvlGetQpcBias = InfinityHookUtils::ResolveRipRelative(address);
        if(!gHvlGetQpcBias)
            return STATUS_NOT_FOUND;

        address = InfinityHookUtils::FindPatternImage(kernelBase,
            "\x48\x8b\x05\x00\x00\x00\x00\x48\x85\xc0\x74\x00\x33\xc9\xe8\x00\x00\x00\x00\x48\x8b\xd8",
            "xxx????xxxx?xxx????xxx");
        if(!address)
        {
            address = InfinityHookUtils::FindPatternImage(kernelBase,
                "\x48\x8b\x05\x00\x00\x00\x00\xE8\x00\x00\x00\x00\x48\x03\xd8",
                "xxx????x????xxx");
        }
        if(!address)
            return STATUS_NOT_FOUND;
        gHvlpGetReferenceTimeUsingTscPage = InfinityHookUtils::ResolveRipRelative(address);
        if(!gHvlpGetReferenceTimeUsingTscPage)
            return STATUS_NOT_FOUND;

        ULONG_PTR halpCounterScan = InfinityHookUtils::FindPatternImage(kernelBase,
            "\x48\x8b\x05\x00\x00\x00\x00\x48\x8b\xf9\x48\x85\xc0\x74\x00\x83\xb8",
            "xxx????xxxxxxx?xx");
        if(!halpCounterScan)
            return STATUS_NOT_FOUND;
        gHalpPerformanceCounter = InfinityHookUtils::ResolveRipRelative(halpCounterScan);
        if(!gHalpPerformanceCounter)
            return STATUS_NOT_FOUND;

        address = InfinityHookUtils::FindPatternImage(kernelBase,
            "\x48\x8b\x05\x00\x00\x00\x00\x48\x3b\x00\x0f\x85\x00\x00\x00\x00\xA0",
            "xxx????xx?xx????x");
        if(!address)
        {
            address = InfinityHookUtils::FindPatternImage(kernelBase,
                "\x48\x8b\x0d\x00\x00\x00\x00\x4c\x00\x00\x00\x00\x48\x3b\xf1",
                "xxx????x????xxx");
        }
        if(!address)
            return STATUS_NOT_FOUND;
        gHalpOriginalPerformanceCounter = InfinityHookUtils::ResolveRipRelative(address);
        if(!gHalpOriginalPerformanceCounter)
            return STATUS_NOT_FOUND;

        gHalpPerformanceCounterType = (PULONG)((ULONG_PTR)(*(PVOID*)gHalpPerformanceCounter) + kCounterTypeOffset);
        if(!gHalpPerformanceCounterType)
            return STATUS_NOT_FOUND;

        if(*gHalpPerformanceCounterType == kCounterTypePhysical)
        {
            gVmHalpPerformanceCounterType = *((PUCHAR)halpCounterScan + 21);

            gHalpOriginalPerformanceCounterCopy = (ULONG_PTR)ExAllocatePoolWithTag(NonPagedPool, 0xFF, kPerfTag);
            if(!gHalpOriginalPerformanceCounterCopy)
                return STATUS_MEMORY_NOT_ALLOCATED;
            RtlZeroMemory((PVOID)gHalpOriginalPerformanceCounterCopy, 0xFF);
            *(PULONGLONG)(gHalpOriginalPerformanceCounterCopy + kCounterBaseRateOffset) = kCounterBaseRate;
            *(PULONG)(gHalpOriginalPerformanceCounterCopy + kCounterTypeOffset) = kCounterTypePhysical;

            gQpcMdl = IoAllocateMdl((PLONGLONG)kQpcBiasVa, sizeof(LONGLONG), FALSE, FALSE, NULL);
            if(!gQpcMdl)
                return STATUS_INSUFFICIENT_RESOURCES;

            MmBuildMdlForNonPagedPool(gQpcMdl);
            gQpcPointer = (PLONGLONG)MmMapLockedPagesSpecifyCache(gQpcMdl, KernelMode, MmWriteCombined, NULL, FALSE, NormalPagePriority);
            if(!gQpcPointer)
                return STATUS_NONE_MAPPED;
        }

        return STATUS_SUCCESS;
    }
}

NTSTATUS InfinityHookCore::Initialize(INFINITY_HOOK_CALLBACK Callback)
{
#ifndef _WIN64
    UNREFERENCED_PARAMETER(Callback);
    return STATUS_NOT_SUPPORTED;
#else
    if(!Callback || !MmIsAddressValid(Callback))
        return STATUS_INVALID_PARAMETER;

    if(gState == InfinityStateStarted || gState == InfinityStateInitialized)
    {
        gCallback = Callback;
        return STATUS_SUCCESS;
    }

    gCallback = Callback;
    gDetectThreadStatus = true;

    NTSTATUS status = EventTraceControl(InfinityEtwpUpdateTrace);
    if(!NT_SUCCESS(status))
    {
        status = EventTraceControl(InfinityEtwpStartTrace);
        if(!NT_SUCCESS(status))
            goto Fail;
        status = EventTraceControl(InfinityEtwpUpdateTrace);
        if(!NT_SUCCESS(status))
            goto Fail;
    }

    gBuildNumber = InfinityHookUtils::GetSystemBuildNumber();
    if(!gBuildNumber)
    {
        status = STATUS_NOT_SUPPORTED;
        goto Fail;
    }

    ULONG_PTR kernelBase = InfinityHookUtils::GetModuleAddress("ntoskrnl.exe", NULL);
    if(!kernelBase)
    {
        status = STATUS_NOT_FOUND;
        goto Fail;
    }

    ULONG_PTR etwData = InfinityHookUtils::FindPatternImage(kernelBase, "\x00\x00\x2c\x08\x04\x38\x0c", "??xxxxx", ".text");
    if(!etwData)
        etwData = InfinityHookUtils::FindPatternImage(kernelBase, "\x00\x00\x2c\x08\x04\x38\x0c", "??xxxxx", ".data");
    if(!etwData)
        etwData = InfinityHookUtils::FindPatternImage(kernelBase, "\x00\x00\x2c\x08\x04\x38\x0c", "??xxxxx", ".rdata");
    if(!etwData)
    {
        status = STATUS_NOT_FOUND;
        goto Fail;
    }
    gEtwpDebuggerData = (PVOID)etwData;

    gEtwpDebuggerDataSilo = *(PVOID**)((ULONG_PTR)gEtwpDebuggerData + 0x10);
    if(!gEtwpDebuggerDataSilo)
    {
        status = STATUS_NOT_FOUND;
        goto Fail;
    }

    gCkclWmiLoggerContext = gEtwpDebuggerDataSilo[0x2];
    if(!gCkclWmiLoggerContext)
    {
        status = STATUS_NOT_FOUND;
        goto Fail;
    }

    if(gBuildNumber <= 7601 || gBuildNumber >= 22000)
        gGetCpuClock = (PVOID*)((ULONG_PTR)gCkclWmiLoggerContext + 0x18);
    else
        gGetCpuClock = (PVOID*)((ULONG_PTR)gCkclWmiLoggerContext + 0x28);

    if(!MmIsAddressValid(gGetCpuClock))
    {
        status = STATUS_ACCESS_VIOLATION;
        goto Fail;
    }

    gSystemCallTable = PAGE_ALIGN(InfinityHookUtils::GetSyscallEntry(kernelBase));
    if(!gSystemCallTable)
    {
        status = STATUS_NOT_FOUND;
        goto Fail;
    }

    if(gBuildNumber > 18363)
    {
        status = ResolveModernBuildTargets(kernelBase);
        if(!NT_SUCCESS(status))
            goto Fail;
    }

    gState = InfinityStateInitialized;
    return STATUS_SUCCESS;

Fail:
    ReleaseAllocatedState();
    ClearResolvedState();
    gDetectThreadStatus = false;
    gCallback = NULL;
    gState = InfinityStateUninitialized;
    return status;
#endif
}

NTSTATUS InfinityHookCore::Start()
{
#ifndef _WIN64
    return STATUS_NOT_SUPPORTED;
#else
    if(gState == InfinityStateStarted)
        return STATUS_SUCCESS;
    if(gState != InfinityStateInitialized || !gCallback)
        return STATUS_INVALID_DEVICE_STATE;
    if(!gGetCpuClock || !MmIsAddressValid(gGetCpuClock))
        return STATUS_ACCESS_VIOLATION;

    gOriginalGetCpuClock = (ULONG_PTR)(*gGetCpuClock);

    if(gBuildNumber <= 18363)
    {
        *gGetCpuClock = (PVOID)SelfGetCpuClock;
        gGetCpuClockModified = true;
    }
    else
    {
        *gGetCpuClock = (PVOID)2;
        gGetCpuClockModified = true;

        gOriginalHvlGetQpcBias = (HVL_GET_QPC_BIAS)(*(PULONG_PTR)gHvlGetQpcBias);

        if(gHvlpGetReferenceTimeUsingTscPage)
        {
            gOriginalHvlpGetReferenceTimeUsingTscPage = *(PULONG_PTR)gHvlpGetReferenceTimeUsingTscPage;
            if(gOriginalHvlpGetReferenceTimeUsingTscPage == 0)
            {
                *(PULONG_PTR)gHvlpGetReferenceTimeUsingTscPage = (ULONG_PTR)FakeGetReferenceTimeUsingTscPage;
                gReferenceTimeModified = true;
            }
        }

        gOriginalHalpPerformanceCounterType = *gHalpPerformanceCounterType;
        if(*gHalpPerformanceCounterType == kCounterTypePhysical)
        {
            gOriginalHalpOriginalPerformanceCounter = *(PULONG_PTR)gHalpOriginalPerformanceCounter;
            *(PULONG_PTR)gHalpOriginalPerformanceCounter = gHalpOriginalPerformanceCounterCopy;
            KeQueryPerformanceCounter(NULL);
            *gHalpPerformanceCounterType = gVmHalpPerformanceCounterType;
            KeQueryPerformanceCounter(NULL);
            gHalpCounterModified = true;
        }

        *(PULONG_PTR)gHvlGetQpcBias = (ULONG_PTR)FakeHvlGetQpcBias;
        gHvlGetQpcBiasModified = true;
    }

    if(!gDetectThreadObject)
    {
        OBJECT_ATTRIBUTES oa = {};
        InitializeObjectAttributes(&oa, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);

        NTSTATUS status = PsCreateSystemThread(&gDetectThreadHandle, THREAD_ALL_ACCESS, &oa, NULL, &gClientId, DetectThreadRoutine, NULL);
        if(!NT_SUCCESS(status))
        {
            Stop();
            return status;
        }

        status = ObReferenceObjectByHandle(gDetectThreadHandle, THREAD_ALL_ACCESS, NULL, KernelMode, (PVOID*)&gDetectThreadObject, NULL);
        if(!NT_SUCCESS(status))
        {
            gDetectThreadStatus = false;
            ZwWaitForSingleObject(gDetectThreadHandle, FALSE, NULL);
            ZwClose(gDetectThreadHandle);
            gDetectThreadHandle = NULL;
            Stop();
            return status;
        }

        ZwClose(gDetectThreadHandle);
        gDetectThreadHandle = NULL;
    }

    gState = InfinityStateStarted;
    return STATUS_SUCCESS;
#endif
}

NTSTATUS InfinityHookCore::Stop()
{
#ifndef _WIN64
    return STATUS_NOT_SUPPORTED;
#else
    if(gState == InfinityStateUninitialized)
        return STATUS_SUCCESS;

    gState = InfinityStateStopping;
    gDetectThreadStatus = false;

    NTSTATUS traceStatus = EventTraceControl(InfinityEtwpStopTrace);
    NTSTATUS restartStatus = EventTraceControl(InfinityEtwpStartTrace);

    if(gDetectThreadObject)
        KeWaitForSingleObject(gDetectThreadObject, Executive, KernelMode, FALSE, NULL);

    DereferenceDetectThread();

    if(gGetCpuClockModified && gGetCpuClock && MmIsAddressValid(gGetCpuClock))
    {
        *gGetCpuClock = (PVOID)gOriginalGetCpuClock;
        gGetCpuClockModified = false;
    }

    if(gBuildNumber > 18363)
    {
        if(gReferenceTimeModified && gHvlpGetReferenceTimeUsingTscPage)
        {
            *(PULONG_PTR)gHvlpGetReferenceTimeUsingTscPage = gOriginalHvlpGetReferenceTimeUsingTscPage;
            gReferenceTimeModified = false;
        }

        if(gHalpCounterModified && gHalpPerformanceCounterType)
        {
            LARGE_INTEGER before = KeQueryPerformanceCounter(NULL);
            *gHalpPerformanceCounterType = gOriginalHalpPerformanceCounterType;
            if(gHalpOriginalPerformanceCounter)
                *(PULONG_PTR)gHalpOriginalPerformanceCounter = gOriginalHalpOriginalPerformanceCounter;
            LARGE_INTEGER after = KeQueryPerformanceCounter(NULL);
            if(gQpcPointer && after.QuadPart - before.QuadPart > kCounterBaseRate)
                *gQpcPointer -= (after.QuadPart - before.QuadPart);
            gHalpCounterModified = false;
        }

        if(gHvlGetQpcBiasModified && gHvlGetQpcBias)
        {
            *(PULONG_PTR)gHvlGetQpcBias = (ULONG_PTR)gOriginalHvlGetQpcBias;
            gHvlGetQpcBiasModified = false;
        }
    }

    ReleaseAllocatedState();
    ClearResolvedState();
    gCallback = NULL;
    gBuildNumber = 0;
    gOriginalGetCpuClock = 0;
    gOriginalHvlGetQpcBias = NULL;
    gOriginalHvlpGetReferenceTimeUsingTscPage = 0;
    gOriginalHalpPerformanceCounterType = 0;
    gOriginalHalpOriginalPerformanceCounter = 0;
    gClientId = {};
    gState = InfinityStateUninitialized;

    if(!NT_SUCCESS(traceStatus))
        return traceStatus;
    return restartStatus;
#endif
}
