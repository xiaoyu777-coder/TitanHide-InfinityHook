#ifndef _NTDLL_H
#define _NTDLL_H

#include "_global.h"

class NTDLL
{
public:
    static NTSTATUS Initialize();
    static void Deinitialize();
    static int GetExportSsdtIndex(const char* ExportName);
    static int GetWow64ExportSsdtIndex(const char* ExportName);

private:
    static int GetExportSsdtIndex(const unsigned char* Data, ULONG Size, const char* ExportName);

    static unsigned char* NativeFileData;
    static ULONG NativeFileSize;
    static unsigned char* Wow64FileData;
    static ULONG Wow64FileSize;
};

#endif //_NTDLL_H
