#include "log.h"

#include <windows.h>

#include <cstdarg>

namespace
{
    FILE* g_file = nullptr;
    CRITICAL_SECTION g_lock;
    bool g_lockReady = false;

    void EnsureLock()
    {
        if (!g_lockReady)
        {
            InitializeCriticalSection(&g_lock);
            g_lockReady = true;
        }
    }

    void Emit(const char* tag, const char* fmt, va_list args)
    {
        EnsureLock();
        EnterCriticalSection(&g_lock);

        if (tag && tag[0])
            printf("[%s] ", tag);

        vprintf(fmt, args);
        printf("\n");

        if (g_file)
        {
            if (tag && tag[0])
                fprintf(g_file, "[%s] ", tag);
            vfprintf(g_file, fmt, args);
            fprintf(g_file, "\n");
            fflush(g_file);
        }

        LeaveCriticalSection(&g_lock);
    }

    void EmitTagged(const char* tag, const char* fmt, va_list args)
    {
        Emit(tag, fmt, args);
    }
}

namespace vgb_log
{
    void OpenFile(const std::wstring& path)
    {
        if (g_file)
            return;

        _wfopen_s(&g_file, path.c_str(), L"w");
    }

    void CloseFile()
    {
        if (!g_file)
            return;

        fclose(g_file);
        g_file = nullptr;
    }

    void Line(const char* tag, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        EmitTagged(tag, fmt, args);
        va_end(args);
    }

    void Info(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        EmitTagged("*", fmt, args);
        va_end(args);
    }

    void Step(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        EmitTagged(">", fmt, args);
        va_end(args);
    }

    void Good(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        EmitTagged("+", fmt, args);
        va_end(args);
    }

    void Warn(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        EmitTagged("!", fmt, args);
        va_end(args);
    }

    void Error(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        EmitTagged("-", fmt, args);
        va_end(args);
    }
}