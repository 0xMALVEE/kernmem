/*
 * log.h - tiny console logger for clients.
 *
 * The loader has its own logger; clients are separate executables and do not link
 * it. Header-only on purpose: no translation unit to keep in sync, nothing to
 * link.
 */
#pragma once

#include <windows.h>

#include <cstdarg>
#include <cstdio>

namespace km_log
{
    enum Colour
    {
        Grey = 8,
        Green = 10,
        Yellow = 14,
        Red = 12,
        White = 15
    };

    inline void Set(Colour colour)
    {
        static HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTextAttribute(out, (WORD)colour);
    }

    inline void Write(Colour colour, const char* prefix, const char* format, va_list args)
    {
        Set(colour);
        std::printf("[%s] ", prefix);
        std::vprintf(format, args);
        std::printf("\n");
        Set(White);
    }

    inline void Step(const char* format, ...)
    {
        va_list args; va_start(args, format);
        Write(Grey, "-", format, args);
        va_end(args);
    }

    inline void Info(const char* format, ...)
    {
        va_list args; va_start(args, format);
        Write(White, "*", format, args);
        va_end(args);
    }

    inline void Good(const char* format, ...)
    {
        va_list args; va_start(args, format);
        Write(Green, "+", format, args);
        va_end(args);
    }

    inline void Warn(const char* format, ...)
    {
        va_list args; va_start(args, format);
        Write(Yellow, "!", format, args);
        va_end(args);
    }

    inline void Error(const char* format, ...)
    {
        va_list args; va_start(args, format);
        Write(Red, "x", format, args);
        va_end(args);
    }
}
