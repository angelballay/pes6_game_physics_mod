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
    bool IsOpen() const { return path_[0] != '\0'; }

private:
    bool WriteLineOpenClose(const char* line);

    HANDLE file_; // kept only for compatibility / future persistent mode
    CRITICAL_SECTION cs_;
    char path_[MAX_PATH];
};
