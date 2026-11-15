// Debug.h
#pragma once
#include <Windows.h>
#include <iostream>

inline void AttachConsoleOnce() {
    static bool attached = false;
    if (attached) return;
    attached = true;

    AllocConsole();
    freopen_s((FILE**)stdin, "CONIN$", "r", stdin);
    freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
    freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

    std::cout << "[leagueone] Console attached\n";
}
