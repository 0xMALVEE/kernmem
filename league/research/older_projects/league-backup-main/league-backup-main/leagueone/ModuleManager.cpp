#include "ModuleManager.h"
#include "GameReader.h"
#include "Logger.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>

namespace ModuleManager {
    std::vector<IChampion*> AvailableModules;
    IChampion* ActiveModule = nullptr;
    bool hasLoggedMissing = false;

    std::string ToLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    void RegisterModule(IChampion* championModule) {
        AvailableModules.push_back(championModule);
    }

    void Initialize() {
        if (ActiveModule != nullptr) return;

        uintptr_t localAddr = GameReader::GetLocalPlayer();
        if (!localAddr) return;

        GameObject* localPlayer = (GameObject*)localAddr;

        // USE THE NEW FUNCTION HERE
        std::string myChampName = localPlayer->GetChampionName();

        if (myChampName.empty()) return;

        std::string champLower = ToLower(myChampName);

        for (auto* mod : AvailableModules) {
            if (ToLower(mod->GetName()) == champLower) {
                ActiveModule = mod;
                ActiveModule->Initialize();
                Logger::Add("[MODULE] Loaded script for %s", myChampName.c_str());
                return;
            }
        }

        if (!hasLoggedMissing) {
            Logger::Add("[MODULE] No script available for %s", myChampName.c_str());
            hasLoggedMissing = true;
        }
    }

    void OnTick() {
        if (!ActiveModule) Initialize();
        if (ActiveModule) ActiveModule->OnTick();
    }

    void OnDraw() {
        if (ActiveModule) ActiveModule->OnDraw();
    }

    void OnMenu() {
        if (ActiveModule) {
            ActiveModule->OnMenu();
        }
        else {
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "No script loaded for your current champion.");
        }
    }
}