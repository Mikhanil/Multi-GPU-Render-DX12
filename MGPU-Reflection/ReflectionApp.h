#pragma once

#include "d3dApp.h"
#include "GDeviceFactory.h"
#include "ReflectionRenderer.h"
#include "Scene.h"
#include "GameObject.h"
#include "Services/BenchmarkService.h"

class HybridCubeMapApp : public Common::D3DApp
{
public:
    explicit HybridCubeMapApp(HINSTANCE hInstance);
    ~HybridCubeMapApp() override;
    bool Initialize() override;
    // Called only by Release benchmark states. Scene owns transform reset;
    // renderer receives the capture/update cadence and probe distribution.
    void SetReflectionBenchmarkConfiguration(ReflectionProbeConfiguration configuration);
    void SetReflectionBenchmarkTitle(const std::wstring& stateName, uint32_t remainingSeconds,
                                     float averageFps, bool isSettling);

protected:
    void Update(const GameTimer& gt) override;
    void Draw(const GameTimer& gt) override;
    void OnResize() override;
    bool InitMainWindow() override;
    void CalculateFrameStats() override;

private:
    void InitDevices();
    void Flush() override;
    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

    std::vector<std::shared_ptr<GDevice>> devices;
    std::vector<std::unique_ptr<Common::Scene>> scenes;
    std::unique_ptr<ReflectionRenderer> renderer;
    BenchmarkService benchmark;
};

using ReflectionApp = HybridCubeMapApp;
