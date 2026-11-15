/*
 * log.h - console output with severity tags and an opt-in file log.
 */
#pragma once

#include <cstdio>
#include <string>

namespace vgb_log
{
    /* Enables echoing every line to a file next to the executable. */
    void OpenFile(const std::wstring& path);
    void CloseFile();

    /* Opens %TEMP%\vgb-logs\<timestamp>.log and returns its path (empty on
     * failure). Every line is flushed, so the file survives a bugcheck and the
     * last line written names the step that killed the machine. */
    std::wstring OpenCrashLog();

    /* Installs a filter that logs unhandled user-mode exceptions. */
    void InstallExceptionFilter();

    void Line(const char* tag, const char* fmt, ...);

    void Info(const char* fmt, ...);
    void Step(const char* fmt, ...);
    void Good(const char* fmt, ...);
    void Warn(const char* fmt, ...);
    void Error(const char* fmt, ...);
}
