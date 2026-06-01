#ifndef _SYSCALL_RESOLVER_H
#define _SYSCALL_RESOLVER_H

#include "_global.h"

class SyscallResolver
{
public:
    static PVOID GetFunctionAddress(const char* ApiName);
};

#endif
