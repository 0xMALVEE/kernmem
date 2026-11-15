#pragma once
#include <d3d11.h>
#include <Windows.h>

namespace Renderer {
    bool Initialize(IDXGISwapChain* pSwapChain);
    void Draw();
    void Cleanup();

    extern ID3D11Device* pDevice;
    extern ID3D11DeviceContext* pContext;
    extern ID3D11RenderTargetView* mainRenderTargetView;
    extern WNDPROC oWndProc;
    extern HWND window;
}