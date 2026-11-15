#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <fcntl.h>
#include <io.h>

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

            /* fflush only moves the CRT buffer into the OS cache; a bugcheck
             * takes that with it. Force the bytes onto the disk so the last
             * line really is the step that was executing. */
            FlushFileBuffers(reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(g_file))));
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

        /*
         * _wfopen_s opens with no sharing at all, so a running log could not be
         * read while the process was alive - which is exactly when it matters.
         * Open a share-read handle and wrap it in a FILE* instead.
         */
        HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (handle == INVALID_HANDLE_VALUE)
            return;

        const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_APPEND);

        if (fd == -1)
        {
            CloseHandle(handle);
            return;
        }

        g_file = _fdopen(fd, "a");

        if (!g_file)
            _close(fd);
    }

    void CloseFile()
    {
        if (!g_file)
            return;

        fclose(g_file);
        g_file = nullptr;
    }

    std::wstring OpenCrashLog()
    {
        wchar_t temp[MAX_PATH + 1] = { 0 };

        if (!GetTempPathW(MAX_PATH + 1, temp))
            return L"";

        std::wstring directory = temp;

        if (!directory.empty() && directory.back() == L'\\')
            directory.pop_back();

        directory += L"\\vgb-logs";
        CreateDirectoryW(directory.c_str(), nullptr);

        SYSTEMTIME now;
        GetLocalTime(&now);

        wchar_t name[64] = { 0 };
        swprintf_s(name, L"\\%04u%02u%02u-%02u%02u%02u.log",
                   now.wYear, now.wMonth, now.wDay,
                   now.wHour, now.wMinute, now.wSecond);

        const std::wstring path = directory + name;
        OpenFile(path);

        return g_file ? path : L"";
    }

    namespace
    {
        LONG WINAPI ExceptionFilter(EXCEPTION_POINTERS* info)
        {
            const DWORD code = info && info->ExceptionRecord
                                   ? info->ExceptionRecord->ExceptionCode
                                   : 0;

            Line("-", "UNHANDLED EXCEPTION 0x%08X at %p",
                 code, info && info->ExceptionRecord
                           ? info->ExceptionRecord->ExceptionAddress
                           : nullptr);

            CloseFile();

            return EXCEPTION_EXECUTE_HANDLER;
        }
    }

    void InstallExceptionFilter()
    {
        SetUnhandledExceptionFilter(ExceptionFilter);
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