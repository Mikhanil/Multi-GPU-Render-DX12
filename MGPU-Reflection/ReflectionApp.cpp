#include "ReflectionApp.h"

#include "Window.h"
#include "States/ReflectionBenchmarkState.h"
#include <cstdlib>
#include <filesystem>
#include <format>

using namespace PEPEngine;
using namespace Graphics;
using namespace DirectX::SimpleMath;

HybridCubeMapApp::HybridCubeMapApp(const HINSTANCE hInstance) : D3DApp(hInstance)
{
    mainWindowCaption = L"MGPU-Reflection";
}

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

#if !defined(DEBUG) && !defined(_DEBUG)
    // Keep the two existing rendering modes as separate, repeatable samples:
    // primary-only is the baseline and the second state enables the MGPU path.
    constexpr uint32_t benchmarkDurationSeconds = 100;
    const auto logDirectory = std::filesystem::current_path() / L"BenchmarkLogs";
    std::error_code error;
    std::filesystem::create_directories(logDirectory, error);
    const auto primaryName = devices[GraphicAdapterPrimary]->GetName();
    const auto secondName = devices[GraphicAdapterSecond]->GetName();
    benchmark.SetLooping(false);
    benchmark.AddState<ReflectionBenchmarkState>(*this, true, L"Primary baseline",
        benchmarkDurationSeconds, FileQueueWriter(logDirectory / (L"MGPU-Reflection_Primary_" + primaryName + L".log")));
    benchmark.AddState<ReflectionBenchmarkState>(*this, false, L"MGPU reflection",
        benchmarkDurationSeconds, FileQueueWriter(logDirectory / (L"MGPU-Reflection_MGPU_" + primaryName + L"+" + secondName + L".log")));
    benchmark.Start();
#endif
    return true;
}

void HybridCubeMapApp::Update(const GameTimer& gt)
{
#if !defined(DEBUG) && !defined(_DEBUG)
    benchmark.Tick(gt.DeltaTime());
    // BenchmarkState::Exit writes the final log before BenchmarkService marks
    // the queue finished.  A process exit avoids an interactive Release window.
    if (benchmark.IsFinished())
    {
        std::exit(EXIT_SUCCESS);
    }
#endif
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
#if defined(DEBUG) || defined(_DEBUG)
    D3DApp::CalculateFrameStats();
#endif
}

void HybridCubeMapApp::SetReflectionBenchmarkConfiguration(const bool useOnlyPrime)
{
    Flush();
    for (const auto& scene : scenes)
    {
        scene->ResetBenchmarkAnimation();
    }
    renderer->SetUseOnlyPrime(useOnlyPrime);
    for (auto& gpuTime : gpuTimes)
    {
        gpuTime = 0;
    }
}

void HybridCubeMapApp::SetReflectionBenchmarkTitle(const std::wstring& stateName,
                                                    const uint32_t remainingSeconds,
                                                    const float averageFps,
                                                    const bool isSettling)
{
    MainWindow->SetWindowTitle(
        L"MGPU-Reflection | " + stateName +
        (isSettling ? L" | settling/prewarm" : L" | Remaining: " + std::to_wstring(remainingSeconds) +
         L" s | Average FPS: " + std::format(L"{:.2f}", averageFps)));
}

void HybridCubeMapApp::Flush()
{
    for (auto& device : devices)
        if (device) device->Flush();
}

LRESULT HybridCubeMapApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
    switch (msg)
    {
    case WM_KEYUP:
        keyboard.OnKeyReleased(static_cast<char>(wParam));
        return 0;
    case WM_INPUT:
        {
            UINT dataSize = 0;
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dataSize,
                            sizeof(RAWINPUTHEADER));
            if (dataSize > 0)
            {
                auto rawdata = std::make_unique<BYTE[]>(dataSize);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawdata.get(), &dataSize,
                                    sizeof(RAWINPUTHEADER)) == dataSize)
                {
                    const auto raw = reinterpret_cast<RAWINPUT*>(rawdata.get());
                    if (raw->header.dwType == RIM_TYPEMOUSE)
                    {
                        mouse.OnMouseMoveRaw(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
                    }
                }
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    case WM_MOUSEMOVE:
        mouse.OnMouseMove(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_LBUTTONDOWN:
        mouse.OnLeftPressed(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_RBUTTONDOWN:
        mouse.OnRightPressed(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MBUTTONDOWN:
        mouse.OnMiddlePressed(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_LBUTTONUP:
        mouse.OnLeftReleased(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_RBUTTONUP:
        mouse.OnRightReleased(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MBUTTONUP:
        mouse.OnMiddleReleased(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_MOUSEWHEEL:
        if (GET_WHEEL_DELTA_WPARAM(wParam) > 0)
            mouse.OnWheelUp(LOWORD(lParam), HIWORD(lParam));
        else if (GET_WHEEL_DELTA_WPARAM(wParam) < 0)
            mouse.OnWheelDown(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_KEYDOWN:
        {
            const auto keycode = static_cast<char>(wParam);
            const bool wasPressed = lParam & 0x40000000;
            if (keyboard.IsKeysAutoRepeat() || !wasPressed)
            {
                keyboard.OnKeyPressed(keycode);
            }
            return 0;
        }
    }

    return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}
