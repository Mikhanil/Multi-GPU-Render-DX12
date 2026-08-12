#include "ReflectionApp.h"

#include "Camera.h"
#include "GCommandQueue.h"
#include "GDeviceFactory.h"
#include "Window.h"
#include "GameObject.h"
#include "States/ReflectionBenchmarkState.h"
#include <array>
#include <filesystem>

using namespace DirectX::SimpleMath;

namespace Common
{
    ReflectionApp::ReflectionApp(const HINSTANCE hInstance) : D3DApp(hInstance)
    {
        mainWindowCaption = L"MGPU-SSR";
    }

    ReflectionApp::~ReflectionApp()
    {
        Flush();
    }

    bool ReflectionApp::Initialize()
    {
        if (!D3DApp::Initialize())
            return false;

        auto commandQueue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Graphics);
        auto cmdList = commandQueue->GetCommandList();

        scene = std::make_unique<Scene>(GDeviceFactory::GetDevice());
        scene->Initialize(cmdList, AspectRatio(), mainLightDirection);
        camera = scene->GetCamera();

        renderer = std::make_unique<ReflectionRenderer>(MainWindow, *scene, camera, backBufferFormat,
                                                        depthStencilFormat, isM4xMsaa, m4xMsaaQuality);
        renderer->SetUseSecondGpuForSsr(isUsingSecondGpuForSsr);
        renderer->SetUseSecondGpuForReflectionProbes(isUsingSecondGpuForReflectionProbes);
        renderer->SetUseDynamicReflectionProbes(isUsingDynamicReflectionProbes);
        renderer->SetUpdateOneProbeFacePerFrame(isUpdatingOneProbeFacePerFrame);
        renderer->Initialize(cmdList);
#if defined(DEBUG) || defined(_DEBUG)
        renderer->SetDebugMap(pathMapShow);
#endif
        renderer->OnResize();

        commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));
        GDeviceFactory::GetDevice()->Flush();
        Flush();

        BuildFrameResources();
        Flush();

        // Allocate and populate the complete baked set before Run() displays
        // its first frame. Benchmark states never initialise cubemap resources.
        currentFrameResourceIndex = 0;
        currentFrameResource = frameResources[currentFrameResourceIndex].get();
        scene->Update();
        scene->UpdateMaterials(currentFrameResource);
        renderer->SetFrameResource(currentFrameResource);
        renderer->Update(*GetTimer());
        renderer->PrewarmReflectionProbeBakes();
        Flush();

#if !defined(DEBUG) && !defined(_DEBUG)
        // Each SSAA tier has 40 states: five 4-probe distributions, baked/full
        // dynamic capture, whole-probe/one-face updates, and SSR on either adapter.
        // Further tiers are appended only after the level's control state proves
        // that the current SSAA factor still sustains more than 30 FPS.
        benchmark.SetLooping(false);
        AddSsaaBenchmarkLevel(1);
        benchmark.Start();
#endif

        return true;
    }

    LRESULT ReflectionApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
    {
        switch (msg)
        {
        case WM_INPUT:
            {
                UINT dataSize;
                GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dataSize,
                                sizeof(RAWINPUTHEADER));

                if (dataSize > 0)
                {
                    auto rawdata = std::make_unique<BYTE[]>(dataSize);
                    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawdata.get(), &dataSize,
                                        sizeof(RAWINPUTHEADER)) == dataSize)
                    {
                        auto raw = reinterpret_cast<RAWINPUT*>(rawdata.get());
                        if (raw->header.dwType == RIM_TYPEMOUSE)
                        {
                            mouse.OnMouseMoveRaw(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
                        }
                    }
                }

                return DefWindowProc(hwnd, msg, wParam, lParam);
            }
        case WM_MOUSEMOVE:
            {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                mouse.OnMouseMove(x, y);
                return 0;
            }
        case WM_LBUTTONDOWN:
            {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                mouse.OnLeftPressed(x, y);
                return 0;
            }
        case WM_RBUTTONDOWN:
            {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                mouse.OnRightPressed(x, y);
                return 0;
            }
        case WM_MBUTTONDOWN:
            {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                mouse.OnMiddlePressed(x, y);
                return 0;
            }
        case WM_LBUTTONUP:
            {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                mouse.OnLeftReleased(x, y);
                return 0;
            }
        case WM_RBUTTONUP:
            {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                mouse.OnRightReleased(x, y);
                return 0;
            }
        case WM_MBUTTONUP:
            {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                mouse.OnMiddleReleased(x, y);
                return 0;
            }
        case WM_MOUSEWHEEL:
            {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                if (GET_WHEEL_DELTA_WPARAM(wParam) > 0)
                {
                    mouse.OnWheelUp(x, y);
                }
                else if (GET_WHEEL_DELTA_WPARAM(wParam) < 0)
                {
                    mouse.OnWheelDown(x, y);
                }
                return 0;
            }
        case WM_KEYUP:
            {
                unsigned char keycode = static_cast<unsigned char>(wParam);
                keyboard.OnKeyReleased(keycode);
                return 0;
            }
        case WM_KEYDOWN:
            {
                unsigned char keycode = static_cast<unsigned char>(wParam);
                const bool wasPressed = lParam & 0x40000000;
                const bool firstPress = !wasPressed;
                if (keyboard.IsKeysAutoRepeat() || firstPress)
                {
                    keyboard.OnKeyPressed(keycode);
                }

#if defined(DEBUG) || defined(_DEBUG)
                if (firstPress && keycode == VK_F2 && keyboard.KeyIsPressed(VK_F2))
                {
                    pathMapShow = (pathMapShow + 1) % maxPathMap;
                    if (renderer != nullptr)
                    {
                        renderer->SetDebugMap(pathMapShow);
                    }
                }
#endif

                if (firstPress && keycode == VK_F3 && keyboard.KeyIsPressed(VK_F3))
                {
                    ++ssaaMultiplier;
                    if (ssaaMultiplier > maxSsaaMultiplier)
                    {
                        ssaaMultiplier = 1;
                    }

                    if (renderer != nullptr)
                    {
                        renderer->SetSsaaMultiplier(ssaaMultiplier);
                    }
                }

                return 0;
            }
        case WM_CHAR:
            {
                unsigned char ch = static_cast<unsigned char>(wParam);
                if (keyboard.IsCharsAutoRepeat())
                {
                    keyboard.OnChar(ch);
                }
                else
                {
                    const bool wasPressed = lParam & 0x40000000;
                    if (!wasPressed)
                    {
                        keyboard.OnChar(ch);
                    }
                }
                return 0;
            }
        }

        return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
    }

    void ReflectionApp::CalculateFrameStats()
    {
#if defined(DEBUG) || defined(_DEBUG)
        D3DApp::CalculateFrameStats();
#endif
        // Release title belongs exclusively to the active benchmark state.
    }

    void ReflectionApp::OnResize()
    {
        D3DApp::OnResize();

        if (camera != nullptr)
        {
            camera->SetAspectRatio(AspectRatio());
        }

        if (renderer != nullptr)
        {
            renderer->OnResize();
        }
    }

    void ReflectionApp::Update(const GameTimer& gt)
    {
        auto commandQueue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Graphics);

        currentFrameResourceIndex = (currentFrameResourceIndex + 1) % globalCountFrameResources;
        currentFrameResource = frameResources[currentFrameResourceIndex].get();

        if (currentFrameResource->FenceValue != 0 && !commandQueue->IsFinish(currentFrameResource->FenceValue))
        {
            commandQueue->WaitForFenceValue(currentFrameResource->FenceValue);
        }

        const auto& devices = GDeviceFactory::GetAllDevices(false);
        if (devices.size() > GraphicAdapterSecond && currentFrameResource->SecondProbeFenceValue != 0)
        {
            auto secondQueue = GDeviceFactory::GetDevice(GraphicAdapterSecond)->GetCommandQueue(GQueueType::Graphics);
            if (!secondQueue->IsFinish(currentFrameResource->SecondProbeFenceValue))
            {
                secondQueue->WaitForFenceValue(currentFrameResource->SecondProbeFenceValue);
            }
        }

        benchmark.Tick(gt.DeltaTime());

        if (benchmark.IsFinished() && !benchmarkFinished)
        {
            benchmarkFinished = true;
            MainWindow->SetWindowTitle(L"MGPU-SSR | benchmark finished — close the window");
        }

        scene->Update();
        scene->UpdateMaterials(currentFrameResource);
        renderer->SetFrameResource(currentFrameResource);
        renderer->Update(gt);
    }

    void ReflectionApp::SetReflectionBenchmarkConfiguration(const bool useSecondGpuForSsr,
                                                             const UINT primaryProbeCount,
                                                             const bool dynamicReflectionProbes,
                                                             const bool updateOneProbeFacePerFrame,
                                                             const UINT ssaaLevel)
    {
        Flush();
        scene->ResetBenchmarkAnimation();
        isUsingSecondGpuForSsr = useSecondGpuForSsr;
        isUsingSecondGpuForReflectionProbes = primaryProbeCount < Scene::ReflectionProbeCount;
        isUsingDynamicReflectionProbes = dynamicReflectionProbes;
        isUpdatingOneProbeFacePerFrame = updateOneProbeFacePerFrame;
        ssaaMultiplier = ssaaLevel;
        renderer->SetSsaaMultiplier(ssaaMultiplier);
        renderer->SetUseSecondGpuForSsr(isUsingSecondGpuForSsr);
        renderer->SetUseSecondGpuForReflectionProbes(isUsingSecondGpuForReflectionProbes);
        renderer->SetPrimaryProbeCount(primaryProbeCount);
        renderer->SetUseDynamicReflectionProbes(isUsingDynamicReflectionProbes);
        renderer->SetUpdateOneProbeFacePerFrame(isUpdatingOneProbeFacePerFrame);
        renderer->ResetSecondaryBenchmarkAnimation();

        const bool presentOnSecondGpu = renderer->IsMgpuSsrEnabled();
        const auto presentationDevice = presentOnSecondGpu
                                            ? GDeviceFactory::GetDevice(GraphicAdapterSecond)
                                            : GDeviceFactory::GetDevice(GraphicAdapterPrimary);
        MainWindow->SetPresentationDevice(presentationDevice);
        renderer->SetPresentationOnSecondGpu(presentOnSecondGpu);
        renderer->OnResize();

        const UINT secondaryProbeCount = Scene::ReflectionProbeCount - primaryProbeCount;
        MainWindow->SetWindowTitle(
            L"MGPU-SSR | SSAA x" + std::to_wstring(ssaaLevel) +
            L" | Cubemap " + std::to_wstring(primaryProbeCount) + L"/" +
            std::to_wstring(secondaryProbeCount) +
            (dynamicReflectionProbes ? L" | Full dynamic" : L" | Baked + dynamic overlay") +
            (updateOneProbeFacePerFrame ? L" | One face/frame" : L" | One cubemap/frame") +
            (useSecondGpuForSsr ? L" | SSR Secondary" : L" | SSR Primary"));
    }

    void ReflectionApp::AddSsaaBenchmarkLevel(const UINT ssaaLevel)
    {
        constexpr uint32_t benchmarkDurationSeconds = 100;
        constexpr std::array<UINT, 5> primaryProbeCounts = {4, 0, 1, 2, 3};
        const auto primaryDevice = GDeviceFactory::GetDevice(GraphicAdapterPrimary);
        const auto& devices = GDeviceFactory::GetAllDevices(false);
        const auto secondDevice = devices.size() > GraphicAdapterSecond
                                      ? GDeviceFactory::GetDevice(GraphicAdapterSecond)
                                      : primaryDevice;
        const auto logDirectory = std::filesystem::current_path() / L"BenchmarkLogs";
        std::error_code error;
        std::filesystem::create_directories(logDirectory, error);

        for (const bool fullDynamic : {false, true})
        {
            for (const UINT primaryCount : primaryProbeCounts)
            {
                for (const bool updateOneFace : {false, true})
                {
                    for (const bool ssrOnSecondary : {false, true})
                    {
                        const UINT secondaryCount = Scene::ReflectionProbeCount - primaryCount;
                        const std::wstring cubeMapMode = fullDynamic
                                                             ? L"Full dynamic"
                                                             : L"Baked + local dynamic overlay";
                        const std::wstring updateMode = updateOneFace
                                                            ? L"One face/frame"
                                                            : L"One cubemap/frame";
                        const std::wstring ssrMode = ssrOnSecondary ? L"SSR Secondary" : L"SSR Primary";
                        const std::wstring name = L"SSAA x" + std::to_wstring(ssaaLevel) + L" | " + cubeMapMode +
                            L" | Cubemap " + std::to_wstring(primaryCount) + L"/" +
                            std::to_wstring(secondaryCount) + L" | " + updateMode + L" | " + ssrMode;
                        const std::wstring logName = L"SSAAx" + std::to_wstring(ssaaLevel) +
                            (fullDynamic ? L"_FullDynamic" : L"_BakedOverlay") +
                            L"_Cubemap" + std::to_wstring(primaryCount) + L"-" +
                            std::to_wstring(secondaryCount) +
                            (updateOneFace ? L"_OneFace" : L"_WholeCube") +
                            (ssrOnSecondary ? L"_SSRSecondary" : L"_SSRPrimary");
                        const bool isExpansionProbe = fullDynamic && primaryCount == 4 &&
                            !updateOneFace && !ssrOnSecondary;
                        const bool endsSsaaLevel = fullDynamic && primaryCount == 3 &&
                            updateOneFace && ssrOnSecondary;
                        const auto logPath = logDirectory /
                            (logName + L" " + primaryDevice->GetName() + L"+" + secondDevice->GetName() + L".log");

                        benchmark.AddState<ReflectionBenchmarkState>(
                            *this, ssrOnSecondary, primaryCount, fullDynamic, updateOneFace, ssaaLevel,
                            isExpansionProbe, endsSsaaLevel, name, benchmarkDurationSeconds,
                            FileQueueWriter(logPath));
                    }
                }
            }
        }
    }

    void ReflectionApp::OnReflectionBenchmarkStateCompleted(const bool isSsaaExpansionProbe,
                                                             const bool endsSsaaLevel,
                                                             const float averageFps)
    {
        if (isSsaaExpansionProbe)
        {
            expansionProbeFps = averageFps;
            nextSsaaLevelQueued = false;
            if (ssaaMultiplier < maxSsaaMultiplier && expansionProbeFps > 30.0f)
            {
                // Add the next complete level as soon as its control sample is
                // available. It stays after the already queued current level.
                AddSsaaBenchmarkLevel(ssaaMultiplier + 1);
                nextSsaaLevelQueued = true;
            }
        }

        if (!endsSsaaLevel)
        {
            return;
        }

        if (!nextSsaaLevelQueued)
        {
            benchmarkFinished = true;
            MainWindow->SetWindowTitle(L"MGPU-SSR | benchmark finished — close the window");
        }
    }

    void ReflectionApp::SetReflectionBenchmarkTitle(const std::wstring& stateName,
                                                    const uint32_t remainingSeconds, const float averageFps)
    {
        MainWindow->SetWindowTitle(
            L"MGPU-SSR | " + stateName + L" | Remaining: " + std::to_wstring(remainingSeconds) +
            L" s | Average FPS: " + std::format(L"{:.2f}", averageFps));
    }

    void ReflectionApp::Draw(const GameTimer& gt)
    {
        if (isResizing) return;

        auto commandQueue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Graphics);
        bool shouldPresent = true;

        if (renderer->IsMgpuProbeEnabled())
        {
            renderer->RenderProbesOnSecondGpu();

            auto cmdList = commandQueue->GetCommandList();
            renderer->RenderPrimaryWithImportedProbes(cmdList);
            currentFrameResource->FenceValue = commandQueue->ExecuteCommandList(cmdList);
            if (renderer->IsMgpuSsrEnabled())
            {
                shouldPresent = renderer->RenderSsrOnSecondGpu();
            }
        }
        else if (renderer->IsMgpuSsrEnabled())
        {
            auto cmdList = commandQueue->GetCommandList();
            renderer->RenderMgpuPrimary(cmdList);
            currentFrameResource->FenceValue = commandQueue->ExecuteCommandList(cmdList);

            // Like SSAO/MBlur, the secondary queue consumes current shared
            // inputs and publishes its output independently for a later frame.
            shouldPresent = renderer->RenderSsrOnSecondGpu();
        }
        else
        {
            auto cmdList = commandQueue->GetCommandList();
            renderer->Render(cmdList);
            currentFrameResource->FenceValue = commandQueue->ExecuteCommandList(cmdList);
        }

        if (shouldPresent)
        {
            backBufferIndex = MainWindow->Present();
        }
    }

    void ReflectionApp::BuildFrameResources()
    {
        UINT pointLightCount = 0;
        UINT spotLightCount = 0;
        for (const auto* light : scene->GetLights())
        {
            if (light->Type() == Point)
            {
                ++pointLightCount;
            }
            else if (light->Type() == Spot)
            {
                ++spotLightCount;
            }
        }

        std::shared_ptr<GDevice> secondDevice = nullptr;

        if (GDeviceFactory::GetAllDevices(false).size() > GraphicAdapterSecond)
        {
            secondDevice = GDeviceFactory::GetDevice(GraphicAdapterSecond);
        }

        for (int i = 0; i < globalCountFrameResources; ++i)
        {
            frameResources.push_back(
                std::make_unique<FrameResource>(GDeviceFactory::GetDevice(), secondDevice,
                                                static_cast<UINT>(scene->GetObjectCount()),
                                                static_cast<UINT>(scene->GetMaterialCount()),
                                                pointLightCount, spotLightCount));
        }
    }
}
