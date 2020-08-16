#include "d3dApp.h"
#include <WindowsX.h>


#include "GCommandQueue.h"
#include "Window.h"
#include "DXAllocator.h"

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
			directCommandQueue = std::make_shared<GCommandQueue>(GetOrCreateDevice(), D3D12_COMMAND_LIST_TYPE_DIRECT);
			computeCommandQueue = std::make_shared<GCommandQueue>(GetOrCreateDevice(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
			copyCommandQueue = std::make_shared<GCommandQueue>(GetOrCreateDevice(), D3D12_COMMAND_LIST_TYPE_COPY);

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

	void D3DApp::Destroy() const
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

	void D3DApp::DestroyWindow(const std::wstring& windowName)
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

	std::shared_ptr<GCommandQueue> D3DApp::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
	{
		std::shared_ptr<GCommandQueue> commandQueue;
		switch (type)
		{
		case D3D12_COMMAND_LIST_TYPE_DIRECT:
			commandQueue = directCommandQueue;
			break;
		case D3D12_COMMAND_LIST_TYPE_COMPUTE:
			commandQueue = computeCommandQueue;
			break;
		case D3D12_COMMAND_LIST_TYPE_COPY:
			commandQueue = copyCommandQueue;
			break;
		default:
			assert(false && "Invalid command queue type.");
		}

		return commandQueue;
	}

	void D3DApp::Flush()
	{
		directCommandQueue->Signal();
		directCommandQueue->Flush();
		
		computeCommandQueue->Signal();
		computeCommandQueue->Flush();

		copyCommandQueue->Signal();
		copyCommandQueue->Flush();
	}

	

	UINT D3DApp::GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const
	{
		return dxDevice->GetDescriptorHandleIncrementSize(type);
	}

	ComPtr<IDXGIAdapter4> D3DApp::GetAdapter(bool bUseWarp)
	{
		ComPtr<IDXGIFactory4> dxgiFactory;
		UINT createFactoryFlags = 0;
#if defined(_DEBUG)
		createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

		ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

		ComPtr<IDXGIAdapter1> dxgiAdapter1;
		ComPtr<IDXGIAdapter4> dxgiAdapter4;

		if (bUseWarp)
		{
			ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
			ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
		}
		else
		{
			SIZE_T maxDedicatedVideoMemory = 0;
			for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
			{
				DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
				dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

				// Check to see if the adapter can create a D3D12 device without actually 
				// creating it. The adapter with the largest dedicated video memory
				// is favored.
				if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
					SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(),
						D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
					dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
				{
					maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
					ThrowIfFailed(dxgiAdapter1.As(&dxgiAdapter4));
				}
			}
		}

		return dxgiAdapter4;
	}

	ComPtr<ID3D12Device2> D3DApp::GetOrCreateDevice()
	{
		if (dxDevice == nullptr)
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

			HRESULT hardwareResult = D3D12CreateDevice(
				nullptr, // default adapter
				D3D_FEATURE_LEVEL_11_0,
				IID_PPV_ARGS(&dxDevice));

			if (FAILED(hardwareResult))
			{
				ComPtr<IDXGIAdapter> pWarpAdapter;
				ThrowIfFailed(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&pWarpAdapter)));

				ThrowIfFailed(D3D12CreateDevice(
					pWarpAdapter.Get(),
					D3D_FEATURE_LEVEL_11_0,
					IID_PPV_ARGS(&dxDevice)));
			}

#if defined(DEBUG) || defined(_DEBUG)

			ComPtr<ID3D12InfoQueue> pInfoQueue;
			if (SUCCEEDED(dxDevice.As(&pInfoQueue)))
			{
				pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
				pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
				pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);


				// Suppress messages based on their severity level
				D3D12_MESSAGE_SEVERITY Severities[] =
				{
					D3D12_MESSAGE_SEVERITY_INFO
				};

				// Suppress individual messages by their ID
				D3D12_MESSAGE_ID DenyIds[] = {
					D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
					// I'm really not sure how to avoid this message.
					D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
					// This warning occurs when using capture frame while graphics debugging.
					D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
					// This warning occurs when using capture frame while graphics debugging.
				};

				D3D12_INFO_QUEUE_FILTER NewFilter = {};
				NewFilter.DenyList.NumSeverities = _countof(Severities);
				NewFilter.DenyList.pSeverityList = Severities;
				NewFilter.DenyList.NumIDs = _countof(DenyIds);
				NewFilter.DenyList.pIDList = DenyIds;

				ThrowIfFailed(pInfoQueue->PushStorageFilter(&NewFilter));
			}
#endif
		}

		return dxDevice;
	}

	bool D3DApp::CheckTearingSupport()
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
		return *(GetApp().dxDevice.Get());
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
		assert(dxDevice);

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

				if (dxDevice)
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

	bool D3DApp::InitDirect3D()
	{
		GetOrCreateDevice();


		rtvDescriptorSize = dxDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
		dsvDescriptorSize = dxDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		cbvSrvUavDescriptorSize = dxDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
		msQualityLevels.Format = backBufferFormat;
		msQualityLevels.SampleCount = 4;
		msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
		msQualityLevels.NumQualityLevels = 0;
		ThrowIfFailed(dxDevice->CheckFeatureSupport(
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
			LogAdapterOutputs(adapterList[i]);
			ReleaseCom(adapterList[i]);
		}
	}

	void D3DApp::LogAdapterOutputs(IDXGIAdapter* adapter)
	{
		UINT i = 0;
		IDXGIOutput* output = nullptr;
		while (adapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND)
		{
			DXGI_OUTPUT_DESC desc;
			output->GetDesc(&desc);

			std::wstring text = L"***Output: ";
			text += desc.DeviceName;
			text += L"\n";
			OutputDebugString(text.c_str());

			LogOutputDisplayModes(output, backBufferFormat);

			ReleaseCom(output);

			++i;
		}
	}

	void D3DApp::LogOutputDisplayModes(IDXGIOutput* output, DXGI_FORMAT format)
	{
		UINT count = 0;
		UINT flags = 0;

		// Call with nullptr to get list count.
		output->GetDisplayModeList(format, flags, &count, nullptr);

		std::vector<DXGI_MODE_DESC> modeList(count);
		output->GetDisplayModeList(format, flags, &count, &modeList[0]);

		for (auto& x : modeList)
		{
			UINT n = x.RefreshRate.Numerator;
			UINT d = x.RefreshRate.Denominator;
			std::wstring text =
				L"Width = " + std::to_wstring(x.Width) + L" " +
				L"Height = " + std::to_wstring(x.Height) + L" " +
				L"Refresh = " + std::to_wstring(n) + L"/" + std::to_wstring(d) +
				L"\n";

			::OutputDebugString(text.c_str());
		}
	}
}
