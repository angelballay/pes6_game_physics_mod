#include "pch.h"
#include "Logger.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static const char* LOG_PATH = "D:\\pes6_passspeed.log";

void WriteLog(const char* text)
{
    HANDLE hFile = CreateFileA(
        LOG_PATH,
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(hFile, text, (DWORD)strlen(text), &written, nullptr);
        WriteFile(hFile, "\r\n", 2, &written, nullptr);
        CloseHandle(hFile);
    }
}

void LogFormat(const char* format, ...)
{
    char buffer[1024];

    va_list args;
    va_start(args, format);
    vsprintf_s(buffer, sizeof(buffer), format, args);
    va_end(args);

    WriteLog(buffer);
}