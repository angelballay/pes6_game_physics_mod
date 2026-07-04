#include "FileLogSink.h"
#include <Windows.h>

FileLogSink::FileLogSink()
    : file_(INVALID_HANDLE_VALUE)
{
    path_[0] = '\0';
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
    // fix8c: do not keep the log file permanently locked. Store the path and
    // open/write/close for each line so logs.txt can be renamed or deleted while PES is running.
    CreateDirectoryA("D:\\pes", nullptr);
    CreateDirectoryA("D:\\pes\\IA", nullptr);

    if (path == nullptr || path[0] == '\0')
        return false;

    lstrcpynA(path_, path, MAX_PATH);

    // Touch the file once so startup failures are visible, but close immediately.
    HANDLE h = CreateFileA(
        path_,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (h == INVALID_HANDLE_VALUE)
    {
        path_[0] = '\0';
        return false;
    }

    CloseHandle(h);
    return true;
}

void FileLogSink::Close()
{
    if (file_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
    // Keep path_ until destruction? No: Shutdown should mark logger closed.
    path_[0] = '\0';
}

bool FileLogSink::WriteLineOpenClose(const char* line)
{
    if (path_[0] == '\0' || line == nullptr)
        return false;

    HANDLE h = CreateFileA(
        path_,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (h == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    WriteFile(h, line, static_cast<DWORD>(lstrlenA(line)), &written, nullptr);
    WriteFile(h, "\r\n", 2, &written, nullptr);
    FlushFileBuffers(h);
    CloseHandle(h);
    return true;
}

void FileLogSink::WriteLine(const char* line)
{
    if (line == nullptr || path_[0] == '\0')
        return;

    EnterCriticalSection(&cs_);
    WriteLineOpenClose(line);
    LeaveCriticalSection(&cs_);
}
