#pragma once

#include "d3dApp.h"
#include "FrameResource.h"
#include "ReflectionRenderer.h"
#include "Scene.h"
#include <memory>
#include <vector>
#include "GameTimer.h"

namespace Common
{
    class ReflectionApp : public D3DApp
    {
    public:
        explicit ReflectionApp(HINSTANCE hInstance);
        ReflectionApp(const ReflectionApp& rhs) = delete;
        ReflectionApp& operator=(const ReflectionApp& rhs) = delete;
        ~ReflectionApp() override;

        bool Initialize() override;

#if defined(DEBUG) || defined(_DEBUG)
        UINT pathMapShow = 0;
        const UINT maxPathMap = 2;
#endif
        UINT ssaaMultiplier = 1;
        const UINT maxSsaaMultiplier = 4;
        MultiGpuRenderConfig renderConfig = MultiGpuRenderConfig::SingleGpu;
        bool isUsingMgpuSsr = false;
        bool isUsingDynamicReflectionProbes = true; // false: capture once, true: update every frame.

    private:
        LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;
        void OnResize() override;
        void Update(const GameTimer& gt) override;
        void Draw(const GameTimer& gt) override;
        void BuildFrameResources();

        UINT backBufferIndex = 0;
        DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D32_FLOAT;

        std::vector<std::unique_ptr<FrameResource>> frameResources;
        FrameResource* currentFrameResource = nullptr;
        int currentFrameResourceIndex = 0;

        std::unique_ptr<Scene> scene;
        std::unique_ptr<ReflectionRenderer> renderer;

        DirectX::SimpleMath::Vector3 mainLightDirection = DirectX::SimpleMath::Vector3(0.457f, -0.457f, -0.762f);
    };
}
