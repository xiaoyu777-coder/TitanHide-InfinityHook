#ifndef _INFINITYHOOK_BACKEND_H
#define _INFINITYHOOK_BACKEND_H

#include "../_global.h"

typedef struct _INFINITYHOOK_SYSCALL_HOOK
{
    ULONG SyscallIndex;
    PVOID HookFunction;
} INFINITYHOOK_SYSCALL_HOOK, *PINFINITYHOOK_SYSCALL_HOOK;

class InfinityHookBackend
{
public:
    static NTSTATUS Initialize(const INFINITYHOOK_SYSCALL_HOOK* Hooks, ULONG HookCount);
    static NTSTATUS Deinitialize();
    static ULONG InstalledCount();
};

#endif
