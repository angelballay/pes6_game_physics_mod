#include "FileLogSink.h"
#include <Windows.h>

FileLogSink::FileLogSink()
    : file_(INVALID_HANDLE_VALUE)
{
    InitializeCriticalSection(&cs_);
}

FileLogSink::~FileLogSink()
{
    Close();
    DeleteCriticalSection(&cs_);
}

bool FileLogSink::Open(const char* path)
{
    // Required path: D:/pes/IA/logs.txt
    CreateDirectoryA("D:\\pes", nullptr);
    CreateDirectoryA("D:\\pes\\IA", nullptr);

    file_ = CreateFileA(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    return file_ != INVALID_HANDLE_VALUE;
}

void FileLogSink::Close()
{
    if (file_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
}

void FileLogSink::WriteLine(const char* line)
{
    if (file_ == INVALID_HANDLE_VALUE || line == nullptr)
        return;

    EnterCriticalSection(&cs_);
    DWORD written = 0;
    WriteFile(file_, line, static_cast<DWORD>(lstrlenA(line)), &written, nullptr);
    WriteFile(file_, "\r\n", 2, &written, nullptr);
    LeaveCriticalSection(&cs_);
}
