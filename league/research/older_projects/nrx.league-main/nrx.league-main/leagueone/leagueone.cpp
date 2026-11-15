#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <iostream>
#include "Visuals.h"
#include "GameReader.h"
#include "Renderer.h"
#include "Orbwalker.h"
#include "Utils.h"
#include "Logger.h"
#include "ConfigManager.h" // Include the new manager
#include "ConfigLoader.h"  // League Config (.json)
#include "Ezreal.h" // Include your new champion scripts here
#include "ModuleManager.h"
//#include "Detour.h" 
#include "Input.h"
#include "Prediction.h"

#pragma comment(lib, "d3d11.lib")

typedef void* (__fastcall* fnHudUpdate)(void* ecx, float delta);
fnHudUpdate oHudUpdate = nullptr;
uintptr_t* shadowVTable = nullptr;

// 2. THE HOOK: This runs on the GAME THREAD
void* __fastcall hkHudUpdate(void* ecx, float delta) {
    // This is now 100% safe. We are on the same thread as the game logic.

    // Call the original function
    return oHudUpdate(ecx, delta);
}

// --- TYPES ---
typedef HRESULT(__stdcall* D3D11PresentHook) (IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
D3D11PresentHook oPresent = nullptr;

// --- GLOBALS ---
bool g_Running = true;
bool g_Initialized = false;
bool g_HookedGameThread = false; // New flag

//extern void HookedCastSpell();

typedef bool(__fastcall* fnWorldToScreen)(Vector3* world, Vector2* screen);
fnWorldToScreen oWorldToScreen = nullptr;

// 2. The Hook Function (Runs on Game Thread!)
bool __fastcall hkWorldToScreen(Vector3* world, Vector2* screen) {
    // Every time the game calculates a screen position, we check our queue

    return oWorldToScreen(world, screen);
}

// --- PRESENT HOOK ---
HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    // 1. Initialize on first frame
    if (!g_Initialized) {
        if (Renderer::Initialize(pSwapChain)) {
            ConfigManager::LoadSettings();
            ConfigLoader::LoadLeagueSettings();
            Orbwalker::Initialize();

            ModuleManager::RegisterModule(new Ezreal());
            ModuleManager::Initialize();
            g_Initialized = true;
        }
    }

    // 2. Thread Pivot Initialization (Persistent Attempt)
    if (g_Initialized && !g_HookedGameThread) {
        static uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
        uintptr_t hudInstance = SafeReadSDK<uintptr_t>(base + Offset::oHudInstance, 0);

        if (hudInstance) {
            // Try 0x60 (Primary candidate), fallback to 0x18
            uintptr_t hudInputLogic = SafeReadSDK<uintptr_t>(hudInstance + 0x60, 0);
            if (!hudInputLogic) hudInputLogic = SafeReadSDK<uintptr_t>(hudInstance + 0x18, 0);

            if (IsValidPtr((void*)hudInputLogic)) {
                uintptr_t* originalVTable = *(uintptr_t**)hudInputLogic;

                // Copy the VTable (Using 100 entries to be safe)
                shadowVTable = new uintptr_t[100];
                memcpy(shadowVTable, originalVTable, 100 * sizeof(uintptr_t));

                // Swap Index 0 (Update/Render Loop)
                oHudUpdate = (fnHudUpdate)shadowVTable[0];
                shadowVTable[0] = (uintptr_t)hkHudUpdate;

                // Point the HUD Object to our modified VTable
                *(uintptr_t**)hudInputLogic = shadowVTable;

                Logger::Add("[SYS] Shadow VMT Hook SUCCESS on HUD object!");
                g_HookedGameThread = true;
            }
            else {
                Logger::Add("[SYS] Shadow VMT Hook FAILED on HUD object!");
            }
        }
    }

    // 2. Main Loop
    if (g_Running) {
        // A. Update Memory Readers
        static uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
        GameReader::Update(base);

        Visuals::Update();

        Prediction::UpdateTrackers(GameReader::GetEnemies());


        // B. Run Logic (Orbwalker)
        uintptr_t localAddr = GameReader::GetLocalPlayer();
        if (localAddr != 0) {
            Orbwalker::Update(
                (GameObject*)localAddr,
                GameReader::GetEnemies(),
                GameReader::GetMinions()
            );

            ModuleManager::OnTick();
        }

        // C. Draw UI and Overlays
        Renderer::Draw();
    }

    return oPresent(pSwapChain, SyncInterval, Flags);
}

// --- MAIN THREAD ---
DWORD WINAPI MainThread(LPVOID lpParam) {
    // Wait for Game Module
    uintptr_t base = 0;
    while (!(base = (uintptr_t)GetModuleHandle(NULL))) Sleep(100);

    // Create Dummy Device to find SwapChain VTable
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = GetForegroundWindow();
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* pDummySwapChain = nullptr;
    ID3D11Device* pDummyDevice = nullptr;
    ID3D11DeviceContext* pDummyContext = nullptr;

    if (SUCCEEDED(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, featureLevels, 2, D3D11_SDK_VERSION, &sd, &pDummySwapChain, &pDummyDevice, &featureLevel, &pDummyContext))) {
        // Get VTable
        void** pVTable = *reinterpret_cast<void***>(pDummySwapChain);

        // Hook Present (Index 8)
        oPresent = (D3D11PresentHook)pVTable[8];

        DWORD oldProtect;
        VirtualProtect(&pVTable[8], 8, PAGE_EXECUTE_READWRITE, &oldProtect);
        pVTable[8] = hkPresent;
        VirtualProtect(&pVTable[8], 8, oldProtect, &oldProtect);

        // Cleanup Dummy
        pDummyDevice->Release();
        pDummyContext->Release();
        pDummySwapChain->Release();
    }
    else {
        MessageBoxA(NULL, "Failed to create D3D Device", "Error", MB_ICONERROR);
        return 0;
    }

    // Keep thread alive until Unload
    while (g_Running) {
        if (GetAsyncKeyState(VK_DELETE) & 1) g_Running = false;
        Sleep(100);
    }

    // Restore WndProc before exiting
    Renderer::Cleanup();

    // Unhooking Present is risky without a Trampoline/Detour library (MinHook), 
    // so we usually just let the game close or accept the crash on eject for internal hacks 
    // unless using MinHook. For this simple snippet, we exit cleanly.

    FreeLibraryAndExitThread((HMODULE)lpParam, 0);
    return 0;
}

// --- DLL ENTRY ---
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr);
    }
    return TRUE;
}