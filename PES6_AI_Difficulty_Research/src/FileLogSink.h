#pragma once

#include <Windows.h>

class FileLogSink final
{
public:
    FileLogSink();
    ~FileLogSink();

    bool Open(const char* path);
    void Close();
    void WriteLine(const char* line);
    bool IsOpen() const { return file_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE file_;
    CRITICAL_SECTION cs_;
};
