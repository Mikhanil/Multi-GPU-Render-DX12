#include "d3dApp.h"
#include <WindowsX.h>


#include "GCommandQueue.h"
#include "Window.h"
#include "DXAllocator.h"
#include "GDevice.h"

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

namespace DXLib
{
	constexpr wchar_t WINDOW_CLASS_NAME[] = L"DXLibRenderWindowClass";
	
	using WindowPtr = std::shared_ptr<Window>;
	using WindowMap = custom_unordered_map< HWND, std::shared_ptr<Window> >;
	using WindowNameMap = custom_unordered_map< std::wstring, std::shared_ptr<Window> >;

	static WindowMap gs_Windows = DXAllocator::CreateUnorderedMap<HWND, std::shared_ptr<Window>>();
	static WindowNameMap gs_WindowByName = DXAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<Window>>();
	
	LRESULT CALLBACK
	MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return D3DApp::GetApp().MsgProc(hwnd, msg, wParam, lParam);
	}

	D3DApp* D3DApp::instance = nullptr;

	D3DApp& D3DApp::GetApp()
	{
		return *instance;
	}

	D3DApp::D3DApp(HINSTANCE hInstance)
		: appInstance(hInstance)
	{
		InitialAdaptersAndDevices();		
		
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
		

		{
			

			m_TearingSupported = CheckTearingSupport();
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

	bool D3DApp::IsTearingSupported() const
	{
		return m_TearingSupported;
	}

	// A wrapper struct to allow shared pointers for the window class.
	struct MakeWindow : public Window
	{
		MakeWindow() = default;

		MakeWindow(WNDCLASS classWindow, const std::wstring& windowName, int clientWidth, int clientHeight, bool vSync)
			: Window(classWindow, windowName, clientWidth, clientHeight, vSync)
		{
		}
	};


	std::shared_ptr<Window> D3DApp::CreateRenderWindow(const std::wstring& windowName, int clientWidth,
	                                                   int clientHeight, bool vSync)
	{
				
		auto pWindow = std::make_shared<MakeWindow>(windowClass, windowName, clientWidth, clientHeight, vSync);
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

	void D3DApp::DestroyWindow(std::shared_ptr<Window> window)
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

	void D3DApp::Quit(int exitCode)
	{
		PostQuitMessage(exitCode);
	}


	void D3DApp::Flush()
	{
		/*for (auto && device : gdevices)
		{
			device.value()->Flush();
		}*/

		
		gdevices[0].value()->Flush();		
		//gdevices[1].value()->Flush();		
	}
	
	UINT D3DApp::GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const
	{
		return gdevices[0].value()->GetDescriptorHandleIncrementSize(type);
	}
	
	void D3DApp::InitialAdaptersAndDevices()
	{		
		{
#if defined(DEBUG) || defined(_DEBUG)
			{
				ComPtr<ID3D12Debug> debugController;
				ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
				debugController->EnableDebugLayer();
			}
#endif

			dxgiFactory.Reset();
			UINT createFactoryFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
			createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif
						

			ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));


			
			UINT adapterindex = 0;
			ComPtr<IDXGIAdapter1> adapter;
			while (dxgiFactory->EnumAdapters1(adapterindex++, &adapter) != DXGI_ERROR_NOT_FOUND)
			{
				DXGI_ADAPTER_DESC1 desc;
				adapter->GetDesc1(&desc);

				wstring name(desc.Description);

				//Skip warp adapter
				if(name.find(L"Microsoft") != std::wstring::npos)
				{
					continue;
				}
				
				
				ComPtr<IDXGIAdapter3>  adapter3;
				ThrowIfFailed(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)));
				adapters.push_back((adapter3));
								
				
				gdevices.push_back(std::move(Lazy< std::shared_ptr<GDevice >>([adapter3] { return std::make_shared<GDevice>(adapter3);    })));
				
			}

			if(gdevices.size() <= 1)
			{
				dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
				ComPtr<IDXGIAdapter3>  adapter3;
				ThrowIfFailed(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)));
				adapters.push_back((adapter3));
				gdevices.push_back(std::move(Lazy< std::shared_ptr<GDevice >>([adapter3] { return std::make_shared<GDevice>(adapter3);    })));				
			}
		}
	}

	bool D3DApp::CheckTearingSupport() const
	{
		BOOL allowTearing = FALSE;

		// Rather than create the DXGI 1.5 factory interface directly, we create the
		// DXGI 1.4 interface and query for the 1.5 interface. This is to enable the 
		// graphics debugging tools which will not support the 1.5 factory interface 
		// until a future update.
		ComPtr<IDXGIFactory4> factory4;
		if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
		{
			ComPtr<IDXGIFactory5> factory5;
			if (SUCCEEDED(factory4.As(&factory5)))
			{
				factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
				                              &allowTearing, sizeof(allowTearing));
			}
		}

		return allowTearing == TRUE;
	}

	GameTimer* D3DApp::GetTimer()
	{
		return &timer;
	}

	ID3D12Device& D3DApp::GetDevice()
	{
		return *(GetApp().GetMainDevice()->GetDevice().Get());
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

	void D3DApp::Set4xMsaaState(bool value)
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

		for (auto && pair : gs_Windows)
		{
			pair.second->ResetTimer();
		}
		
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

				DXAllocator::ResetAllocator();
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
	static void RemoveWindow(HWND hWnd)
	{
		auto windowIter = gs_Windows.find(hWnd);
		if (windowIter != gs_Windows.end())
		{
			WindowPtr pWindow = windowIter->second;
			gs_WindowByName.erase(pWindow->GetWindowName());
			gs_Windows.erase(windowIter);
		}
	}

	
	LRESULT D3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		WindowPtr pWindow;
		{
			WindowMap::iterator iter = gs_Windows.find(hwnd);
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
			case WM_SIZE: {
				// Save the new client area dimensions.

				int width = ((int)(short)LOWORD(lParam));
				int height = ((int)(short)HIWORD(lParam));

				pWindow->SetWidth(width);
				pWindow->SetHeight(height);

				if (GetMainDevice())
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
		MainWindow = CreateRenderWindow(mainWindowCaption, 1920, 1080, false);
		
		return true;
	}

	std::shared_ptr<GDevice> D3DApp::GetMainDevice() const
	{
		return gdevices[0].value();
	}

	bool D3DApp::InitDirect3D()
	{
		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
		msQualityLevels.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		msQualityLevels.SampleCount = 4;
		msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
		msQualityLevels.NumQualityLevels = 0;
		ThrowIfFailed(GetMainDevice()->GetDevice()->CheckFeatureSupport(
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

	void D3DApp::CalculateFrameStats() const
	{
		MainWindow->OnRender();
	}

	void D3DApp::LogAdapters()
	{
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
