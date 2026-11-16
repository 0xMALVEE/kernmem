#include "ConfigLoader.h"
#include "Settings.h"
#include "Logger.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <map>
#include <cstdlib>

namespace ConfigLoader {

    // --- HELPER: Read File ---
    std::string ReadFileToString(const std::string& path) {
        std::ifstream f(path);
        if (!f.is_open()) return "";
        std::stringstream buf;
        buf << f.rdbuf();
        return buf.str();
    }

    // --- HELPER: Normalize Path ---
    std::string CleanPath(std::string path) {
        path.erase(std::remove(path.begin(), path.end(), '\"'), path.end());
        std::replace(path.begin(), path.end(), '\\', '/');
        size_t doubleSlash;
        while ((doubleSlash = path.find("//")) != std::string::npos) {
            path.replace(doubleSlash, 2, "/");
        }
        if (!path.empty() && path.back() == '/') path.pop_back();
        return path;
    }

    // --- HELPER: Strip Executable ---
    std::string StripExecName(std::string path) {
        size_t lastSlash = path.rfind('/');
        size_t lastDot = path.rfind('.');
        if (lastDot != std::string::npos && lastDot > lastSlash) {
            if (lastSlash != std::string::npos) return path.substr(0, lastSlash);
        }
        return path;
    }

    // --- CORE LOGIC: Map Config String -> Scan Code -> Virtual Key ---
    int GetVKFromConfig(std::string keyStr) {
        if (keyStr.empty()) return 0;

        // Clean brackets: "[g]" -> "g"
        size_t lastOpen = keyStr.rfind('[');
        size_t lastClose = keyStr.rfind(']');
        if (lastOpen != std::string::npos && lastClose != std::string::npos && lastClose > lastOpen) {
            keyStr = keyStr.substr(lastOpen + 1, lastClose - lastOpen - 1);
        }

        // 1. Map String to PHYSICAL SCAN CODE (US Standard Locations)
        // This maps the "Name" of the key to "Where it is physically located"
        static std::map<std::string, int> scanCodeMap = {
            // Row 1 (Function) - Usually hardware fixed
            {"Esc", 0x01}, {"F1", 0x3B}, {"F2", 0x3C}, {"F3", 0x3D}, {"F4", 0x3E},
            {"F5", 0x3F}, {"F6", 0x40}, {"F7", 0x41}, {"F8", 0x42},

            // Row 2 (Numbers)
            {"`", 0x29}, {"~", 0x29}, // The key under ESC. US: `~ | IT: \|
            {"1", 0x02}, {"2", 0x03}, {"3", 0x04}, {"4", 0x05}, {"5", 0x06},
            {"6", 0x07}, {"7", 0x08}, {"8", 0x09}, {"9", 0x0A}, {"0", 0x0B},
            {"-", 0x0C}, {"_", 0x0C}, {"=", 0x0D}, {"+", 0x0D}, {"Backspace", 0x0E},

            // Row 3 (QWERTY)
            {"Tab", 0x0F},
            {"q", 0x10}, {"w", 0x11}, {"e", 0x12}, {"r", 0x13}, {"t", 0x14},
            {"y", 0x15}, {"u", 0x16}, {"i", 0x17}, {"o", 0x18}, {"p", 0x19},
            {"[", 0x1A}, {"{", 0x1A}, {"]", 0x1B}, {"}", 0x1B}, {"Return", 0x1C},

            // Row 4 (ASDF)
            {"Ctrl", 0x1D}, // Left Ctrl
            {"a", 0x1E}, {"s", 0x1F}, {"d", 0x20}, {"f", 0x21}, {"g", 0x22},
            {"h", 0x23}, {"j", 0x24}, {"k", 0x25}, {"l", 0x26},
            {";", 0x27}, {":", 0x27}, {"'", 0x28}, {"\"", 0x28},
            {"\\", 0x2B}, {"|", 0x2B}, // US Backslash (above enter)

            // Row 5 (ZXCV)
            {"LShift", 0x2A}, {"Shift", 0x2A},
            {"z", 0x2C}, {"x", 0x2D}, {"c", 0x2E}, {"v", 0x2F}, {"b", 0x30},
            {"n", 0x31}, {"m", 0x32},
            {",", 0x33}, {"<", 0x33}, {".", 0x34}, {">", 0x34}, {"/", 0x35}, {"?", 0x35},

            // Misc
            {"Space", 0x39}, {"Alt", 0x38}
        };

        int scanCode = 0;

        // Case 1: Known String (e.g. "Space", "Tab", "`")
        if (scanCodeMap.count(keyStr)) {
            scanCode = scanCodeMap[keyStr];
        }
        // Case 2: Single Letter fallback (e.g. "k")
        else if (keyStr.length() == 1) {
            char c = tolower(keyStr[0]);
            std::string s(1, c);
            if (scanCodeMap.count(s)) {
                scanCode = scanCodeMap[s];
            }
        }

        if (scanCode == 0) return 0;

        // 2. Convert Scan Code to Virtual Key for CURRENT LAYOUT
        // MAPVK_VSC_TO_VK (1) takes a hardware scan code and returns the VK for the active keyboard.
        // Example: Scan 0x29 (Key under Esc) -> IT Layout -> Returns VK_OEM_5 (220, '\')
        // Example: Scan 0x10 (Top Left Letter) -> FR Layout -> Returns VK_A ('a')
        UINT vk = MapVirtualKeyA(scanCode, MAPVK_VSC_TO_VK);

        // Failsafe: If mapping failed (rare), hardcode simple letter ASCII
        if (vk == 0 && keyStr.length() == 1) {
            return toupper(keyStr[0]);
        }

        return vk;
    }

    bool TryParseConfig(const std::string& configPath) {
        std::string cfgContent = ReadFileToString(configPath);
        if (cfgContent.empty()) return false;

        // 1. Get Keybind
        bool foundKey = false;
        size_t keyPos = cfgContent.find("\"name\": \"evtChampionOnly\"");
        if (keyPos != std::string::npos) {
            size_t valLabel = cfgContent.find("\"value\":", keyPos);
            if (valLabel != std::string::npos) {
                size_t qStart = cfgContent.find("\"", valLabel + 8);
                size_t qEnd = cfgContent.find("\"", qStart + 1);
                if (qStart != std::string::npos && qEnd != std::string::npos) {
                    std::string val = cfgContent.substr(qStart + 1, qEnd - qStart - 1);
                    Logger::Add("[CFG] Found String: %s", val.c_str());

                    int vk = GetVKFromConfig(val);
                    if (vk != 0) {
                        Settings::AttackChampionsOnlyKey = vk;
                        foundKey = true;
                    }
                }
            }
        }

        if (!foundKey) return false;

        // 2. Get Toggle Mode
        Settings::IsTCOToggle = false;
        size_t togglePos = cfgContent.find("\"name\": \"TargetChampionsOnlyAsToggle\"");
        if (togglePos != std::string::npos) {
            size_t valLabel = cfgContent.find("\"value\":", togglePos);
            if (valLabel != std::string::npos) {
                size_t qStart = cfgContent.find("\"", valLabel + 8);
                size_t qEnd = cfgContent.find("\"", qStart + 1);
                if (qStart != std::string::npos && qEnd != std::string::npos) {
                    std::string val = cfgContent.substr(qStart + 1, qEnd - qStart - 1);
                    if (val == "1") Settings::IsTCOToggle = true;
                }
            }
        }

        Logger::Add("[CFG] Success! VK: %d | Mode: %s",
            Settings::AttackChampionsOnlyKey, Settings::IsTCOToggle ? "Toggle" : "Hold");
        return true;
    }

    void LoadLeagueSettings() {
        Logger::Add("[CFG] Starting Auto-Detection...");

        char* buf = nullptr; size_t sz = 0;
        if (_dupenv_s(&buf, &sz, "ProgramData") != 0 || !buf) return;
        std::string programData = buf; free(buf);

        std::string rcPath = programData + "/Riot Games/RiotClientInstalls.json";
        rcPath = CleanPath(rcPath);

        std::string rcContent = ReadFileToString(rcPath);
        if (rcContent.empty()) {
            Logger::Add("[CFG] Error: No RiotClientInstalls.json");
            return;
        }

        size_t searchPos = 0;
        bool foundAny = false;

        while (true) {
            size_t lolPos = rcContent.find("League of Legends", searchPos);
            if (lolPos == std::string::npos) break;

            size_t qEnd = rcContent.find("\"", lolPos);
            size_t qStart = rcContent.rfind("\"", qEnd - 1);

            if (qStart != std::string::npos && qEnd != std::string::npos && qEnd > qStart) {
                std::string path = rcContent.substr(qStart + 1, qEnd - qStart - 1);
                path = CleanPath(path);
                path = StripExecName(path);

                if (TryParseConfig(path + "/Config/PersistedSettings.json")) {
                    foundAny = true;
                    break;
                }
            }
            searchPos = lolPos + 1;
        }

        if (!foundAny) Logger::Add("[CFG] Failed to find config.");
    }
}