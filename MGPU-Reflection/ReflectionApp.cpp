#include "ReflectionApp.h"

#include "Window.h"
#include "States/ReflectionBenchmarkState.h"
#include "Utils.h"
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
    HybridCubeMapApp::Flush();
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
    const auto& allDevices = GDeviceFactory::GetAllDevices(false);
    if (allDevices.empty())
        throw std::runtime_error("MGPU-Reflection requires a hardware Direct3D 12 adapter.");

    const auto& firstDevice = allDevices[0];
    devices.push_back(firstDevice);
    if (allDevices.size() > 1)
    {
        const auto& otherDevice = allDevices[1];
        if (firstDevice->GetName().find(L"NVIDIA") == std::wstring::npos &&
            otherDevice->GetName().find(L"NVIDIA") != std::wstring::npos)
        {
            devices[GraphicAdapterPrimary] = otherDevice;
            devices.push_back(firstDevice);
        }
        else
        {
            devices.push_back(otherDevice);
        }
    }
    scenes.resize(devices.size());
}

bool HybridCubeMapApp::Initialize()
{
    InitDevices();
    InitMainWindow();
    constexpr Vector3 mainLightDirection(0.57735f, -0.57735f, 0.57735f);
    for (UINT i = 0; i < scenes.size(); ++i)
    {
        scenes[i] = std::make_unique<Common::Scene>(devices[i]);
        scenes[i]->Initialize(AspectRatio(), mainLightDirection);
    }
    camera = scenes[GraphicAdapterPrimary]->GetCamera();
    renderer = std::make_unique<ReflectionRenderer>(MainWindow, devices, scenes, BackBufferFormat, DepthStencilFormat);
    renderer->Initialize();
    OnResize();

#if !defined(DEBUG) && !defined(_DEBUG)
    // Every benchmark axis is explicit in both the state title and log name:
    // capture strategy, update cadence, and primary/secondary probe split.
    constexpr uint32_t benchmarkDurationSeconds = 20;
    const auto logDirectory = std::filesystem::current_path() / L"BenchmarkLogs";
    std::error_code error;
    std::filesystem::create_directories(logDirectory, error);
    if (error)
    {
        OutputDebugStringW(L"MGPU-Reflection failed to create benchmark log directory.\n");
        MessageBoxW(nullptr, L"MGPU-Reflection failed to create benchmark log directory.",
                    L"MGPU-Reflection benchmark error", MB_OK | MB_ICONERROR);
        return false;
    }
    benchmark.SetLooping(false);
    constexpr std::array captureModes =
    {
        ReflectionProbeCaptureMode::BakedDynamicOverlay,
        ReflectionProbeCaptureMode::FullDynamic
    };
    constexpr std::array updateModes =
    {
        ReflectionProbeUpdateMode::AllProbesPerFrame,
        ReflectionProbeUpdateMode::OneProbePerFrame,
        ReflectionProbeUpdateMode::OneFacePerFrame
    };
    const uint32_t distributionCount = devices.size() == ReflectionAdapterCount
                                           ? Common::Scene::ReflectionProbeCount + 1
                                           : 1;
    const uint32_t totalStateCount = static_cast<uint32_t>(captureModes.size() * updateModes.size()) *
        distributionCount;
    uint32_t stateIndex = 0;
    for (const auto captureMode : captureModes)
    {
        const std::wstring captureName = captureMode == ReflectionProbeCaptureMode::FullDynamic
                                             ? L"Full dynamic"
                                             : L"Baked + dynamic overlay";
        const std::wstring captureLogName = captureMode == ReflectionProbeCaptureMode::FullDynamic
                                                ? L"FullDynamic"
                                                : L"BakedOverlay";
        for (const auto updateMode : updateModes)
        {
            std::wstring updateName;
            std::wstring updateLogName;
            switch (updateMode)
            {
            case ReflectionProbeUpdateMode::AllProbesPerFrame:
                updateName = L"All probes/frame";
                updateLogName = L"AllProbes";
                break;
            case ReflectionProbeUpdateMode::OneProbePerFrame:
                updateName = L"One probe/frame";
                updateLogName = L"OneProbe";
                break;
            case ReflectionProbeUpdateMode::OneFacePerFrame:
                updateName = L"One face/frame";
                updateLogName = L"OneFace";
                break;
            }

            for (UINT primaryProbeCount = 0;
                 primaryProbeCount <= Common::Scene::ReflectionProbeCount;
                 ++primaryProbeCount)
            {
                if (devices.size() < ReflectionAdapterCount &&
                    primaryProbeCount != Common::Scene::ReflectionProbeCount)
                {
                    continue;
                }

                const UINT secondaryProbeCount = Common::Scene::ReflectionProbeCount - primaryProbeCount;
                const std::wstring distributionName = L"Primary " + std::to_wstring(primaryProbeCount) +
                    L" / Secondary " + std::to_wstring(secondaryProbeCount);
                const std::wstring stateName = captureName + L" | " + updateName + L" | " + distributionName;
                const std::wstring logPrefix = L"MGPU-Reflection_" + captureLogName + L"_" + updateLogName +
                    L"_Primary" + std::to_wstring(primaryProbeCount) + L"-Secondary" +
                    std::to_wstring(secondaryProbeCount) + L" ";
                const auto logPath = devices.size() == ReflectionAdapterCount
                                         ? Benchmark::GetLogFile(logPrefix, *devices[GraphicAdapterPrimary],
                                                                 *devices[GraphicAdapterSecond])
                                         : logDirectory / (logPrefix + devices[GraphicAdapterPrimary]->GetName() +
                                                           L"+SingleGPU.log");
                ++stateIndex;
                benchmark.AddState<ReflectionBenchmarkState>(
                    *this, ReflectionProbeConfiguration{primaryProbeCount, captureMode, updateMode}, stateName,
                    stateIndex, totalStateCount, benchmarkDurationSeconds, FileQueueWriter(logPath));
            }
        }
    }
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
#if defined(DEBUG) || defined(_DEBUG)
    D3DApp::CalculateFrameStats();
#endif
}

void HybridCubeMapApp::SetReflectionBenchmarkConfiguration(const ReflectionProbeConfiguration configuration)
{
    Flush();
    for (const auto& scene : scenes)
    {
        scene->ResetBenchmarkAnimation();
    }
    renderer->ResetBenchmarkAnimation();
    renderer->SetReflectionProbeConfiguration(configuration);
}

void HybridCubeMapApp::UpdateReflectionBenchmarkStatus(const std::wstring& stateName,
                                                       const uint32_t currentState,
                                                       const uint32_t totalStates,
                                                       const uint32_t remainingSeconds,
                                                       const TimeStats* latestStats,
                                                       const bool isSettling)
{
    const std::wstring primaryGpuName = devices[GraphicAdapterPrimary]->GetName();
    const std::wstring secondaryGpuName = devices.size() == ReflectionAdapterCount
                                              ? devices[GraphicAdapterSecond]->GetName()
                                              : L"";
    const std::wstring gpuNames = secondaryGpuName.empty()
                                      ? primaryGpuName
                                      : primaryGpuName + L" + " + secondaryGpuName;
    const std::wstring fps = latestStats ? std::format(L"{:.2f}", latestStats->fps) : L"--";
    MainWindow->SetWindowTitle(
        stateName + L" | GPUs: " + gpuNames +
        L" | Remaining: " + std::to_wstring(remainingSeconds) + L" s" +
        L" | State: " + std::to_wstring(currentState) + L"/" + std::to_wstring(totalStates) +
        L" | FPS: " + fps);

    ReflectionBenchmarkDisplayState displayState{};
    displayState.IsSettling = isSettling;
    displayState.HasStats = latestStats != nullptr;
    displayState.CurrentState = currentState;
    displayState.TotalStates = totalStates;
    displayState.RemainingSeconds = remainingSeconds;
    displayState.PrimaryGpuName = primaryGpuName;
    displayState.SecondaryGpuName = secondaryGpuName;
    if (latestStats)
    {
        displayState.Fps = latestStats->fps;
        displayState.Mspf = latestStats->mspf;
        displayState.MinFps = latestStats->minFps;
        displayState.MinMspf = latestStats->minMspf;
        displayState.MaxFps = latestStats->maxFps;
        displayState.MaxMspf = latestStats->maxMspf;
    }
    renderer->SetBenchmarkDisplayState(std::move(displayState));
}

void HybridCubeMapApp::Flush()
{
    for (auto& device : devices)
        if (device) device->Flush();
}

LRESULT HybridCubeMapApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
    if (renderer)
    {
        renderer->ForwardUiMessage(hwnd, msg, wParam, lParam);
        if (renderer->UiWantsMouseCapture())
        {
            switch (msg)
            {
            case WM_INPUT:
                return DefWindowProc(hwnd, msg, wParam, lParam);
            case WM_MOUSEMOVE:
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_MOUSEWHEEL:
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
            }
        }
        if (renderer->UiWantsKeyboardCapture())
        {
            if (msg == WM_KEYUP)
            {
                keyboard.OnKeyReleased(static_cast<char>(wParam));
                return 0;
            }
            if (msg == WM_KEYDOWN)
                return 0;
        }
    }
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
