#include "ntdll.h"
#include "log.h"
#include "pe.h"

unsigned char* NTDLL::NativeFileData = 0;
ULONG NTDLL::NativeFileSize = 0;
unsigned char* NTDLL::Wow64FileData = 0;
ULONG NTDLL::Wow64FileSize = 0;

static NTSTATUS ReadNtdllFile(const wchar_t* Path, unsigned char** FileData, ULONG* FileSize)
{
    UNICODE_STRING FileName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    RtlInitUnicodeString(&FileName, Path);
    InitializeObjectAttributes(&ObjectAttributes, &FileName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);

    if(KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
#ifdef _DEBUG
        DbgPrint("[TITANHIDE] KeGetCurrentIrql != PASSIVE_LEVEL!\n");
#endif
        return STATUS_UNSUCCESSFUL;
    }

    HANDLE FileHandle;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS NtStatus = ZwCreateFile(&FileHandle,
                                     GENERIC_READ,
                                     &ObjectAttributes,
                                     &IoStatusBlock, NULL,
                                     FILE_ATTRIBUTE_NORMAL,
                                     FILE_SHARE_READ,
                                     FILE_OPEN,
                                     FILE_SYNCHRONOUS_IO_NONALERT,
                                     NULL, 0);
    if(NT_SUCCESS(NtStatus))
    {
        FILE_STANDARD_INFORMATION StandardInformation = { 0 };
        NtStatus = ZwQueryInformationFile(FileHandle, &IoStatusBlock, &StandardInformation, sizeof(FILE_STANDARD_INFORMATION), FileStandardInformation);
        if(NT_SUCCESS(NtStatus))
        {
            *FileSize = StandardInformation.EndOfFile.LowPart;
            Log("[TITANHIDE] FileSize of %ws is %08X!\r\n", Path, StandardInformation.EndOfFile.LowPart);
            *FileData = (unsigned char*)RtlAllocateMemory(true, *FileSize);
            if(!*FileData)
            {
                ZwClose(FileHandle);
                return STATUS_NO_MEMORY;
            }

            LARGE_INTEGER ByteOffset;
            ByteOffset.LowPart = ByteOffset.HighPart = 0;
            NtStatus = ZwReadFile(FileHandle,
                                  NULL, NULL, NULL,
                                  &IoStatusBlock,
                                  *FileData,
                                  *FileSize,
                                  &ByteOffset, NULL);

            if(!NT_SUCCESS(NtStatus))
            {
                RtlFreeMemory(*FileData);
                *FileData = 0;
                *FileSize = 0;
                Log("[TITANHIDE] ZwReadFile(%ws) failed with status %08X...\r\n", Path, NtStatus);
            }
        }
        else
            Log("[TITANHIDE] ZwQueryInformationFile(%ws) failed with status %08X...\r\n", Path, NtStatus);
        ZwClose(FileHandle);
    }
    else
        Log("[TITANHIDE] ZwCreateFile(%ws) failed with status %08X...\r\n", Path, NtStatus);
    return NtStatus;
}

NTSTATUS NTDLL::Initialize()
{
    NTSTATUS NtStatus = ReadNtdllFile(L"\\SystemRoot\\system32\\ntdll.dll", &NativeFileData, &NativeFileSize);
    if(!NT_SUCCESS(NtStatus))
        return NtStatus;

    NTSTATUS Wow64Status = ReadNtdllFile(L"\\SystemRoot\\SysWOW64\\ntdll.dll", &Wow64FileData, &Wow64FileSize);
    if(!NT_SUCCESS(Wow64Status))
        Log("[TITANHIDE] WoW64 ntdll.dll unavailable, 32-bit syscall hooks disabled: %08X...\r\n", Wow64Status);

    return NtStatus;
}

void NTDLL::Deinitialize()
{
    if(NativeFileData)
        RtlFreeMemory(NativeFileData);
    NativeFileData = 0;
    NativeFileSize = 0;
    if(Wow64FileData)
        RtlFreeMemory(Wow64FileData);
    Wow64FileData = 0;
    Wow64FileSize = 0;
}

int NTDLL::GetExportSsdtIndex(const char* ExportName)
{
    return GetExportSsdtIndex(NativeFileData, NativeFileSize, ExportName);
}

int NTDLL::GetWow64ExportSsdtIndex(const char* ExportName)
{
    return GetExportSsdtIndex(Wow64FileData, Wow64FileSize, ExportName);
}

int NTDLL::GetExportSsdtIndex(const unsigned char* Data, ULONG Size, const char* ExportName)
{
    if(!Data || Size == 0)
        return -1;

    ULONG_PTR ExportOffset = PE::GetExportOffset(Data, Size, ExportName);
    if(ExportOffset == PE_ERROR_VALUE)
        return -1;

    int SsdtOffset = -1;
    const unsigned char* ExportData = Data + ExportOffset;
    for(int i = 0; i < 32 && ExportOffset + i < Size; i++)
    {
        if(ExportData[i] == 0xC2 || ExportData[i] == 0xC3)  //RET
            break;
        if(ExportData[i] == 0xB8)  //mov eax,X
        {
            SsdtOffset = *(int*)(ExportData + i + 1);
            break;
        }
    }

    if(SsdtOffset == -1)
    {
        Log("[TITANHIDE] SSDT Offset for %s not found...\r\n", ExportName);
    }

    return SsdtOffset;
}
