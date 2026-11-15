#include "Renderer.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "Settings.h"
#include "Logger.h"
#include "GameReader.h"
#include "Overlays.h"
#include "Utils.h"
#include "Debugger.h"
#include "ConfigLoader.h"
#include "ConfigManager.h"
#include "GameAPI.h"
#include "ModuleManager.h"
#include "Input.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace Renderer {
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    ID3D11RenderTargetView* mainRenderTargetView = nullptr;
    WNDPROC oWndProc = nullptr;
    HWND window = nullptr;
    bool init = false;

    const char* GetKeyName(int key) {
        static char name[32];
        if (key == 0) return "None";
        unsigned int scanCode = MapVirtualKey(key, MAPVK_VK_TO_VSC);
        switch (key) {
        case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN:
        case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME:
        case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
            scanCode |= 0x100;
        }
        if (GetKeyNameTextA(scanCode << 16, name, sizeof(name)) == 0) return "Unknown";
        return name;
    }

    LRESULT __stdcall WndProc(const HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam)) return true;

        // ---> THE VISUAL BLOCKER <---
        // If we are micro-flicking, we swallow the mouse updates so the in-game cursor doesn't jump visually!
        if (Input::g_IsFlicking) {
            if (uMsg == WM_MOUSEMOVE || uMsg == WM_SETCURSOR || uMsg == WM_NCHITTEST) {
                return TRUE; // Swallow the message so the game UI ignores it
            }
        }

        if (Settings::ShowMenu && (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP || uMsg == WM_MOUSEMOVE)) return true;
        return CallWindowProc(oWndProc, hWnd, uMsg, wParam, lParam);
    }

    bool Initialize(IDXGISwapChain* pSwapChain) {
        if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pDevice))) return false;
        pDevice->GetImmediateContext(&pContext);
        DXGI_SWAP_CHAIN_DESC sd;
        pSwapChain->GetDesc(&sd);
        window = sd.OutputWindow;
        RECT rect;
        GetClientRect(window, &rect);
        Utils::Initialize(rect.right - rect.left, rect.bottom - rect.top);
        ID3D11Texture2D* pBackBuffer;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
        pDevice->CreateRenderTargetView(pBackBuffer, NULL, &mainRenderTargetView);
        pBackBuffer->Release();
        oWndProc = (WNDPROC)SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)WndProc);
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        ImGui_ImplWin32_Init(window);
        ImGui_ImplDX11_Init(pDevice, pContext);
        init = true;
        return true;
    }

    void Cleanup() {
        if (window && oWndProc) SetWindowLongPtr(window, GWLP_WNDPROC, (LONG_PTR)oWndProc);
    }

    void Draw() {
        if (!init) return;

        static bool lastShift = false;
        bool currentShift = GetAsyncKeyState(VK_LSHIFT) & 0x8000;
        if (currentShift && !lastShift) Settings::ShowMenu = !Settings::ShowMenu;
        lastShift = currentShift;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::GetIO().MouseDrawCursor = Settings::ShowMenu;

        if (Settings::ShowMenu) {
            ImGui::SetNextWindowSize(ImVec2(500, 500), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("LeagueOne Internal", &Settings::ShowMenu)) {
                if (ImGui::BeginTabBar("Tabs")) {

                    // TAB: Visuals
                    if (ImGui::BeginTabItem("Visuals")) {
                        ImGui::Text("Camera Settings");
                        if (ImGui::Checkbox("Enable Zoom Hack", &Settings::ZoomHackEnabled)) {
                            if (!Settings::ZoomHackEnabled) Settings::MaxZoomValue = 2250.0f;
                            ConfigManager::SaveSettings();
                        }
                        if (Settings::ZoomHackEnabled) {
                            if (ImGui::SliderFloat("Max Zoom Distance", &Settings::MaxZoomValue, 2250.0f, 6000.0f))
                                ConfigManager::SaveSettings();
                        }
                        ImGui::Separator();
                        ImGui::Text("Overlays");
                        if (ImGui::Checkbox("Draw Attack Range", &Settings::DrawAttackRange)) ConfigManager::SaveSettings();
                        if (ImGui::SliderFloat("Range Thickness", &Settings::RangeThickness, 1.0f, 5.0f)) ConfigManager::SaveSettings();
                        if (ImGui::ColorEdit4("Range Color", Settings::RangeColor)) ConfigManager::SaveSettings();

                        ImGui::Separator();
                        if (ImGui::Checkbox("Draw Missiles (Skillshots)", &Settings::DrawMissiles)) ConfigManager::SaveSettings();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Champion")) {
                        ModuleManager::OnMenu(); // Renders the Ezreal checkbox dynamically
                        ImGui::EndTabItem();
                    }

                    // TAB: Orbwalker
                    if (ImGui::BeginTabItem("Orbwalker")) {
                        if (ImGui::Checkbox("Master Switch", &Settings::OrbwalkerEnabled)) ConfigManager::SaveSettings();
                        if (ImGui::Checkbox("Attack Units", &Settings::AttackUnit)) ConfigManager::SaveSettings();
                        if (ImGui::Checkbox("Move to Mouse", &Settings::MoveUnit)) ConfigManager::SaveSettings();

                        ImGui::Separator();
                        ImGui::Text("Timings");
                        ImGui::SliderFloat("Windup %", &Settings::WindupPercent, 0.0f, 0.5f);
                        ImGui::SliderFloat("Base Buffer (s)", &Settings::BaseBuffer, 0.0f, 0.1f);
                        ImGui::SliderFloat("Move Delay", &Settings::MoveDelay, 0.030f, 0.150f);

                        // --- NEW FLICK DELAY MENU ---
                        ImGui::Separator();
                        ImGui::Text("Micro-Flick Tuning (Milliseconds)");
                        if (ImGui::SliderFloat("Pre-Delay", &Settings::FlickPreDelay, 0.5f, 15.0f, "%.1f ms"))
                            ConfigManager::SaveSettings();

                        if (ImGui::SliderFloat("Press-Delay", &Settings::FlickPressDelay, 0.5f, 25.0f, "%.1f ms"))
                            ConfigManager::SaveSettings();

                        if (ImGui::SliderFloat("Post-Delay", &Settings::FlickPostDelay, 0.5f, 15.0f, "%.1f ms"))
                            ConfigManager::SaveSettings();

                        ImGui::TextColored(ImVec4(0.5, 0.5, 0.5, 1), "Increase Press-Delay if attacks are cancelling.");

                        ImGui::Separator();
                        ImGui::TextColored(ImVec4(0, 1, 1, 1), "Live Stats");
                        ImGui::Text("Ping: %d ms", Debug::CurrentPing);
                        ImGui::Text("Buffer: %.3f s", Debug::CurrentBuffer);
                        ImGui::EndTabItem();
                    }

                    // TAB: Targeting
                    if (ImGui::BeginTabItem("Targeting")) {
                        ImGui::Text("Target Selection Logic");
                        const char* modes[] = { "Closest to Mouse", "Lowest HP", "Priority" };
                        if (ImGui::Combo("Targeting Mode", &Settings::TargetMode, modes, IM_ARRAYSIZE(modes))) {
                            ConfigManager::SaveSettings();
                        }

                        if (Settings::TargetMode == 2) {
                            ImGui::Separator();
                            ImGui::Text("Champion Priorities (1 = Highest, 5 = Lowest)");
                            ImGui::Spacing();

                            auto& enemies = GameReader::GetEnemies();
                            if (enemies.empty()) {
                                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Waiting for enemies to spawn...");
                            }
                            else {
                                for (auto* enemy : enemies) {
                                    if (!IsValidPtr(enemy)) continue;
                                    std::string name = enemy->GetChampionName();
                                    if (name.empty()) continue;

                                    // Populate default value dynamically
                                    if (Settings::PriorityMap.find(name) == Settings::PriorityMap.end()) {
                                        Settings::PriorityMap[name] = 5;
                                    }

                                    int prio = Settings::PriorityMap[name];
                                    ImGui::PushID(name.c_str());
                                    ImGui::Text("%-15s", name.c_str());
                                    ImGui::SameLine(150);
                                    ImGui::SetNextItemWidth(150);
                                    if (ImGui::SliderInt("##prio", &prio, 1, 5)) {
                                        Settings::PriorityMap[name] = prio;
                                        ConfigManager::SaveSettings();
                                    }
                                    ImGui::PopID();
                                }
                            }
                        }
                        ImGui::EndTabItem();
                    }

                    // TAB: Keybinds
                    if (ImGui::BeginTabItem("Keybinds")) {
                        ImGui::Text("Key Configuration");
                        static bool waitCombo = false;
                        ImGui::Text("Combo: "); ImGui::SameLine();
                        if (waitCombo) {
                            ImGui::Button("Press key...");
                            for (int i = 3; i < 255; i++) if (GetAsyncKeyState(i) & 0x8000) { Settings::ComboKey = i; waitCombo = false; ConfigManager::SaveSettings(); }
                        }
                        else if (ImGui::Button(GetKeyName(Settings::ComboKey))) waitCombo = true;

                        ImGui::Checkbox("Use TCO", &Settings::UseTCO);
                        if (ImGui::Button("Re-Sync from League Config")) ConfigLoader::LoadLeagueSettings();
                        ImGui::EndTabItem();
                    }

                    // TAB: Debug
                    if (ImGui::BeginTabItem("Debug")) {

                        ImGui::TextColored(ImVec4(1, 1, 0, 1), ">>> THE ULTIMATE CASTSPELL DIAGNOSTIC <<<");
                        if (ImGui::Button("Deep Dump CastSpell Info", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
                            Debugger::DeepDumpCastSpellInfo();
                        }
                        ImGui::Separator();

                        ImGui::TextColored(ImVec4(0, 1, 1, 1), ">>> SMART STAT SCANNER <<<");
                        ImGui::Text("Matches Local HTTP API stats with Memory space.");
                        if (ImGui::Button("Scan For Armor/MR/AD Offsets", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
                            Debugger::FindStatOffsets();
                        }
                        ImGui::Separator();

                        if (ImGui::Button("Log Active Skillshot Coords")) {
                            Debugger::LogActiveMissiles();
                        }
                        ImGui::Text("Click this while Q is in the air to verify Offset 0xF8 is updating.");
                        if (ImGui::Button("Scan for CastSpell")) {
                            Debugger::FindCastSpell();
                        }
                        if (ImGui::Button("Dump HudInstance Pointers")) {
                            Debugger::DumpHudInstance();
                        }
                        if (ImGui::Button("Scan For AIManager", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
                            Debugger::FindAIManager();
                        }
                        if (ImGui::Button("Scan For Waypoints", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
                            Debugger::FindWaypoints();
                        }
                        if (ImGui::Button("Run Behavioral Scanner", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
                            Debugger::RunBehavioralScanner();
                        }

                        if (ImGui::Button("Find HP Bar Offset (Look at Enemy)", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
                            Debugger::FindHpBarOffset();
                        }

                        if (ImGui::Button("Find HP Bar (Pan Camera Method)", ImVec2(ImGui::GetContentRegionAvail().x, 30))) {
                            Debugger::FindHpBarDelta();
                        }

                        ImGui::EndTabItem();
                    }

                    // ---> NEW TAB: SPELL INSPECTOR <---
                    if (ImGui::BeginTabItem("Spell Inspector")) {
                        uintptr_t localAddr = GameReader::GetLocalPlayer();
                        if (localAddr) {
                            GameObject* local = (GameObject*)localAddr;

                            ImGui::Text("Spell Level Finder");
                            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Do not level up Q yet!");
                            ImGui::Text("Look for an offset that is '0', then level up Q and see which one changes to '1'.");
                            ImGui::Spacing();

                            SpellSlot qSlot = local->GetSpellSlot(0);

                            if (IsValidPtr((void*)qSlot.address)) {
                                ImGui::BeginChild("IntViewer", ImVec2(0, 0), true);
                                // Scan offsets from 0x10 to 0x50 for integers
                                for (int i = 0x10; i <= 0x50; i += 4) {
                                    int val = SafeReadSDK<int>(qSlot.address + i, -1);

                                    // Highlight typical spell levels (0 to 5)
                                    if (val >= 0 && val <= 5) {
                                        ImGui::TextColored(ImVec4(0, 1, 0, 1), "+0x%X : %d  <-- POTENTIAL LEVEL", i, val);
                                    }
                                    else {
                                        ImGui::Text("+0x%X : %d", i, val);
                                    }
                                }
                                ImGui::EndChild();
                            }
                        }
                        else {
                            ImGui::TextColored(ImVec4(1, 0, 0, 1), "Waiting for player instance...");
                        }
                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::End();
            }
            static bool sLogs = true;
            Logger::Draw(&sLogs);
        }

        uintptr_t localAddr = GameReader::GetLocalPlayer();
        if (localAddr) {
            Overlays::DrawAttackRange((GameObject*)localAddr, GameAPI::CurrentRange);
            Overlays::DrawMissiles();

            Overlays::DrawPredictionPaths(GameReader::GetEnemies());

            ModuleManager::OnDraw();

        }

        ImGui::EndFrame();
        ImGui::Render();
        pContext->OMSetRenderTargets(1, &mainRenderTargetView, NULL);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
}