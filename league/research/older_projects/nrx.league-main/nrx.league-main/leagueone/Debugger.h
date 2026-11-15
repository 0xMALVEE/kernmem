#pragma once
#include <Windows.h>
#include <vector>
#include <atomic>
#include "SDK.h" 

namespace Debugger {
    struct MemoryPath {
        uintptr_t offset1;
        uintptr_t offset2;
        uintptr_t offset3;
        int depth;
    };

    extern std::vector<MemoryPath> SavedPaths;
    extern std::atomic<bool> IsScanning;
    extern std::atomic<float> ScanProgress;

    void ScanMinions();
    void ResetPingScan();
    void StartPointerScan(uintptr_t ceAddr);

    // NEW: File Dumper
    void DumpMissilesToFile();
    void LogActiveMissiles();
    void FindCastSpell();
    void DumpHudInstance();
    void FindCastSpellPatterns();
    void DumpHudVTables();
    void InspectSpellSlot(int slot);
    void DumpSpellInput(int slot);
    void VerifyCastSpell(uintptr_t offset);
    void DiagnosticDump();
    void DeepDumpCastSpellInfo();
    void FindStatOffsets();
    void FindAIManager();
    void TraceNavArray();
    void FindWaypoints();
    void RunBehavioralScanner();
    void FindHpBarOffset();
    void FindHpBarDelta();
}