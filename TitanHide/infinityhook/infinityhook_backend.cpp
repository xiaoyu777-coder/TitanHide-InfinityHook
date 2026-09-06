#include "infinityhook_backend.h"
#include "infinityhook_core.h"

namespace
{
    constexpr ULONG kMaxSyscallHooks = 64;

    INFINITYHOOK_SYSCALL_HOOK gHooks[kMaxSyscallHooks] = {};
    ULONG gHookCount = 0;

    void __fastcall InfinityDispatch(ULONG CallIndex, PVOID* CallAddress)
    {
        if(!CallAddress)
            return;

        for(ULONG i = 0; i < gHookCount; i++)
        {
            if(gHooks[i].SyscallIndex == CallIndex)
            {
                *CallAddress = gHooks[i].HookFunction;
                return;
            }
        }
    }
}

NTSTATUS InfinityHookBackend::Initialize(const INFINITYHOOK_SYSCALL_HOOK* Hooks, ULONG HookCount)
{
#ifndef _WIN64
    UNREFERENCED_PARAMETER(Hooks);
    UNREFERENCED_PARAMETER(HookCount);
    return STATUS_NOT_SUPPORTED;
#else
    if(!Hooks || HookCount == 0 || HookCount > kMaxSyscallHooks)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(gHooks, sizeof(gHooks));
    for(ULONG i = 0; i < HookCount; i++)
    {
        if(!Hooks[i].HookFunction)
        {
            gHookCount = 0;
            return STATUS_INVALID_PARAMETER;
        }
        gHooks[i] = Hooks[i];
    }
    gHookCount = HookCount;

    NTSTATUS status = InfinityHookCore::Initialize(InfinityDispatch);
    if(!NT_SUCCESS(status))
    {
        gHookCount = 0;
        RtlZeroMemory(gHooks, sizeof(gHooks));
        return status;
    }

    status = InfinityHookCore::Start();
    if(!NT_SUCCESS(status))
    {
        InfinityHookCore::Stop();
        gHookCount = 0;
        RtlZeroMemory(gHooks, sizeof(gHooks));
        return status;
    }

    return STATUS_SUCCESS;
#endif
}

NTSTATUS InfinityHookBackend::Deinitialize()
{
    NTSTATUS status = InfinityHookCore::Stop();
    gHookCount = 0;
    RtlZeroMemory(gHooks, sizeof(gHooks));
    return status;
}

ULONG InfinityHookBackend::InstalledCount()
{
    return gHookCount;
}
