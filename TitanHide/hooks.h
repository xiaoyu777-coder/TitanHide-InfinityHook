#ifndef _HOOKS_H
#define _HOOKS_H

#include "_global.h"

typedef struct _HOOKS_INITIALIZE_RESULT
{
    NTSTATUS Status;
    ULONG HookCount;
} HOOKS_INITIALIZE_RESULT, *PHOOKS_INITIALIZE_RESULT;

class Hooks
{
public:
    static HOOKS_INITIALIZE_RESULT Initialize();
    static void Deinitialize();
};

#endif