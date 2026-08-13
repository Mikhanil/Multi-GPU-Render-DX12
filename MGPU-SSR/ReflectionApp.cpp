#include "ReflectionApp.h"

#include "Camera.h"
#include "GCommandQueue.h"
#include "GDeviceFactory.h"
#include "Window.h"
#include "GameObject.h"
#include "States/ReflectionBenchmarkState.h"
#include <algorithm>
#include <array>
#include <cstdlib>
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

        InitializeGpuPairContexts();
        if (gpuPairContexts.empty())
        {
            return false;
        }
        ActivateGpuPairContext(0);

#if !defined(DEBUG) && !defined(_DEBUG)
        // Each GPU pair has 40 states per SSAA tier: five 4-probe distributions,
        // baked/full dynamic capture, whole-probe/one-face updates, and SSR placement.
        // Further tiers are appended only after the level's control state proves
        // that the current SSAA factor still sustains more than 30 FPS.
        benchmark.SetLooping(false);
        currentBenchmarkPairIndex = 0;
        AddSsaaBenchmarkLevel(1, currentBenchmarkPairIndex);
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
        auto commandQueue = activeGpuPairContext->primaryDevice->GetCommandQueue(GQueueType::Graphics);

        activeGpuPairContext->currentFrameResourceIndex =
            (activeGpuPairContext->currentFrameResourceIndex + 1) % globalCountFrameResources;
        currentFrameResource = activeGpuPairContext->frameResources[
            activeGpuPairContext->currentFrameResourceIndex].get();

        if (currentFrameResource->FenceValue != 0 && !commandQueue->IsFinish(currentFrameResource->FenceValue))
        {
            commandQueue->WaitForFenceValue(currentFrameResource->FenceValue);
        }

        benchmark.Tick(gt.DeltaTime());

#if !defined(DEBUG) && !defined(_DEBUG)
        // The final state's log has already been written synchronously from
        // ReflectionBenchmarkState::Exit().  Exit here instead of closing the
        // window: normal teardown flushes every GPU and can wait forever after
        // a secondary adapter has stopped making progress.
        if (benchmark.IsFinished())
        {
            std::exit(EXIT_SUCCESS);
        }
#endif

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

    void ReflectionApp::SetReflectionBenchmarkConfiguration(const UINT gpuPairContextIndex,
                                                             const bool useSecondGpuForSsr,
                                                             const UINT primaryProbeCount,
                                                             const bool dynamicReflectionProbes,
                                                             const bool updateOneProbeFacePerFrame,
                                                             const UINT ssaaLevel)
    {
        assert(gpuPairContextIndex < gpuPairContexts.size());
        const bool gpuPairChanged = activeGpuPairContext != gpuPairContexts[gpuPairContextIndex].get();

        if (!gpuPairChanged && activeGpuPairContext != nullptr)
        {
            activeGpuPairContext->primaryDevice->Flush();
            if (activeGpuPairContext->secondaryDevice != nullptr)
            {
                activeGpuPairContext->secondaryDevice->Flush();
            }
        }

        isUsingSecondGpuForSsr = useSecondGpuForSsr;
        isUsingSecondGpuForReflectionProbes = primaryProbeCount < Scene::ReflectionProbeCount;
        isUsingDynamicReflectionProbes = dynamicReflectionProbes;
        isUpdatingOneProbeFacePerFrame = updateOneProbeFacePerFrame;
        ssaaMultiplier = ssaaLevel;

        ActivateGpuPairContext(gpuPairContextIndex);
        scene->ResetBenchmarkAnimation();
        renderer->SetSsaaMultiplier(ssaaMultiplier);
        renderer->SetUseSecondGpuForSsr(isUsingSecondGpuForSsr);
        renderer->SetUseSecondGpuForReflectionProbes(isUsingSecondGpuForReflectionProbes);
        renderer->SetPrimaryProbeCount(primaryProbeCount);
        renderer->SetUseDynamicReflectionProbes(isUsingDynamicReflectionProbes);
        renderer->SetUpdateOneProbeFacePerFrame(isUpdatingOneProbeFacePerFrame);
        renderer->ResetSecondaryBenchmarkAnimation();

        const bool presentOnSecondGpu = renderer->IsMgpuSsrEnabled();
        const auto presentationDevice = presentOnSecondGpu
                                            ? activeGpuPairContext->secondaryDevice
                                            : activeGpuPairContext->primaryDevice;
        MainWindow->SetPresentationDevice(presentationDevice);
        renderer->SetPresentationOnSecondGpu(presentOnSecondGpu);
        // MGPU OnResize also rebinds the active swap-chain back buffers and
        // rebuilds the cross-adapter SSR resources. It must run after the final
        // benchmark configuration even when the presentation adapter did not
        // change; skipping it leaves stale bindings after lazy pair creation.
        renderer->OnResize();

        const UINT secondaryProbeCount = Scene::ReflectionProbeCount - primaryProbeCount;
        MainWindow->SetWindowTitle(
            L"MGPU-SSR | SSAA x" + std::to_wstring(ssaaLevel) +
            L" | GPU " + std::to_wstring(activeGpuPairContext->primaryAdapterIndex + 1) + L"+" +
            std::to_wstring(activeGpuPairContext->secondaryAdapterIndex + 1) +
            L" | Cubemap " + std::to_wstring(primaryProbeCount) + L"/" +
            std::to_wstring(secondaryProbeCount) +
            (dynamicReflectionProbes ? L" | Full dynamic" : L" | Baked + dynamic overlay") +
            (updateOneProbeFacePerFrame ? L" | One face/frame" : L" | One cubemap/frame") +
            (useSecondGpuForSsr ? L" | SSR Secondary" : L" | SSR Primary"));
    }

    void ReflectionApp::AddSsaaBenchmarkLevel(const UINT ssaaLevel, const UINT gpuPairContextIndex)
    {
        constexpr uint32_t benchmarkDurationSeconds = 100;
        constexpr std::array<UINT, 5> primaryProbeCounts = {4, 0, 1, 2, 3};
        const auto logDirectory = std::filesystem::current_path() / L"BenchmarkLogs";
        std::error_code error;
        std::filesystem::create_directories(logDirectory, error);

        assert(gpuPairContextIndex < gpuPairContexts.size());
        const auto& pair = *gpuPairContexts[gpuPairContextIndex];
        const std::wstring pairMode = L"GPU " + std::to_wstring(pair.primaryAdapterIndex + 1) + L"+" +
            std::to_wstring(pair.secondaryAdapterIndex + 1);
        const std::wstring pairLogName = L"GPU" + std::to_wstring(pair.primaryAdapterIndex + 1) + L"-" +
            std::to_wstring(pair.secondaryAdapterIndex + 1);
        for (const bool fullDynamic : {false, true})
        {
            for (const UINT primaryCount : primaryProbeCounts)
            {
                for (const bool updateOneFace : {false, true})
                {
                    for (const bool ssrOnSecondary : {false, true})
                    {
                        if (pair.secondaryDevice == nullptr &&
                            (ssrOnSecondary || primaryCount < Scene::ReflectionProbeCount))
                        {
                            continue;
                        }
                        const UINT secondaryCount = Scene::ReflectionProbeCount - primaryCount;
                        const std::wstring cubeMapMode = fullDynamic
                                                             ? L"Full dynamic"
                                                             : L"Baked + local dynamic overlay";
                        const std::wstring updateMode = updateOneFace
                                                            ? L"One face/frame"
                                                            : L"One cubemap/frame";
                        const std::wstring ssrMode = ssrOnSecondary ? L"SSR Secondary" : L"SSR Primary";
                        const std::wstring name = L"SSAA x" + std::to_wstring(ssaaLevel) + L" | " + pairMode +
                            L" | " + cubeMapMode + L" | Cubemap " + std::to_wstring(primaryCount) + L"/" +
                            std::to_wstring(secondaryCount) + L" | " + updateMode + L" | " + ssrMode;
                        const std::wstring logName = L"SSAAx" + std::to_wstring(ssaaLevel) + L"_" + pairLogName +
                            (fullDynamic ? L"_FullDynamic" : L"_BakedOverlay") +
                            L"_Cubemap" + std::to_wstring(primaryCount) + L"-" +
                            std::to_wstring(secondaryCount) +
                            (updateOneFace ? L"_OneFace" : L"_WholeCube") +
                            (ssrOnSecondary ? L"_SSRSecondary" : L"_SSRPrimary");
                        const bool isExpansionProbe = fullDynamic && primaryCount == Scene::ReflectionProbeCount &&
                            !updateOneFace && !ssrOnSecondary;
                        const bool endsSsaaLevel = fullDynamic && updateOneFace &&
                            ((pair.secondaryDevice != nullptr && primaryCount == 3 && ssrOnSecondary) ||
                             (pair.secondaryDevice == nullptr && primaryCount == Scene::ReflectionProbeCount &&
                              !ssrOnSecondary));
                        const auto logPath = logDirectory /
                            (logName + L" " + pair.primaryDevice->GetName() + L"+" +
                             (pair.secondaryDevice != nullptr
                                  ? pair.secondaryDevice->GetName()
                                  : L"SingleGPU") + L".log");

                        benchmark.AddState<ReflectionBenchmarkState>(
                            *this, gpuPairContextIndex, ssrOnSecondary, primaryCount, fullDynamic, updateOneFace,
                            ssaaLevel, isExpansionProbe, endsSsaaLevel, name, benchmarkDurationSeconds,
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
            nextGpuPairQueued = false;
            if (ssaaMultiplier < maxSsaaMultiplier && expansionProbeFps > 30.0f)
            {
                // Add the next level for this pair behind the remaining states
                // of the current level.
                AddSsaaBenchmarkLevel(ssaaMultiplier + 1, currentBenchmarkPairIndex);
                nextSsaaLevelQueued = true;
            }
            else if (currentBenchmarkPairIndex + 1 < gpuPairContexts.size())
            {
                // BenchmarkService decides whether another state exists before
                // it calls Exit() on the last state. Queue the next pair here,
                // while the current level still has states left to execute.
                AddSsaaBenchmarkLevel(1, currentBenchmarkPairIndex + 1);
                nextGpuPairQueued = true;
            }
        }

        if (!endsSsaaLevel)
        {
            return;
        }

        if (nextSsaaLevelQueued)
        {
            return;
        }

        if (nextGpuPairQueued)
        {
            ++currentBenchmarkPairIndex;
            expansionProbeFps = 0.0f;
            nextGpuPairQueued = false;
        }
        else
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

        auto commandQueue = activeGpuPairContext->primaryDevice->GetCommandQueue(GQueueType::Graphics);
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

    void ReflectionApp::InitializeGpuPairContexts()
    {
        const auto& devices = GDeviceFactory::GetAllDevices(false);
        const UINT hardwareDeviceCount = static_cast<UINT>(std::min<size_t>(devices.size(), GraphicAdapterCount));
        if (hardwareDeviceCount == 0)
        {
            return;
        }

        std::vector<std::pair<UINT, UINT>> adapterPairs;
        if (hardwareDeviceCount >= GraphicAdapterCount)
        {
            // The extended benchmark is enabled only when three real hardware
            // adapters exist. WARP is absent from `devices` by construction.
            adapterPairs.emplace_back(GraphicAdapterPrimary, GraphicAdapterSecond);
            adapterPairs.emplace_back(GraphicAdapterPrimary, GraphicAdapterThird);
            adapterPairs.emplace_back(GraphicAdapterSecond, GraphicAdapterThird);
        }
        else if (hardwareDeviceCount >= 2)
        {
            adapterPairs.emplace_back(GraphicAdapterPrimary, GraphicAdapterSecond);
        }
        else
        {
            adapterPairs.emplace_back(0, 0);
        }

        for (const auto [primaryIndex, secondaryIndex] : adapterPairs)
        {
            auto context = std::make_unique<GpuPairContext>();
            context->primaryAdapterIndex = primaryIndex;
            context->secondaryAdapterIndex = secondaryIndex;
            context->primaryDevice = devices[primaryIndex];
            context->secondaryDevice = primaryIndex == secondaryIndex ? nullptr : devices[secondaryIndex];
            gpuPairContexts.emplace_back(std::move(context));
        }
    }

    void ReflectionApp::InitializeGpuPairContext(GpuPairContext& context)
    {
        assert(context.primaryDevice != nullptr);
        assert(context.scene == nullptr);
        assert(context.renderer == nullptr);
        assert(context.frameResources.empty());

        MainWindow->SetPresentationDevice(context.primaryDevice);
        auto commandQueue = context.primaryDevice->GetCommandQueue(GQueueType::Graphics);
        auto cmdList = commandQueue->GetCommandList();

        context.scene = std::make_unique<Scene>(context.primaryDevice);
        context.scene->Initialize(cmdList, AspectRatio(), mainLightDirection);
        const auto contextCamera = context.scene->GetCamera();
        context.renderer = std::make_unique<ReflectionRenderer>(
            MainWindow, *context.scene, contextCamera, context.primaryDevice, context.secondaryDevice,
            backBufferFormat, depthStencilFormat, isM4xMsaa, m4xMsaaQuality);
        context.renderer->Initialize(cmdList);
#if defined(DEBUG) || defined(_DEBUG)
        context.renderer->SetDebugMap(pathMapShow);
#endif
        context.renderer->SetSsaaMultiplier(ssaaMultiplier);
        context.renderer->SetUseSecondGpuForSsr(isUsingSecondGpuForSsr);
        context.renderer->SetUseSecondGpuForReflectionProbes(isUsingSecondGpuForReflectionProbes);
        context.renderer->SetPrimaryProbeCount(isUsingSecondGpuForReflectionProbes ? 0 : Scene::ReflectionProbeCount);
        context.renderer->SetUseDynamicReflectionProbes(isUsingDynamicReflectionProbes);
        context.renderer->SetUpdateOneProbeFacePerFrame(isUpdatingOneProbeFacePerFrame);
        const bool presentOnSecondGpu = context.renderer->IsMgpuSsrEnabled();
        MainWindow->SetPresentationDevice(presentOnSecondGpu ? context.secondaryDevice : context.primaryDevice);
        context.renderer->SetPresentationOnSecondGpu(presentOnSecondGpu);
        context.renderer->OnResize();

        commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));

        BuildFrameResources(context);
        context.currentFrameResourceIndex = 0;
        auto* prewarmFrameResource = context.frameResources[0].get();
        context.scene->Update();
        context.scene->UpdateMaterials(prewarmFrameResource);
        context.renderer->SetFrameResource(prewarmFrameResource);
        context.renderer->Update(*GetTimer());
        context.renderer->PrewarmReflectionProbeBakes();
        if (context.secondaryDevice != nullptr)
        {
            context.secondaryDevice->Flush();
        }

        std::wstring message = L"[MGPU-SSR] Loaded GPU pair ";
        message += std::to_wstring(context.primaryAdapterIndex + 1);
        message += L"+";
        message += std::to_wstring(context.secondaryAdapterIndex + 1);
        message += L": ";
        message += context.primaryDevice->GetName();
        if (context.secondaryDevice != nullptr)
        {
            message += L" + ";
            message += context.secondaryDevice->GetName();
        }
        message += L"\n";
        OutputDebugStringW(message.c_str());
    }

    void ReflectionApp::ReleaseActiveGpuPairContext()
    {
        if (activeGpuPairContext == nullptr)
        {
            return;
        }

        // Every resource in the pair is released only after both adapters have
        // stopped using it. The GDevice objects themselves stay owned by the
        // factory and are reused by the next pair.
        activeGpuPairContext->primaryDevice->Flush();
        if (activeGpuPairContext->secondaryDevice != nullptr)
        {
            activeGpuPairContext->secondaryDevice->Flush();
        }

        std::wstring message = L"[MGPU-SSR] Releasing GPU pair ";
        message += std::to_wstring(activeGpuPairContext->primaryAdapterIndex + 1);
        message += L"+";
        message += std::to_wstring(activeGpuPairContext->secondaryAdapterIndex + 1);
        message += L"\n";
        OutputDebugStringW(message.c_str());

        currentFrameResource = nullptr;
        renderer = nullptr;
        scene = nullptr;
        camera.reset();

        activeGpuPairContext->renderer.reset();
        activeGpuPairContext->frameResources.clear();
        activeGpuPairContext->scene.reset();
        activeGpuPairContext->primaryDevice->ResetAllocators(0);
        if (activeGpuPairContext->secondaryDevice != nullptr)
        {
            activeGpuPairContext->secondaryDevice->ResetAllocators(0);
        }
        activeGpuPairContext->currentFrameResourceIndex = 0;
        activeGpuPairContext = nullptr;
    }

    void ReflectionApp::ActivateGpuPairContext(const UINT contextIndex)
    {
        assert(contextIndex < gpuPairContexts.size());
        auto* requestedContext = gpuPairContexts[contextIndex].get();
        if (requestedContext != activeGpuPairContext)
        {
            ReleaseActiveGpuPairContext();
            InitializeGpuPairContext(*requestedContext);
            activeGpuPairContext = requestedContext;
        }

        scene = activeGpuPairContext->scene.get();
        renderer = activeGpuPairContext->renderer.get();
        camera = scene->GetCamera();
        currentFrameResource = activeGpuPairContext->frameResources[
            activeGpuPairContext->currentFrameResourceIndex].get();
        renderer->SetFrameResource(currentFrameResource);
    }

    void ReflectionApp::BuildFrameResources(GpuPairContext& context)
    {
        UINT pointLightCount = 0;
        UINT spotLightCount = 0;
        for (const auto* light : context.scene->GetLights())
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

        for (int i = 0; i < globalCountFrameResources; ++i)
        {
            context.frameResources.push_back(
                std::make_unique<FrameResource>(context.primaryDevice, context.secondaryDevice,
                                                static_cast<UINT>(context.scene->GetObjectCount()),
                                                static_cast<UINT>(context.scene->GetMaterialCount()),
                                                pointLightCount, spotLightCount));
        }
    }
}
