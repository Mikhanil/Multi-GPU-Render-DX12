#include "ReflectionApp.h"

#include "Window.h"

using namespace PEPEngine;
using namespace Graphics;
using namespace DirectX::SimpleMath;

HybridCubeMapApp::HybridCubeMapApp(const HINSTANCE hInstance) : D3DApp(hInstance) {}

HybridCubeMapApp::~HybridCubeMapApp()
{
    Flush();
    for (auto& device : devices)
    {
        if (device)
        {
            device->ResetAllocators(frameCount);
            device->TerminatedQueuesWorker();
        }
    }
}

void HybridCubeMapApp::InitDevices()
{
    devices.resize(GraphicAdapterCount);
    const auto allDevices = GDeviceFactory::GetAllDevices(true);
    const auto firstDevice = allDevices[0];
    const auto otherDevice = allDevices[1];
    if (firstDevice->GetName().find(L"NVIDIA") == std::wstring::npos &&
        otherDevice->GetName().find(L"NVIDIA") != std::wstring::npos)
    {
        devices[GraphicAdapterPrimary] = otherDevice;
        devices[GraphicAdapterSecond] = firstDevice;
    }
    else
    {
        devices[GraphicAdapterPrimary] = firstDevice;
        devices[GraphicAdapterSecond] = otherDevice;
    }
    scenes.resize(GraphicAdapterCount);
}

bool HybridCubeMapApp::Initialize()
{
    InitDevices();
    InitMainWindow();
    constexpr Vector3 mainLightDirection(0.57735f, -0.57735f, 0.57735f);
    for (UINT i = 0; i < GraphicAdapterCount; ++i)
    {
        scenes[i] = std::make_unique<Common::Scene>(devices[i]);
        scenes[i]->Initialize(AspectRatio(), mainLightDirection);
    }
    camera = scenes[GraphicAdapterPrimary]->GetCamera();
    renderer = std::make_unique<ReflectionRenderer>(MainWindow, devices, scenes, BackBufferFormat, DepthStencilFormat);
    renderer->Initialize();
    OnResize();
    return true;
}

void HybridCubeMapApp::Update(const GameTimer& gt)
{
    if (renderer) renderer->Update(gt);
}

void HybridCubeMapApp::Draw(const GameTimer& gt)
{
    if (renderer) renderer->Draw(gt);
}

void HybridCubeMapApp::OnResize()
{
    D3DApp::OnResize();
    if (renderer) renderer->OnResize(AspectRatio());
}

bool HybridCubeMapApp::InitMainWindow()
{
    MainWindow = CreateRenderWindow(devices[GraphicAdapterPrimary], mainWindowCaption, 1920, 1080, false);
    return true;
}

void HybridCubeMapApp::CalculateFrameStats()
{
    if (!renderer) return;
    for (UINT i = 0; i < GraphicAdapterCount; ++i)
        gpuTimes[i] = renderer->GetGpuTime(static_cast<GraphicsAdapter>(i));
}

void HybridCubeMapApp::Flush()
{
    for (auto& device : devices)
        if (device) device->Flush();
}

LRESULT HybridCubeMapApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
    return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}
