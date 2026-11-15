#include "Debugger.h"
#include "Offsets.h"
#include "Logger.h"
#include "SDK.h" 
#include "GameReader.h"
#include <string>
#include <vector>
#include <set>
#include <thread>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream> // ADDED: Required for std::stringstream

namespace Debugger {

    std::vector<MemoryPath> SavedPaths;
    std::atomic<bool> IsScanning = false;
    std::atomic<float> ScanProgress = 0.0f;

    bool SafeCopy(void* dst, uintptr_t src, size_t size) {
        if (!IsValidPtr((void*)src)) return false;
        __try { memcpy(dst, (void*)src, size); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    template<typename T>
    T Read(uintptr_t addr, T def = T()) {
        T val;
        return SafeCopy(&val, addr, sizeof(T)) ? val : def;
    }

    bool IsPrintableString(uintptr_t addr) {
        char buf[16] = { 0 };
        if (!SafeCopy(buf, addr, 15)) return false;
        int printable = 0;
        for (int i = 0; i < 15; i++) {
            if (buf[i] == 0 && i > 3) return true;
            if (buf[i] < 32 || buf[i] > 126) return false;
            printable++;
        }
        return printable > 3;
    }

    void GetEntitiesFromManager(uintptr_t base, uint64_t offset, std::set<uintptr_t>& outSet) {
        uintptr_t mgr = Read<uintptr_t>(base + offset, 0);
        if (IsValidPtr((void*)mgr)) {
            uintptr_t arr = Read<uintptr_t>(mgr + 0x08, 0);
            int count = Read<int>(mgr + 0x10, 0);
            if (count > 0 && count < 3000 && IsValidPtr((void*)arr)) {
                for (int i = 0; i < count; i++) {
                    uintptr_t obj = Read<uintptr_t>(arr + (i * 8), 0);
                    if (IsValidPtr((void*)obj)) outSet.insert(obj);
                }
            }
        }
    }

    void ScanMinions() {}
    void StartPointerScan(uintptr_t ceAddr) {}
    void ResetPingScan() {}

    // --- DEEP POINTER CATCHER ---
    DWORD WINAPI CatchAnySpawnThread(LPVOID) {
        uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
        uintptr_t localPlayer = Read<uintptr_t>(base + Offset::oLocalPlayer, 0);
        Vector3 myPos = Read<Vector3>(localPlayer + 0x25C, { 0,0,0 });

        Logger::Add("[CATCHER] Initializing baseline...");
        std::set<uintptr_t> knownObjects;

        GetEntitiesFromManager(base, Offset::oMinionList, knownObjects);
        GetEntitiesFromManager(base, Offset::oMissileList, knownObjects);

        Logger::Add("[CATCHER] Baseline set. Fire your spell NOW!");

        int dumpedCount = 0;
        std::ofstream outFile("DeepSpawnDump.txt", std::ios::trunc);
        if (!outFile.is_open()) {
            Logger::Add("[CATCHER] Error opening file.");
            IsScanning = false;
            return 0;
        }

        while (IsScanning) {
            std::set<uintptr_t> currentObjects;
            GetEntitiesFromManager(base, Offset::oMinionList, currentObjects);
            GetEntitiesFromManager(base, Offset::oMissileList, currentObjects);

            for (uintptr_t obj : currentObjects) {
                if (knownObjects.find(obj) == knownObjects.end()) {
                    knownObjects.insert(obj);
                    dumpedCount++;

                    Logger::Add("[SPAWN] Caught New Object: 0x%llX", obj);

                    outFile << "====================================================\n";
                    outFile << "NEW OBJECT SPAWNED: 0x" << std::hex << std::uppercase << obj << std::dec << "\n";
                    outFile << "Reference Player Pos: (" << myPos.x << ", " << myPos.y << ", " << myPos.z << ")\n";
                    outFile << "====================================================\n\n";

                    // Scan every 8 bytes for a pointer.
                    for (int i = 0x8; i < 0x500; i += 8) {
                        uintptr_t ptr = Read<uintptr_t>(obj + i, 0);

                        // If it is a valid pointer to somewhere else in memory
                        if (IsValidPtr((void*)ptr) && ptr > 0x1000000) {

                            bool foundDataInPointer = false;
                            std::stringstream pointerData;
                            pointerData << ">>> Following Pointer at Root + 0x" << std::hex << i << " (-> 0x" << ptr << ") <<<\n";

                            // 1. Scan the sub-object for strings
                            for (int j = 0; j < 0x200; j += 8) {
                                if (IsPrintableString(ptr + j)) {
                                    char buf[64] = { 0 }; SafeCopy(buf, ptr + j, 63);
                                    pointerData << "    [STR] +0x" << std::hex << j << std::dec << ": " << buf << "\n";
                                    foundDataInPointer = true;
                                }
                            }

                            // 2. Scan the sub-object for coordinates near the player
                            for (int j = 0; j < 0x200; j += 4) {
                                Vector3 v = Read<Vector3>(ptr + j, { 0,0,0 });
                                if (v.x != 0 && abs(v.x) < 15000 && abs(v.y) < 2000 && abs(v.z) < 15000) {
                                    if (v.distance(myPos) < 2000.0f) {
                                        pointerData << "    [VEC] +0x" << std::hex << j << std::dec
                                            << ": (" << v.x << ", " << v.y << ", " << v.z << ")\n";
                                        foundDataInPointer = true;
                                    }
                                }
                            }

                            // If we found either a string or a coordinate, write it to the file
                            if (foundDataInPointer) {
                                outFile << pointerData.str() << "\n";
                            }
                        }
                    }
                    outFile << "====================================================\n\n";

                    // Stop after catching 3 objects (Usually spell spawns the physical missile + an SFX + a particle)
                    if (dumpedCount >= 3) {
                        outFile.close();
                        Logger::Add("[CATCHER] Captured 3 objects. Check DeepSpawnDump.txt!");
                        IsScanning = false;
                        return 0;
                    }
                }
            }
            Sleep(5); // Loop incredibly fast so we catch it instantly
        }
        outFile.close();
        return 0;
    }

    void DumpMissilesToFile() {
        if (IsScanning) {
            IsScanning = false;
            Logger::Add("[CATCHER] Stopped.");
        }
        else {
            IsScanning = true;
            CreateThread(nullptr, 0, CatchAnySpawnThread, nullptr, 0, nullptr);
        }
    }

    void LogActiveMissiles() {
        auto missiles = GameReader::GetMissiles();
        int active = 0;

        for (auto* m : missiles) {
            std::string name = m->GetMissileSpellName();
            if (name.empty() || name.find("BasicAttack") != std::string::npos || name.find("sfx") != std::string::npos)
                continue;

            Vector3 curr = m->GetMissileCurrentPos();
            if (curr.x != 0 && curr.z != 0) {
                Logger::Add("[LIVE] %s @ X: %.1f | Y: %.1f | Z: %.1f", name.c_str(), curr.x, curr.y, curr.z);
                active++;
            }
        }

        if (active == 0) {
            Logger::Add("[LIVE] No skillshots active.");
        }
    }
}

#include "PatternScanner.h" 

void Debugger::FindCastSpell() {
    Logger::Add("[SCAN] Searching for CastSpell...");
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);

    // This is the "Huge Prologue" pattern common for the main CastSpell wrapper
    // 40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ? ? ? ?
    std::vector<int> pattern = {
        0x40, 0x53, 0x55, 0x56, 0x57,
        0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x81, 0xEC
    };

    // Scan the executable text section (approx first 15MB)
    uintptr_t addr = PatternScanner::FindPattern(base, 0x1000000, pattern);

    if (addr != 0) {
        Logger::Add("[SCAN] FOUND CastSpell at: 0x%llX", addr);
        Logger::Add("[SCAN] Offset: 0x%llX", addr - base);

        // This log is for you to copy-paste into Offsets.h later
        Logger::Add("#define oCastSpell 0x%llX", addr - base);
    }
    else {
        Logger::Add("[SCAN] CastSpell pattern failed.");
    }
}

void Debugger::DumpHudInstance() {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    // Read the main pointer at oHudInstance
    uintptr_t hudMgr = SafeReadSDK<uintptr_t>(base + Offset::oHudInstance, 0);

    if (!IsValidPtr((void*)hudMgr)) {
        Logger::Add("[DUMP] ERROR: oHudInstance (0x%llX) points to null/invalid memory!", Offset::oHudInstance);
        return;
    }

    Logger::Add("=== HUD INSTANCE DUMP (Base: 0x%llX) ===", hudMgr);
    Logger::Add("Looking for HudInputLogic pointer...");

    // Scan the first 0x100 bytes (Structure members)
    for (int i = 0; i < 0x100; i += 8) {
        uintptr_t ptr = SafeReadSDK<uintptr_t>(hudMgr + i, 0);

        // Filter for valid heap pointers (usually 0x1... or 0x2...)
        if (IsValidPtr((void*)ptr) && ptr > 0x1000000) {
            Logger::Add("[+0x%X] -> 0x%llX", i, ptr);
        }
    }
    Logger::Add("=== END DUMP ===");
}

#include "Debugger.h"
#include "PatternScanner.h"
#include "Logger.h"
#include "Offsets.h"
#include "SDK.h"
#include <vector>
#include <iomanip>

void Debugger::FindCastSpellPatterns() {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    Logger::Add("--- FINDING REAL GAME FUNCTIONS ---");

    // 1. Find IssueOrder (Movement/Attack)
    // This pattern looks for the logic that translates a Right Click into a Move Order.
    // Sig: 44 88 4C 24 ? 44 88 44 24 ? 48 89 4C 24 ? 55 53 
    std::vector<int> pIssue = { 0x44, 0x88, 0x4C, 0x24, -1, 0x44, 0x88, 0x44, 0x24, -1, 0x48, 0x89, 0x4C, 0x24, -1, 0x55, 0x53 };
    uintptr_t aIssue = PatternScanner::FindPattern(base, 0x4000000, pIssue);
    if (aIssue) Logger::Add("[FOUND] oIssueOrder: 0x%llX", aIssue - base);

    // 2. Find Hud CastSpell Wrapper
    // This is the function at 0x275650. Let's see if a better signature finds it or a different one.
    // Sig: 40 53 55 56 57 41 54 41 55 41 56 41 57 48 83 EC 60
    std::vector<int> pHudCast = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x60 };
    uintptr_t aHudCast = PatternScanner::FindPattern(base, 0x4000000, pHudCast);
    if (aHudCast) Logger::Add("[FOUND] oCastSpellWrapper: 0x%llX", aHudCast - base);

    // 3. Find the "WorldToScreen" pointer (To fix the startup crash)
    // We will look for the "ViewProjectionMatrix" reference
    std::vector<int> pW2S = { 0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x8D, 0x0D, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1, 0x4C, 0x8B, 0xC0 };
    uintptr_t aW2S = PatternScanner::FindPattern(base, 0x4000000, pW2S);
    if (aW2S) {
        // Follow the RIP relative offset to get the real function address
        int32_t rel = *(int32_t*)(aW2S + 15); // Offset to the call
        uintptr_t w2sFunc = aW2S + 19 + rel;
        Logger::Add("[FOUND] oWorldToScreen: 0x%llX", w2sFunc - base);
    }

    Logger::Add("------------------------------------");
}

void Debugger::InspectSpellSlot(int slot) {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t local = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);

    // Get the slot pointer from the array we found at 0x3BC8
    // (Note: This is equivalent to Reading Spellbook + 0xAE0)
    uintptr_t slotPtr = SafeReadSDK<uintptr_t>(local + 0x3BC8 + (slot * 8), 0);

    if (!IsValidPtr((void*)slotPtr)) {
        Logger::Add("[INSPECT] Slot %d is null.", slot);
        return;
    }

    Logger::Add("=== SPELL SLOT %d INSPECTOR (Ptr: 0x%llX) ===", slot, slotPtr);

    // Check Candidate Offsets for Level (Int)
    // We expect to see '1' (or current level)
    int lvl18 = SafeReadSDK<int>(slotPtr + 0x18, -1);
    int lvl20 = SafeReadSDK<int>(slotPtr + 0x20, -1);
    int lvl28 = SafeReadSDK<int>(slotPtr + 0x28, -1);

    Logger::Add("[INT] +0x18: %d", lvl18);
    Logger::Add("[INT] +0x20: %d", lvl20);
    Logger::Add("[INT] +0x28: %d", lvl28);

    // Check Candidate Offsets for SpellInfo (Pointer)
    // We expect a valid Heap Pointer (0x1... or 0x2...)
    uintptr_t ptr120 = SafeReadSDK<uintptr_t>(slotPtr + 0x120, 0);
    uintptr_t ptr128 = SafeReadSDK<uintptr_t>(slotPtr + 0x128, 0);
    uintptr_t ptr130 = SafeReadSDK<uintptr_t>(slotPtr + 0x130, 0);

    Logger::Add("[PTR] +0x120: 0x%llX", ptr120);
    Logger::Add("[PTR] +0x128: 0x%llX", ptr128); // <--- Expecting this one
    Logger::Add("[PTR] +0x130: 0x%llX", ptr130);

    Logger::Add("=========================================");
}

void Debugger::DumpSpellInput(int slot) {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t local = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);
    if (!local) return;

    // Get Slot -> SpellInput
    uintptr_t slotPtr = SafeReadSDK<uintptr_t>(local + 0x3BC8 + (slot * 8), 0);
    uintptr_t spellInput = SafeReadSDK<uintptr_t>(slotPtr + 0x120, 0); // Confirmed SpellInput Ptr

    if (!IsValidPtr((void*)spellInput)) {
        Logger::Add("[DUMP] SpellInput for slot %d is null.", slot);
        return;
    }

    Logger::Add("=== SPELLINPUT DUMP (Ptr: 0x%llX) ===", spellInput);
    Logger::Add("Cast a spell and click this button immediately!");
    Logger::Add("Your Pos: (%.1f, %.1f, %.1f)",
        ((GameObject*)local)->GetPos().x,
        ((GameObject*)local)->GetPos().y,
        ((GameObject*)local)->GetPos().z);

    // Dump the first 0x100 bytes of the struct
    for (int i = 0; i < 0x100; i += 4) {
        // Read as a float first to check for coordinates
        float fVal = SafeReadSDK<float>(spellInput + i, 0.0f);

        // If it looks like part of a coordinate
        if (abs(fVal) > 1.0f && abs(fVal) < 20000.0f) {
            // Check if the next two floats also form a valid Vector3
            Vector3 vec = SafeReadSDK<Vector3>(spellInput + i, { 0,0,0 });
            if (abs(vec.y) < 1000.0f && abs(vec.z) > 1.0f && abs(vec.z) < 20000.0f) {
                Logger::Add("[VEC3] +0x%X: (%.1f, %.1f, %.1f)", i, vec.x, vec.y, vec.z);
                i += 8; // Skip the next 8 bytes since we just printed them
                continue;
            }
        }

        // Otherwise, just print the raw integer value
        int iVal = SafeReadSDK<int>(spellInput + i, 0);
        if (iVal != 0) {
            Logger::Add("[INT]  +0x%X: %d (0x%X)", i, iVal, iVal);
        }
    }
    Logger::Add("====================================");
}

// Define the original function pointer so we can call it after logging
typedef void(__fastcall* fnCastSpellSniff)(void* hudInput, void* spellInfo);
fnCastSpellSniff originalCastSpell = nullptr;

// This is our "Interceptor" function
void __fastcall SniffedCastSpell(void* rcx, void* rdx) {
    // Log the arguments used by the REAL game
    Logger::Add("--- CastSpell SNIFFED ---");
    Logger::Add("RCX (This): 0x%p", rcx);
    Logger::Add("RDX (SpellInfo): 0x%p", rdx);

    // Call the original game function so the spell actually fires
    if (originalCastSpell) {
        originalCastSpell(rcx, rdx);
    }
}

void Debugger::DumpHudVTables() {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t hudMgr = SafeReadSDK<uintptr_t>(base + Offset::oHudInstance, 0);
    if (!hudMgr) return;

    // We will check both candidate objects
    uint64_t targets[] = { 0x18, 0x60 };

    for (uint64_t off : targets) {
        uintptr_t obj = SafeReadSDK<uintptr_t>(hudMgr + off, 0);
        if (!IsValidPtr((void*)obj)) continue;

        uintptr_t vtable = SafeReadSDK<uintptr_t>(obj, 0);
        Logger::Add("--- VTable for HudInstance + 0x%llX ---", off);

        // List the first 20 functions in this class
        for (int i = 0; i < 20; i++) {
            uintptr_t funcAddr = SafeReadSDK<uintptr_t>(vtable + (i * 8), 0);
            if (funcAddr > base) {
                Logger::Add("[%d] Offset: 0x%llX", i, funcAddr - base);
            }
        }
        Logger::Add("------------------------------------");
    }
}

void Debugger::VerifyCastSpell(uintptr_t offset) {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t target = base + offset;

    Logger::Add("=== VERIFYING FUNCTION AT OFFSET 0x%llX ===", offset);

    unsigned char buffer[32];
    if (SafeCopySDK(buffer, (void*)target, 32)) {
        std::stringstream ss;
        ss << "Bytes: ";
        for (int i = 0; i < 32; i++) {
            ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)buffer[i] << " ";
        }
        Logger::Add("%s", ss.str().c_str());

        // Check if it matches our "Sub RSP" pattern (Standard for large functions)
        if (buffer[0] == 0x48 && buffer[1] == 0x89) {
            Logger::Add("[VERIFY] Matches standard x64 function prologue (MOV [RSP+X]).");
        }

        // Check for the sub rsp, 120h we looked for
        bool foundSub = false;
        for (int i = 0; i < 20; i++) {
            if (buffer[i] == 0x48 && buffer[i + 1] == 0x81 && buffer[i + 2] == 0xEC) {
                Logger::Add("[VERIFY] Found stack allocation (SUB RSP, 0x%X). This is a complex function.",
                    *(int*)&buffer[i + 3]);
                foundSub = true;
                break;
            }
        }
        if (!foundSub) Logger::Add("[WARNING] SUB RSP pattern not found in first 20 bytes.");

    }
    else {
        Logger::Add("[VERIFY] ERROR: Could not read memory at 0x%llX", target);
    }
    Logger::Add("==========================================");
}

void Debugger::DiagnosticDump() {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t local = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);
    if (!local) return;

    Logger::Add("--- SEARCHING FOR REAL SPELLBOOK INSTANCE ---");

    // We know the Spellbook VTable is at Base + 0x2753F0
    uintptr_t targetVTable = base + 0x2753F0;
    bool found = false;

    // Scan the first 0x4000 bytes of the player for a pointer to that VTable
    for (int i = 0; i < 0x4000; i += 8) {
        uintptr_t potentialObj = SafeReadSDK<uintptr_t>(local + i, 0);
        if (IsValidPtr((void*)potentialObj)) {
            uintptr_t vtable = SafeReadSDK<uintptr_t>(potentialObj, 0);
            if (vtable == targetVTable) {
                Logger::Add("[FOUND] Real Spellbook Offset: 0x%X", i);
                Logger::Add("[FOUND] Spellbook Instance: 0x%llX", potentialObj);
                found = true;
                // Once you see this offset in the log, put it in Offsets.h
            }
        }
    }

    if (!found) Logger::Add("[FAIL] Could not find Spellbook instance.");
    Logger::Add("-------------------------------------------");
}

void Debugger::DeepDumpCastSpellInfo() {
    Logger::Add("[DUMP] Starting deep CastSpell analysis... Check CastSpellDump.txt");
    std::ofstream outFile("CastSpellDump.txt", std::ios::trunc);
    if (!outFile.is_open()) {
        Logger::Add("[DUMP] Failed to open CastSpellDump.txt");
        return;
    }

    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    outFile << "=== LEAGUEONE CASTSPELL DIAGNOSTIC DUMP ===\n";
    outFile << "Module Base: 0x" << std::hex << std::uppercase << base << std::dec << "\n\n";

    // 1. HUD Instance
    uintptr_t hudInstance = SafeReadSDK<uintptr_t>(base + Offset::oHudInstance, 0);
    outFile << "[1] HUD INSTANCE (Base + 0x" << std::hex << Offset::oHudInstance << ")\n";
    outFile << "HudInstance Ptr: 0x" << std::hex << hudInstance << "\n";
    if (IsValidPtr((void*)hudInstance)) {
        for (int i = 0; i < 0x100; i += 8) {
            uintptr_t ptr = SafeReadSDK<uintptr_t>(hudInstance + i, 0);
            if (IsValidPtr((void*)ptr) && ptr > 0x1000000) {
                uintptr_t vtable = SafeReadSDK<uintptr_t>(ptr, 0);
                outFile << "  +0x" << std::hex << i << ": 0x" << ptr;
                if (IsValidPtr((void*)vtable) && vtable > base && vtable < base + 0x5000000) {
                    outFile << " (VTable: Base + 0x" << (vtable - base) << ")";
                }
                outFile << "\n";
            }
        }
    }
    else {
        outFile << "  -> INVALID HUD INSTANCE!\n";
    }
    outFile << "\n";

    // 2. CastSpell Function
    outFile << "[2] CASTSPELL FUNCTION PATTERN SCAN\n";
    std::vector<int> pattern = {
        0x40, 0x53, 0x55, 0x56, 0x57,
        0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x81, 0xEC
    };
    uintptr_t castSpellAddr = PatternScanner::FindPattern(base, 0x5000000, pattern);
    if (castSpellAddr) {
        outFile << "  Found Pattern at: Base + 0x" << std::hex << (castSpellAddr - base) << "\n";
        outFile << "  Offset::oCastSpell is currently: Base + 0x" << Offset::oCastSpell << "\n";
        outFile << "  First 32 bytes: ";
        unsigned char buf[32];
        if (SafeCopySDK(buf, (void*)castSpellAddr, 32)) {
            for (int i = 0; i < 32; i++) {
                outFile << std::hex << std::setw(2) << std::setfill('0') << (int)buf[i] << " ";
            }
        }
        outFile << "\n";
    }
    else {
        outFile << "  -> CastSpell Pattern NOT FOUND!\n";
    }
    outFile << "\n";

    // 3. Local Player & SpellBook
    uintptr_t localPlayer = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);
    outFile << "[3] LOCAL PLAYER (Base + 0x" << std::hex << Offset::oLocalPlayer << ")\n";
    outFile << "  LocalPlayer Ptr: 0x" << std::hex << localPlayer << "\n";
    if (!IsValidPtr((void*)localPlayer)) {
        outFile << "  -> INVALID LOCAL PLAYER!\n";
        outFile.close();
        return;
    }

    Vector3 myPos = SafeReadSDK<Vector3>(localPlayer + Offset::oObjPosition, { 0,0,0 });
    outFile << "  Player Pos: (" << std::dec << myPos.x << ", " << myPos.y << ", " << myPos.z << ")\n\n";

    uintptr_t spellBook = SafeReadSDK<uintptr_t>(localPlayer + Offset::oObjSpellBook, 0);
    outFile << "  SpellBook Ptr (+0x" << std::hex << Offset::oObjSpellBook << "): 0x" << spellBook << "\n";

    outFile << "\n[4] SPELL SLOTS DUMP\n";
    uintptr_t slotArrayBase = localPlayer + Offset::oSpellSlotArray;
    outFile << "  Assuming Slot Array at LocalPlayer + 0x" << std::hex << Offset::oSpellSlotArray << "\n";

    for (int slot = 0; slot < 4; slot++) {
        uintptr_t slotPtr = SafeReadSDK<uintptr_t>(slotArrayBase + (slot * 8), 0);
        outFile << "  Slot " << slot << " Ptr: 0x" << std::hex << slotPtr << "\n";
        if (IsValidPtr((void*)slotPtr)) {
            outFile << "    --- Scanning Slot " << slot << " for Pointers ---\n";
            for (int i = 0x100; i < 0x180; i += 8) {
                uintptr_t ptr = SafeReadSDK<uintptr_t>(slotPtr + i, 0);
                if (IsValidPtr((void*)ptr) && ptr > 0x1000000) {
                    outFile << "    [+] Pointer at +0x" << std::hex << i << " -> 0x" << ptr << "\n";

                    bool hasString = false;
                    for (int j = 0; j < 0x100; j += 8) {
                        if (IsPrintableString(ptr + j)) {
                            char strBuf[64] = { 0 };
                            SafeCopySDK(strBuf, (void*)(ptr + j), 63);
                            outFile << "        [STR] +0x" << std::hex << j << ": " << strBuf << "\n";
                            hasString = true;
                        }
                        else {
                            uintptr_t strPtr = SafeReadSDK<uintptr_t>(ptr + j, 0);
                            if (IsValidPtr((void*)strPtr) && strPtr > 0x1000000) {
                                if (IsPrintableString(strPtr)) {
                                    char strBuf[64] = { 0 };
                                    SafeCopySDK(strBuf, (void*)strPtr, 63);
                                    outFile << "        [STR PTR] +0x" << std::hex << j << ": " << strBuf << "\n";
                                    hasString = true;
                                }
                            }
                        }
                    }

                    bool hasCoords = false;
                    for (int j = 0; j < 0x100; j += 4) {
                        Vector3 v = SafeReadSDK<Vector3>(ptr + j, { 0,0,0 });
                        if (v.x != 0.0f && abs(v.x) < 20000.0f && abs(v.z) < 20000.0f && abs(v.y) < 2000.0f) {
                            if (v.distance(myPos) < 5000.0f) {
                                outFile << "        [VEC] +0x" << std::hex << j << ": (" << std::dec << v.x << ", " << v.y << ", " << v.z << ")\n";
                                hasCoords = true;
                                j += 8;
                            }
                        }
                    }
                    if (!hasString && !hasCoords) {
                        outFile << "        -> (No strings or local coords found)\n";
                    }
                }
            }
        }
    }

    outFile << "\n=== END DIAGNOSTIC DUMP ===\n";
    outFile.close();
    Logger::Add("[DUMP] Deep CastSpell dump complete.");
}

#include "GameAPI.h"
#include <cmath>

void Debugger::FindStatOffsets() {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t local = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);

    if (!local) {
        Logger::Add("[SCAN] LocalPlayer not found.");
        return;
    }

    Logger::Add("[SCAN] Starting Smart Stat Scanner... Check StatOffsetsDump.txt");

    std::ofstream outFile("StatOffsetsDump.txt", std::ios::trunc);
    if (!outFile.is_open()) return;

    outFile << "=== SMART STAT FINDER ===\n";
    outFile << "Looking for exact values from Local HTTP API in Player Memory...\n\n";

    float targetAD = GameAPI::CurrentAttackDamage;
    float targetAP = GameAPI::CurrentAbilityPower;
    float targetArmor = GameAPI::CurrentArmor;
    float targetMR = GameAPI::CurrentMR;

    outFile << "--- TARGET VALUES ---\n";
    outFile << "Attack Damage: " << targetAD << "\n";
    outFile << "Ability Power: " << targetAP << "\n";
    outFile << "Armor: " << targetArmor << "\n";
    outFile << "Magic Resist: " << targetMR << "\n\n";

    auto CheckMatch = [&](float val, const std::string& locationPath) {
        // Epsilon of 0.05 to handle float precision differences
        if (targetAD > 0 && abs(val - targetAD) < 0.05f) outFile << "[MATCH] ATTACK DAMAGE found at " << locationPath << " (Val: " << val << ")\n";
        if (targetAP > 0 && abs(val - targetAP) < 0.05f) outFile << "[MATCH] ABILITY POWER found at " << locationPath << " (Val: " << val << ")\n";
        if (targetArmor > 0 && abs(val - targetArmor) < 0.05f) outFile << "[MATCH] ARMOR found at " << locationPath << " (Val: " << val << ")\n";
        if (targetMR > 0 && abs(val - targetMR) < 0.05f) outFile << "[MATCH] MAGIC RESIST found at " << locationPath << " (Val: " << val << ")\n";
    };

    outFile << "--- SHALLOW SCAN (Directly in Object) ---\n";
    for (int i = 0x0; i < 0x5000; i += 4) {
        float val = SafeReadSDK<float>(local + i, 0.0f);
        if (val > 0.1f && val < 5000.0f) {
            char path[64];
            sprintf_s(path, "LocalPlayer + 0x%X", i);
            CheckMatch(val, path);
        }
    }

    outFile << "\n--- DEEP SCAN (Inside Pointers) ---\n";
    for (int i = 0x0; i < 0x5000; i += 8) {
        uintptr_t ptr = SafeReadSDK<uintptr_t>(local + i, 0);
        if (IsValidPtr((void*)ptr) && ptr > 0x1000000) {
            for (int j = 0x0; j < 0x1000; j += 4) {
                float val = SafeReadSDK<float>(ptr + j, 0.0f);
                if (val > 0.1f && val < 5000.0f) {
                    char path[64];
                    sprintf_s(path, "LocalPlayer + 0x%X -> + 0x%X", i, j);
                    CheckMatch(val, path);
                }
            }
        }
    }

    // Dump an Enemy Champion for Cross-Referencing
    outFile << "\n\n=== ENEMY CHAMPION RAW FLOAT DUMP ===\n";
    auto enemies = GameReader::GetEnemies();
    if (enemies.empty()) {
        outFile << "No enemies found to dump.\n";
    }
    else {
        GameObject* enemy = enemies[0];
        outFile << "Dumping Enemy: " << enemy->GetChampionName() << " (Ptr: 0x" << std::hex << (uintptr_t)enemy << std::dec << ")\n";
        outFile << "Use the offsets found above to check if they hold valid values here!\n\n";

        for (int i = 0x1000; i < 0x3000; i += 4) {
            float val = SafeReadSDK<float>((uintptr_t)enemy + i, 0.0f);
            if (val > 5.0f && val < 4000.0f) {
                outFile << "Enemy + 0x" << std::hex << i << std::dec << " : " << val << "\n";
            }
        }
    }

    outFile.close();
    Logger::Add("[SCAN] Smart Scan complete.");
}

void Debugger::FindAIManager() {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t local = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);

    if (!local) return;

    Logger::Add("[SCAN] Searching for AIManager... Check AiManagerDump.txt");

    std::ofstream outFile("AiManagerDump.txt", std::ios::trunc);
    if (!outFile.is_open()) return;

    outFile << "=== AIMANAGER SMART FINDER ===\n";
    outFile << "Make sure you are currently walking when you click this button!\n\n";

    GameObject* player = (GameObject*)local;
    Vector3 myPos = player->GetPos();
    float myMS = player->GetMoveSpeed();

    outFile << "Target Pos: (" << myPos.x << ", " << myPos.y << ", " << myPos.z << ")\n";
    outFile << "Target MoveSpeed: " << myMS << "\n\n";

    // AIManager pointer is typically between 0x2500 and 0x4500 in the GameObject
    for (int i = 0x2500; i < 0x4500; i += 8) {
        uintptr_t ptr = SafeReadSDK<uintptr_t>(local + i, 0);

        // If it's a valid heap pointer
        if (IsValidPtr((void*)ptr) && ptr > 0x10000000) {

            bool foundPos = false;
            bool foundMS = false;
            int posOffset = 0;
            int msOffset = 0;

            // Scan inside the potential AIManager (0x0 to 0x500)
            for (int j = 0x0; j < 0x500; j += 4) {
                // Check for MoveSpeed Float
                float val = SafeReadSDK<float>(ptr + j, 0.0f);
                if (myMS > 0 && abs(val - myMS) < 1.0f) {
                    foundMS = true;
                    msOffset = j;
                }

                // Check for Current Position Vector3
                Vector3 v = SafeReadSDK<Vector3>(ptr + j, { 0,0,0 });
                if (v.distance(myPos) < 100.0f) {
                    foundPos = true;
                    posOffset = j;
                }
            }

            // If an object has both your position and your movespeed, it is 99.9% the AIManager!
            if (foundPos || foundMS) {
                outFile << ">>> POTENTIAL AIMANAGER FOUND AT Offset: 0x" << std::hex << i << " <<<\n";
                outFile << "Pointer: 0x" << ptr << "\n";
                if (foundPos) outFile << "  -> Found ServerPos/StartPath at +0x" << posOffset << "\n";
                if (foundMS)  outFile << "  -> Found MoveSpeed at +0x" << msOffset << "\n";

                outFile << "\n--- DUMPING FIRST 0x400 BYTES OF THIS AIMANAGER ---\n";
                for (int d = 0x200; d < 0x400; d += 4) {
                    float fVal = SafeReadSDK<float>(ptr + d, 0.0f);
                    int iVal = SafeReadSDK<int>(ptr + d, 0);
                    uintptr_t pVal = SafeReadSDK<uintptr_t>(ptr + d, 0);

                    outFile << "+0x" << std::hex << d << std::dec << " | Int: " << iVal << " | Float: " << fVal;
                    if (IsValidPtr((void*)pVal) && pVal > 0x10000000) {
                        outFile << " | PTR: 0x" << std::hex << pVal << std::dec;
                    }
                    outFile << "\n";
                }
                outFile << "---------------------------------------------------\n\n";
            }
        }
    }

    outFile.close();
    Logger::Add("[SCAN] AIManager scan complete.");
}

void Debugger::TraceNavArray() {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t local = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);
    if (!local) return;

    Logger::Add("[TRACE] Running XOR Brute-Force Cracker... Check NavArrayDump.txt");

    std::ofstream outFile("NavArrayDump.txt", std::ios::trunc);
    if (!outFile.is_open()) return;

    outFile << "=== NAVARRAY XOR CRACKER ===\n";
    outFile << "Make sure you are ACTIVELY WALKING (long distance) when you click this!\n\n";

    Vector3 myPos = ((GameObject*)local)->GetPos();
    outFile << "Current Pos: (" << myPos.x << ", " << myPos.y << ", " << myPos.z << ")\n\n";

    // 1. Scan LocalPlayer for all possible AIManager pointers (Usually between 0x2000 and 0x5000)
    for (int i = 0x2000; i < 0x5000; i += 8) {
        uintptr_t aiManager = SafeReadSDK<uintptr_t>(local + i, 0);

        if (IsValidPtr((void*)aiManager) && aiManager > 0x10000000) {

            // 2. Scan inside the potential AIManager
            for (int j = 0x0; j < 0x500; j += 8) {
                // Read 8 bytes
                uint64_t valA = SafeReadSDK<uint64_t>(aiManager + j, 0);
                // Read next 8 bytes
                uint64_t valB = SafeReadSDK<uint64_t>(aiManager + j + 8, 0);

                if (valA == 0 && valB == 0) continue;

                // 3. Prepare decryption candidates
                uintptr_t candidates[3];
                candidates[0] = valA;                   // Plain Pointer
                candidates[1] = valA ^ (~valB);         // A is Value, B is Key
                candidates[2] = valB ^ (~valA);         // B is Value, A is Key

                std::string methods[3] = { "PLAIN", "A ^ ~B", "B ^ ~A" };

                // 4. Test all candidates
                for (int c = 0; c < 3; c++) {
                    uintptr_t testPtr = candidates[c];

                    // If the decrypted value is a valid heap pointer
                    if (IsValidPtr((void*)testPtr) && testPtr > 0x10000000) {

                        // Read the first waypoint
                        Vector3 point1 = SafeReadSDK<Vector3>(testPtr, { 0,0,0 });

                        // Is it a valid coordinate near the player?
                        if (point1.x != 0 && point1.y > -100.0f && point1.y < 1000.0f && point1.distance(myPos) < 500.0f) {

                            // Read the second waypoint
                            Vector3 point2 = SafeReadSDK<Vector3>(testPtr + 12, { 0,0,0 });

                            outFile << ">>> CRACKED NAV ARRAY AT AIManager + 0x" << std::hex << j << " <<<\n";
                            outFile << "AIManager Offset in Player: +0x" << std::hex << i << "\n";
                            outFile << "Decryption Method: " << methods[c] << "\n";
                            outFile << "Decrypted Pointer: 0x" << std::hex << testPtr << std::dec << "\n\n";

                            outFile << "  [Node 0] X: " << point1.x << " | Y: " << point1.y << " | Z: " << point1.z << "\n";

                            if (point2.x != 0 && point2.y > -100.0f && point2.y < 1000.0f) {
                                outFile << "  [Node 1] X: " << point2.x << " | Y: " << point2.y << " | Z: " << point2.z << "\n";
                                outFile << "  ---> SUCCESS! THIS IS THE WAYPOINT ARRAY!\n";
                            }
                            outFile << "---------------------------------------------------\n\n";
                        }
                    }
                }
            }
        }
    }

    outFile.close();
    Logger::Add("[TRACE] XOR Cracker complete. Check NavArrayDump.txt");
}

void Debugger::FindWaypoints() {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t local = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);
    if (!local) return;

    // The discovered inline Navigation Component
    uintptr_t navComponent = local + 0x2D40;

    Logger::Add("[SCAN] Decoding Inline NavPath at LocalPlayer + 0x2D40...");

    std::ofstream outFile("NavStructDump.txt", std::ios::trunc);
    outFile << "=== NAVPATH STRUCT DECODER ===\n";
    outFile << "Base Address: 0x" << std::hex << navComponent << "\n\n";

    Vector3 myPos = ((GameObject*)local)->GetPos();
    outFile << "MyPos: " << std::dec << myPos.x << ", " << myPos.y << ", " << myPos.z << "\n";

    // Dump the struct
    std::vector<uint8_t> mem(0x200);
    SafeCopySDK(mem.data(), (void*)navComponent, 0x200);

    // 1. Look for the Current Segment Index (Int)
    // It should be a small number (0, 1, 2) that increases as you pass nodes.
    outFile << "--- INTEGERS (Potential Indices) ---\n";
    for (int i = 0; i < 0x90; i += 4) {
        int val = *(int*)&mem[i];
        if (val >= 0 && val < 10) {
            outFile << "+0x" << std::hex << i << ": " << std::dec << val << "\n";
        }
    }

    // 2. Look for the Array Start
    // We know from Physics Scan that +0x90 is a valid node.
    outFile << "\n--- VECTOR ARRAY (From Physics Scan) ---\n";
    for (int i = 0x90; i < 0x200; i += 12) { // Assuming 12-byte stride
        Vector3 v = *(Vector3*)&mem[i];

        // Filter empty/junk
        if (v.x != 0 && abs(v.x) < 15000 && abs(v.z) < 15000) {
            float dist = v.distance(myPos);
            outFile << "+0x" << std::hex << i << ": "
                << std::dec << v.x << ", " << v.y << ", " << v.z
                << " (Dist: " << dist << ")\n";
        }
    }

    outFile.close();
    Logger::Add("[SCAN] Done. Check NavStructDump.txt");
}

#include <thread>
#include <chrono>
#include "Utils.h"

void Debugger::RunBehavioralScanner() {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
    uintptr_t local = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);
    if (!local) return;

    Logger::Add("[SCAN] PHYSICS SCAN: Walk in a straight line for 3 seconds.");
    Logger::Add("[SCAN] Scanning in 2...");

    std::thread([=]() {
        Sleep(2000); // Wait for user to start walking

        // --- SAMPLE 1 ---
        Vector3 pos1 = ((GameObject*)local)->GetPos();
        float time1 = Utils::GetGameTime();

        // Snapshot all pointers
        std::vector<uintptr_t> candidates;
        for (int i = 0; i < 0x6000; i += 8) {
            uintptr_t ptr = SafeReadSDK<uintptr_t>(local + i, 0);
            if (IsValidPtr((void*)ptr) && ptr > 0x10000000) candidates.push_back(ptr);
        }

        Sleep(500); // Walk for 0.5s

        // --- SAMPLE 2 ---
        Vector3 pos2 = ((GameObject*)local)->GetPos();
        float time2 = Utils::GetGameTime();

        float distMoved = pos1.distance(pos2);
        float dt = time2 - time1;
        float calculatedSpeed = distMoved / dt;

        if (distMoved < 20.0f) {
            Logger::Add("[SCAN] You didn't move enough! Try again.");
            return;
        }

        std::ofstream outFile("PhysicsDump.txt", std::ios::trunc);
        if (!outFile.is_open()) return;

        outFile << "=== PHYSICS ENGINE SCANNER ===\n";
        outFile << "Player Speed: " << calculatedSpeed << " units/sec\n";
        outFile << "Moved: " << distMoved << " units\n\n";

        for (int i = 0; i < 0x6000; i += 8) {
            uintptr_t ptr = SafeReadSDK<uintptr_t>(local + i, 0);
            if (!IsValidPtr((void*)ptr) || ptr < 0x10000000) continue;

            // Scan inside this object
            for (int j = 0; j < 0x500; j += 4) {

                // 1. Check for SPEED
                float valF = SafeReadSDK<float>(ptr + j, 0.0f);
                if (abs(valF - calculatedSpeed) < 20.0f) { // Allow small error
                    outFile << ">>> SPEED MATCH AT LocalPlayer + 0x" << std::hex << i << " -> +0x" << j << "\n";
                    outFile << "    Value: " << valF << " (Calc: " << calculatedSpeed << ")\n";
                }

                // 2. Check for DESTINATION
                if (j <= 0x500 - 12) {
                    Vector3 dest = SafeReadSDK<Vector3>(ptr + j, { 0,0,0 });

                    // Is it a valid point far away?
                    if (dest.x != 0 && dest.distance(pos2) > 100.0f && dest.distance(pos2) < 10000.0f) {

                        // VECTOR MATH: Is this destination ALIGNED with our movement?
                        // Direction = Normalize(Dest - Start)
                        // Movement  = Normalize(End - Start)
                        // Dot Product should be ~1.0 (Same direction)

                        Vector3 dirToDest = { dest.x - pos1.x, 0, dest.z - pos1.z };
                        float len1 = sqrt(dirToDest.x * dirToDest.x + dirToDest.z * dirToDest.z);
                        dirToDest.x /= len1; dirToDest.z /= len1;

                        Vector3 dirMoved = { pos2.x - pos1.x, 0, pos2.z - pos1.z };
                        float len2 = sqrt(dirMoved.x * dirMoved.x + dirMoved.z * dirMoved.z);
                        dirMoved.x /= len2; dirMoved.z /= len2;

                        float dot = (dirToDest.x * dirMoved.x) + (dirToDest.z * dirMoved.z);

                        if (dot > 0.99f) { // 99% Alignment
                            outFile << ">>> VECTOR ALIGNMENT MATCH AT LocalPlayer + 0x" << std::hex << i << " -> +0x" << j << "\n";
                            outFile << "    Dest: " << std::dec << dest.x << ", " << dest.z << "\n";
                            outFile << "    Dot Product: " << dot << " (Perfect alignment)\n";
                        }
                    }
                }
            }
        }
        outFile.close();
        Logger::Add("[SCAN] Done. Check PhysicsDump.txt");
        }).detach();
}

void Debugger::FindHpBarOffset() {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);

    auto enemies = GameReader::GetEnemies();
    GameObject* enemy = nullptr;
    for (auto* e : enemies) {
        if (e->GetHealth() > 0) { enemy = e; break; }
    }
    if (!enemy) { Logger::Add("[SCAN] Need an enemy on screen!"); return; }

    Vector2 feetPos;
    if (!Utils::WorldToScreen(enemy->GetPos(), feetPos)) return;

    Logger::Add("[SCAN] Running Loose HP Bar Scan... Check HpBarDump.txt");

    std::ofstream outFile("HpBarDump.txt", std::ios::trunc);
    outFile << "Enemy Feet Screen Pos: X=" << std::dec << feetPos.x << ", Y=" << feetPos.y << "\n\n";

    auto ScanRegion = [&](uintptr_t ptr, const std::string& prefix) {
        // Scan for Y coordinates hovering above the champion
        for (int i = 0; i < 0x2000; i += 4) {
            float y = SafeReadSDK<float>(ptr + i, 0.0f);

            if (y > 10.0f && y < feetPos.y && y >(feetPos.y - 300.0f)) {
                // Check adjacent memory for the X coordinate (could be before or after Y)
                float x1 = SafeReadSDK<float>(ptr + i - 4, 0.0f); // X before Y
                float x2 = SafeReadSDK<float>(ptr + i + 4, 0.0f); // X after Y

                // Generous 150px X-tolerance to account for Level Badge asymmetry
                if (abs(x1 - feetPos.x) < 150.0f) {
                    outFile << prefix << " + 0x" << std::hex << (i - 4) << "\n";
                    outFile << "  X: " << std::dec << x1 << " | Y: " << y << "\n\n";
                }
                else if (abs(x2 - feetPos.x) < 150.0f) {
                    outFile << prefix << " + 0x" << std::hex << i << "\n";
                    outFile << "  Y: " << std::dec << y << " | X: " << x2 << "\n\n";
                }
            }
        }
    };

    outFile << "--- SHALLOW SCAN ---\n";
    ScanRegion((uintptr_t)enemy, "Enemy");

    outFile << "--- DEEP SCAN ---\n";
    for (int i = 0; i < 0x5000; i += 8) {
        uintptr_t ptr = SafeReadSDK<uintptr_t>((uintptr_t)enemy + i, 0);
        if (IsValidPtr((void*)ptr) && ptr > 0x10000000) {
            char buf[64]; sprintf_s(buf, "Enemy + 0x%X ->", i);
            ScanRegion(ptr, buf);
        }
    }
    outFile.close();
}

#include "Renderer.h"

void Debugger::FindHpBarDelta() {
    uintptr_t base = (uintptr_t)GetModuleHandle(NULL);

    // Find a valid enemy
    auto enemies = GameReader::GetEnemies();
    GameObject* enemy = nullptr;
    for (auto* e : enemies) {
        if (e->GetHealth() > 0) {
            enemy = e;
            break;
        }
    }
    if (!enemy) {
        Logger::Add("[SCAN] Need an enemy on screen!");
        return;
    }

    // --- SNAPSHOT 1 ---
    Vector2 feet1;
    if (!Utils::WorldToScreen(enemy->GetPos(), feet1)) return;

    // Capture Enemy object and 1-pointer deep structures
    struct MemDump { uintptr_t addr; std::vector<uint8_t> data; };
    std::vector<MemDump> dumps;

    // Dump shallow
    MemDump shallow; shallow.addr = (uintptr_t)enemy; shallow.data.resize(0x5000);
    SafeCopySDK(shallow.data.data(), (void*)shallow.addr, 0x5000);
    dumps.push_back(shallow);

    // Dump deep
    for (int i = 0; i < 0x5000; i += 8) {
        uintptr_t ptr = SafeReadSDK<uintptr_t>((uintptr_t)enemy + i, 0);
        if (IsValidPtr((void*)ptr) && ptr > 0x10000000) {
            MemDump deep; deep.addr = ptr; deep.data.resize(0x500);
            if (SafeCopySDK(deep.data.data(), (void*)ptr, 0x500)) {
                dumps.push_back(deep);
            }
        }
    }

    Logger::Add("[SCAN] SNAPSHOT 1 TAKEN. Do not move your champion!");
    Logger::Add("[SCAN] PAN YOUR CAMERA NOW! (Move screen in 3 secs...)");

    std::thread([=]() {
        Sleep(3000);

        // --- SNAPSHOT 2 ---
        Vector2 feet2;
        if (!Utils::WorldToScreen(enemy->GetPos(), feet2)) return;

        float deltaX = feet2.x - feet1.x;
        float deltaY = feet2.y - feet1.y;

        if (abs(deltaX) < 10.0f && abs(deltaY) < 10.0f) {
            Logger::Add("[SCAN] FAILED: You didn't move the camera enough!");
            return;
        }

        std::ofstream outFile("HpBarDeltaDump.txt", std::ios::trunc);
        outFile << "=== SCREEN DELTA SCANNER ===\n";
        outFile << "Expected Screen Shift: X moved " << deltaX << "px | Y moved " << deltaY << "px\n\n";

        for (const auto& dump : dumps) {
            std::vector<uint8_t> liveMem(dump.data.size());
            if (!SafeCopySDK(liveMem.data(), (void*)dump.addr, dump.data.size())) continue;

            for (int j = 0; j < dump.data.size() - 8; j += 4) {
                // Read Past Screen X/Y
                float pastX = *(float*)&dump.data[j];
                float pastY = *(float*)&dump.data[j + 4];

                // Read Live Screen X/Y
                float liveX = *(float*)&liveMem[j];
                float liveY = *(float*)&liveMem[j + 4];

                // Did this memory shift by the exact same amount as our screen?
                float shiftX = liveX - pastX;
                float shiftY = liveY - pastY;

                // 2.0f tolerance for minor 3D floating point rounding differences
                if (abs(shiftX - deltaX) < 2.0f && abs(shiftY - deltaY) < 2.0f) {

                    // Filter out massive junk floats
                    if (liveX > -2000.0f && liveX < 4000.0f && liveY > -2000.0f && liveY < 4000.0f) {
                        outFile << ">>> PERFECT UI COORDINATE MATCH! <<<\n";
                        outFile << "Base Object: 0x" << std::hex << dump.addr << "\n";
                        outFile << "Offset: +0x" << j << "\n";
                        outFile << "Coordinate Value: (" << std::dec << liveX << ", " << liveY << ")\n";

                        // Tell us where this object came from if it's a pointer
                        if (dump.addr != (uintptr_t)enemy) {
                            for (int k = 0; k < 0x5000; k += 8) {
                                if (SafeReadSDK<uintptr_t>((uintptr_t)enemy + k, 0) == dump.addr) {
                                    outFile << "This is inside: Enemy -> +0x" << std::hex << k << "\n";
                                }
                            }
                        }
                        outFile << "------------------------------------------\n";
                    }
                }
            }
        }

        outFile.close();
        Logger::Add("[SCAN] Done. Check HpBarDeltaDump.txt");
        }).detach();
}