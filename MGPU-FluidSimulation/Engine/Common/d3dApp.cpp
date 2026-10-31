#include "pch.h"
#include "d3dApp.h"
#include <WindowsX.h>


#include "GCommandQueue.h"
#include "Window.h"
#include "MemoryAllocator.h"
#include "GDevice.h"
#include "GDeviceFactory.h"

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

namespace Common
{
    constexpr wchar_t WINDOW_CLASS_NAME[] = L"DXLibRenderWindowClass";

    using WindowPtr = std::shared_ptr<Window>;
    using WindowMap = custom_unordered_map<HWND, std::shared_ptr<Window>>;
    using WindowNameMap = custom_unordered_map<std::wstring, std::shared_ptr<Window>>;

    static WindowMap gs_Windows = MemoryAllocator::CreateUnorderedMap<HWND, std::shared_ptr<Window>>();
    static WindowNameMap gs_WindowByName = MemoryAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<Window>>();

    LRESULT CALLBACK
    MainWndProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
    {
        return D3DApp::GetApp().MsgProc(hwnd, msg, wParam, lParam);
    }


    KeyboardDevice* D3DApp::GetKeyboard()
    {
        return &keyboard;
    }

    Mousepad* D3DApp::GetMouse()
    {
        return &mouse;
    }

    Camera* D3DApp::GetMainCamera() const
    {
        return camera.get();
    }

    D3DApp* D3DApp::instance = nullptr;

    D3DApp& D3DApp::GetApp()
    {
        return *instance;
    }

    D3DApp::D3DApp(const HINSTANCE hInstance)
        : appInstance(hInstance)
    {
        // Windows 10 Creators update adds Per Monitor V2 DPI awareness context.
        // Using this awareness context allows the client area of the window 
        // to achieve 100% scaling while still allowing non-client window content to 
        // be rendered in a DPI sensitive fashion.
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &MainWndProc;
        windowClass.cbClsExtra = 0;
        windowClass.cbWndExtra = 0;
        windowClass.hInstance = appInstance;
        windowClass.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
        windowClass.lpszMenuName = nullptr;
        windowClass.lpszClassName = WINDOW_CLASS_NAME;

        if (!RegisterClass(&windowClass))
        {
            MessageBox(nullptr, L"RegisterClass Failed.", nullptr, 0);
            assert(&windowClass);
        }

        // Only one D3DApp can be constructed.
        assert(instance == nullptr);
        instance = this;
    }

    D3DApp::~D3DApp()
    {
        Flush();
    }

    void D3DApp::Destroy()
    {
        for (auto&& gs_window : gs_Windows)
        {
            gs_window.second->Destroy();
        }

        if (instance)
        {
            delete instance;
            instance = nullptr;
        }
    }

    // A wrapper struct to allow shared pointers for the window class.
    struct MakeWindow : Window
    {
        MakeWindow() = default;

        MakeWindow(const std::shared_ptr<GDevice>& device, const WNDCLASS& classWindow, const std::wstring& windowName,
                   const int clientWidth, const int clientHeight, const bool vSync)
            : Window(device, classWindow, windowName, clientWidth, clientHeight, vSync)
        {
        }
    };


    std::shared_ptr<Window> D3DApp::CreateRenderWindow(std::shared_ptr<GDevice> device, const std::wstring& windowName,
                                                       int clientWidth,
                                                       int clientHeight, bool vSync)
    {
        auto pWindow = std::make_shared<MakeWindow>(device, windowClass, windowName, clientWidth, clientHeight, vSync);
        gs_Windows.insert(WindowMap::value_type(pWindow->GetWindowHandle(), pWindow));
        gs_WindowByName.insert(WindowNameMap::value_type(windowName, pWindow));

        return pWindow;
    }

    void D3DApp::DestroyWindow(const std::wstring& windowName) const
    {
        WindowPtr pWindow = GetWindowByName(windowName);
        if (pWindow)
        {
            DestroyWindow(pWindow);
        }
    }

    void D3DApp::DestroyWindow(const std::shared_ptr<Window>& window)
    {
        if (window) window->Destroy();
    }

    std::shared_ptr<Window> D3DApp::GetWindowByName(const std::wstring& windowName)
    {
        std::shared_ptr<Window> window;
        const auto iter = gs_WindowByName.find(windowName);
        if (iter != gs_WindowByName.end())
        {
            window = iter->second;
        }

        return window;
    }

    void D3DApp::Quit(const int exitCode)
    {
        PostQuitMessage(exitCode);
    }


    void D3DApp::Flush()
    {
        auto& devices = GDeviceFactory::GetAllDevices();
        for (auto& device : devices)
        {
            device->Flush();
        }
    }

    GameTimer* D3DApp::GetTimer()
    {
        return &timer;
    }


    HINSTANCE D3DApp::AppInst() const
    {
        return appInstance;
    }

    std::shared_ptr<Window> D3DApp::MainWnd() const
    {
        return MainWindow;
    }

    float D3DApp::AspectRatio() const
    {
        return MainWindow->AspectRatio();
    }

    bool D3DApp::Get4xMsaaState() const
    {
        return isM4xMsaa;
    }

    void D3DApp::Set4xMsaaState(const bool value)
    {
        if (isM4xMsaa != value)
        {
            isM4xMsaa = value;
            OnResize();
        }
    }

    int D3DApp::Run()
    {
        MSG msg = {nullptr};

        timer.Reset();

        for (auto&& pair : gs_Windows)
        {
            pair.second->ResetTimer();
        }

        auto devices = GDeviceFactory::GetAllDevices(true);

        while (msg.message != WM_QUIT)
        {
            // If there are Window messages then process them.
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            // Otherwise, do animation/game stuff.
            else
            {
                timer.Tick();

                //if (!isAppPaused)
                {
                    CalculateFrameStats();
                    Update(timer);
                    Draw(timer);
                }
                //else
                {
                    //Sleep(100);
                }

                for (auto&& device : devices)
                {
                    device->ResetAllocators(frameCount);
                }
            }
        }

        return static_cast<int>(msg.wParam);
    }

    bool D3DApp::Initialize()
    {
        if (!InitMainWindow())
            return false;

        if (!InitDirect3D())
            return false;

        OnResize();


        return true;
    }


    void D3DApp::OnResize()
    {
        MainWindow->OnResize();
    }


    // Remove a window from our window lists.
    static void RemoveWindow(const HWND hWnd)
    {
        auto windowIter = gs_Windows.find(hWnd);
        if (windowIter != gs_Windows.end())
        {
            WindowPtr pWindow = windowIter->second;
            gs_WindowByName.erase(pWindow->GetWindowName());
            gs_Windows.erase(windowIter);
        }
    }


    LRESULT D3DApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
    {
        WindowPtr pWindow;
        {
            auto iter = gs_Windows.find(hwnd);
            if (iter != gs_Windows.end())
            {
                pWindow = iter->second;
            }
        }

        if (pWindow)
        {
            switch (msg)
            {
            // WM_ACTIVATE is sent when the window is activated or deactivated.  
            // We pause the game when the window is deactivated and unpause it 
            // when it becomes active.  
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE)
                {
                    //isAppPaused = true;
                    //timer.Stop();
                }
                else
                {
                    //isAppPaused = false;
                    //timer.Start();
                }
                return 0;

            // WM_SIZE is sent when the user resizes the window.  
            case WM_SIZE:
                {
                    // Save the new client area dimensions.

                    int width = static_cast<short>(LOWORD(lParam));
                    int height = static_cast<short>(HIWORD(lParam));

                    pWindow->SetWidth(width);
                    pWindow->SetHeight(height);

                    {
                        if (wParam == SIZE_MINIMIZED)
                        {
                            isAppPaused = true;
                            isMinimized = true;
                            isMaximized = false;
                        }
                        else if (wParam == SIZE_MAXIMIZED)
                        {
                            isAppPaused = false;
                            isMinimized = false;
                            isMaximized = true;
                            OnResize();
                        }
                        else if (wParam == SIZE_RESTORED)
                        {
                            // Restoring from minimized state?
                            if (isMinimized)
                            {
                                isAppPaused = false;
                                isMinimized = false;
                                OnResize();
                            }

                            // Restoring from maximized state?
                            else if (isMaximized)
                            {
                                isAppPaused = false;
                                isMaximized = false;
                                OnResize();
                            }
                            else if (isResizing)
                            {
                                // If user is dragging the resize bars, we do not resize 
                                // the buffers here because as the user continuously 
                                // drags the resize bars, a stream of WM_SIZE messages are
                                // sent to the window, and it would be pointless (and slow)
                                // to resize for each WM_SIZE message received from dragging
                                // the resize bars.  So instead, we reset after the user is 
                                // done resizing the window and releases the resize bars, which 
                                // sends a WM_EXITSIZEMOVE message.
                            }
                            else // API call such as SetWindowPos or swapChain->SetFullscreenState.
                            {
                                OnResize();
                            }
                        }
                    }
                    return 0;
                }

            // WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
            case WM_ENTERSIZEMOVE:
                isAppPaused = true;
                isResizing = true;
                timer.Stop();
                return 0;

            // WM_EXITSIZEMOVE is sent when the user releases the resize bars.
            // Here we reset everything based on the new window dimensions.
            case WM_EXITSIZEMOVE:
                isAppPaused = false;
                isResizing = false;
                timer.Start();
                OnResize();
                return 0;

            // WM_DESTROY is sent when the window is being destroyed.
            case WM_DESTROY:

                Flush();

            // If a window is being destroyed, remove it from the 
            // window maps.
                RemoveWindow(hwnd);

                if (gs_Windows.empty())
                {
                    // If there are no more windows, quit the application.
                    PostQuitMessage(0);
                }
                return 0;

            // The WM_MENUCHAR message is sent when a menu is active and the user presses 
            // a key that does not correspond to any mnemonic or accelerator key. 
            case WM_MENUCHAR:
                // Don't beep when we alt-enter.
                return MAKELRESULT(0, MNC_CLOSE);

            // Catch this message so to prevent the window from becoming too small.
            case WM_GETMINMAXINFO:
                ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
                ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
                return 0;
            }
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    bool D3DApp::InitMainWindow()
    {
        MainWindow = CreateRenderWindow(GDeviceFactory::GetHardwareDevice(), mainWindowCaption, 1920, 1080, false);

        return true;
    }


    bool D3DApp::InitDirect3D()
    {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
        msQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        msQualityLevels.SampleCount = 4;
        msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
        msQualityLevels.NumQualityLevels = 0;
        ThrowIfFailed(GDeviceFactory::GetHardwareDevice()->GetDXDevice()->CheckFeatureSupport(
            D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
            &msQualityLevels,
            sizeof(msQualityLevels)));

        m4xMsaaQuality = msQualityLevels.NumQualityLevels;
        assert(m4xMsaaQuality > 0 && "Unexpected MSAA quality level.");

#ifdef _DEBUG
        LogAdapters();
#endif


        return true;
    }

    void D3DApp::CalculateFrameStats()
    {
        frameCount++;

        if ((timer.TotalTime() - timeElapsed) >= 1.0f)
        {
            float fps = static_cast<float>(frameCount); // fps = frameCnt / 1
            float mspf = 1000.0f / fps;

            auto fpsStr = std::to_wstring(fps);
            std::wstring mspfStr = std::to_wstring(mspf);

            std::wstring windowText = MainWindow->GetWindowName() +
                L"    fps: " + fpsStr +
                L"   mspf: " + mspfStr;

            MainWindow->SetWindowTitle(windowText);

            frameCount = 0;
            timeElapsed += 1.0f;
        }
    }

    void D3DApp::LogAdapters()
    {
        auto dxgiFactory = GDeviceFactory::GetFactory();

        UINT i = 0;
        IDXGIAdapter* adapter = nullptr;
        std::vector<IDXGIAdapter*> adapterList;
        while (dxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
        {
            DXGI_ADAPTER_DESC desc;
            adapter->GetDesc(&desc);

            std::wstring text = L"***Adapter: ";
            text += desc.Description;
            text += L"\n";

            OutputDebugString(text.c_str());

            adapterList.push_back(adapter);

            ++i;
        }

        for (size_t i = 0; i < adapterList.size(); ++i)
        {
            ReleaseCom(adapterList[i]);
        }
    }
}
