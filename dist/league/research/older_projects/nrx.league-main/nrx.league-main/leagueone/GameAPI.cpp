#include "GameAPI.h"
#include "SDK.h"
#include "Offsets.h"
#include "Logger.h"
#include <winhttp.h>
#include <thread>
#include "PatternScanner.h" 
#include "Utils.h"
#pragma comment(lib, "winhttp.lib")

// --- INTERNAL HELPERS ---
bool RawSafeReadAPI(void* dst, uintptr_t src, size_t size) {
    if (src < 0x10000 || src > 0x7FFFFFFFFFFF) return false;
    __try {
        memcpy(dst, (void*)src, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

int SafeReadIntAPI(uintptr_t addr) {
    int val = 0;
    return RawSafeReadAPI(&val, addr, sizeof(int)) ? val : 0;
}

namespace GameAPI {

    std::atomic<float> CurrentAttackSpeed = 0.625f;
    std::atomic<float> CurrentRange = 550.0f;
    std::atomic<float> CurrentAttackDamage = 0.0f;
    std::atomic<float> CurrentAbilityPower = 0.0f;
    std::atomic<float> CurrentArmor = 0.0f;
    std::atomic<float> CurrentMR = 0.0f;
    std::atomic<float> CurrentArmorPenPercent = 1.0f;
    std::atomic<float> CurrentLethality = 0.0f;
    std::atomic<float> CurrentMagicPenPercent = 1.0f;
    std::atomic<float> CurrentMagicPenFlat = 0.0f;

    bool Running = true;
    uintptr_t pStaticAddress = 0;

    int GetPing() {
        static uintptr_t base = (uintptr_t)GetModuleHandle(NULL);

        if (pStaticAddress == 0) {
            uintptr_t leaInstr = PatternScanner::FindPattern(base, 0x5000000, {
                0x48, 0x8D, 0x05, -1, -1, -1, -1,
                0x48, 0x81, 0xC1, 0xC8, 0x00, 0x00, 0x00
                });
            if (leaInstr) {
                int32_t ripOff = 0;
                if (RawSafeReadAPI(&ripOff, leaInstr + 3, 4)) {
                    pStaticAddress = leaInstr + 7 + ripOff;
                    Logger::Add("[SYS] Ping source locked at base + 0x%llX", pStaticAddress - base);
                }
            }
        }

        if (pStaticAddress != 0) {
            int oneWayPing = SafeReadIntAPI(pStaticAddress + 0x1B8);
            if (oneWayPing > 0 && oneWayPing < 500) return oneWayPing * 2;
            int altPing = SafeReadIntAPI(pStaticAddress + 0x110);
            if (altPing > 5 && altPing < 500) return altPing;
        }

        return 0;
    }

    void StatsFetcherLoop() {
        HINTERNET hSession = WinHttpOpen(L"LeagueOne_Agent", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) return;
        HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", 2999, 0);
        if (!hConnect) { WinHttpCloseHandle(hSession); return; }

        auto ExtractFloat = [](const std::string& json, const std::string& key) -> float {
            size_t keyPos = json.find("\"" + key + "\"");
            if (keyPos == std::string::npos) return -1.0f;
            size_t valStart = json.find(":", keyPos);
            if (valStart == std::string::npos) return -1.0f;
            valStart++;

            while (valStart < json.length() && (json[valStart] == ' ' || json[valStart] == '\t')) valStart++;

            size_t valEnd = valStart;
            while (valEnd < json.length() && (json[valEnd] >= '0' && json[valEnd] <= '9' || json[valEnd] == '.' || json[valEnd] == '-')) {
                valEnd++;
            }
            if (valEnd > valStart) {
                try { return std::stof(json.substr(valStart, valEnd - valStart)); }
                catch (...) { return -1.0f; }
            }
            return -1.0f;
        };

        while (Running) {
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/liveclientdata/activeplayer", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
            if (hRequest) {
                DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
                WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));
                if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                    if (WinHttpReceiveResponse(hRequest, NULL)) {
                        DWORD dwSize = 0, dwDownloaded = 0;
                        std::string response = "";
                        do {
                            dwSize = 0;
                            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                            if (dwSize == 0) break;
                            std::vector<char> buffer(dwSize + 1);
                            if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                                buffer[dwDownloaded] = 0;
                                response.append(buffer.data());
                            }
                        } while (dwSize > 0);

                        if (response.length() > 20) {
                            float as = ExtractFloat(response, "attackSpeed");
                            if (as > 0) CurrentAttackSpeed = as;

                            float ar = ExtractFloat(response, "attackRange");
                            if (ar > 0) CurrentRange = ar;

                            float ad = ExtractFloat(response, "attackDamage");
                            if (ad >= 0) CurrentAttackDamage = ad;

                            float ap = ExtractFloat(response, "abilityPower");
                            if (ap >= 0) CurrentAbilityPower = ap;

                            float armor = ExtractFloat(response, "armor");
                            if (armor >= 0) CurrentArmor = armor;

                            float mr = ExtractFloat(response, "magicResist");
                            if (mr >= 0) CurrentMR = mr;

                            float arPen = ExtractFloat(response, "armorPenetrationPercent");
                            if (arPen >= 0) CurrentArmorPenPercent = arPen;

                            float lethality = ExtractFloat(response, "armorPenetrationFlat");
                            if (lethality >= 0) CurrentLethality = lethality;

                            float mPenPercent = ExtractFloat(response, "magicPenetrationPercent");
                            if (mPenPercent >= 0) CurrentMagicPenPercent = mPenPercent;

                            float mPenFlat = ExtractFloat(response, "magicPenetrationFlat");
                            if (mPenFlat >= 0) CurrentMagicPenFlat = mPenFlat;
                        }
                    }
                }
                WinHttpCloseHandle(hRequest);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
    }

    void Start() {
        Running = true;
        std::thread(StatsFetcherLoop).detach();
    }
}