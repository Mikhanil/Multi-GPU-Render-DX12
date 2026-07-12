#include "ReflectionApp.h"

#include "Camera.h"
#include "GCommandQueue.h"
#include "GDeviceFactory.h"
#include "Window.h"
#include "GameObject.h"

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
                    ssaaMultiplier *= 2;
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

        scene->Update();
        scene->UpdateMaterials(currentFrameResource);
        renderer->SetFrameResource(currentFrameResource);
        renderer->Update(gt);
    }

    void ReflectionApp::Draw(const GameTimer& gt)
    {
        if (isResizing) return;

        auto commandQueue = GDeviceFactory::GetDevice()->GetCommandQueue(GQueueType::Graphics);
        auto cmdList = commandQueue->GetCommandList();

        renderer->Render(cmdList);

        currentFrameResource->FenceValue = commandQueue->ExecuteCommandList(cmdList);
        backBufferIndex = MainWindow->Present();
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

        for (int i = 0; i < globalCountFrameResources; ++i)
        {
            frameResources.push_back(
                std::make_unique<FrameResource>(GDeviceFactory::GetDevice(),
                                                static_cast<UINT>(scene->GetObjectCount()),
                                                static_cast<UINT>(scene->GetMaterialCount()),
                                                pointLightCount, spotLightCount));
        }
    }
}
