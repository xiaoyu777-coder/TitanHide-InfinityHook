#ifndef _INFINITYHOOK_CORE_H
#define _INFINITYHOOK_CORE_H

#include "infinityhook_defs.h"

class InfinityHookCore
{
public:
    static NTSTATUS Initialize(INFINITY_HOOK_CALLBACK Callback);
    static NTSTATUS Start();
    static NTSTATUS Stop();
};

#endif
