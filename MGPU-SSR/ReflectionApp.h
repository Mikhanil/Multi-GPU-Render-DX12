#pragma once

#include "d3dApp.h"
#include "FrameResource.h"
#include "ReflectionRenderer.h"
#include "Scene.h"
#include "Services/BenchmarkService.h"
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
        void SetReflectionBenchmarkConfiguration(UINT gpuPairContextIndex, bool useSecondGpuForSsr,
                                                 UINT primaryProbeCount,
                                                 bool dynamicReflectionProbes, bool updateOneProbeFacePerFrame,
                                                 UINT ssaaMultiplier);
        void OnReflectionBenchmarkStateCompleted(bool isSsaaExpansionProbe, bool endsSsaaLevel,
                                                 float averageFps);
        void SetReflectionBenchmarkTitle(const std::wstring& stateName, uint32_t remainingSeconds,
                                         float averageFps);

#if defined(DEBUG) || defined(_DEBUG)
        UINT pathMapShow = 0;
        // 0: final image, 1: SSR debug, 2: shadow map.
        const UINT maxPathMap = 3;
#endif
        UINT ssaaMultiplier = 1;
        const UINT maxSsaaMultiplier = 6;
        bool isUsingSecondGpuForSsr = false;
        // Debug starts with the reproducible baseline: SSR and a baked cubemap
        // both run on the primary GPU. Release benchmark states override this.
        bool isUsingSecondGpuForReflectionProbes = false;
        bool isUsingDynamicReflectionProbes = false; // false: capture once, true: update every frame.
        bool isUpdatingOneProbeFacePerFrame = false;

    private:
        struct GpuPairContext
        {
            UINT primaryAdapterIndex = 0;
            UINT secondaryAdapterIndex = 0;
            std::shared_ptr<GDevice> primaryDevice;
            std::shared_ptr<GDevice> secondaryDevice;
            std::unique_ptr<Scene> scene;
            std::unique_ptr<ReflectionRenderer> renderer;
            std::vector<std::unique_ptr<FrameResource>> frameResources;
            int currentFrameResourceIndex = 0;
        };

        LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;
        void CalculateFrameStats() override;
        void OnResize() override;
        void Update(const GameTimer& gt) override;
        void Draw(const GameTimer& gt) override;
        void InitializeGpuPairContexts();
        void ActivateGpuPairContext(UINT contextIndex);
        void BuildFrameResources(GpuPairContext& context);
        void AddSsaaBenchmarkLevel(UINT ssaaLevel);

        UINT backBufferIndex = 0;
        DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D32_FLOAT;

        std::vector<std::unique_ptr<GpuPairContext>> gpuPairContexts;
        GpuPairContext* activeGpuPairContext = nullptr;
        FrameResource* currentFrameResource = nullptr;

        Scene* scene = nullptr;
        ReflectionRenderer* renderer = nullptr;
        BenchmarkService benchmark;
        float expansionProbeFps = 0.0f;
        bool nextSsaaLevelQueued = false;
        bool benchmarkFinished = false;

        DirectX::SimpleMath::Vector3 mainLightDirection = DirectX::SimpleMath::Vector3(0.457f, -0.457f, -0.762f);
    };
}
