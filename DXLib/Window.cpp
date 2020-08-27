#include "Window.h"

#include <cassert>



#include "d3dApp.h"
#include "GCommandQueue.h"
#include "d3dUtil.h"
#include "GResourceStateTracker.h"
#include "Lazy.h"
#include "GCommandList.h"

namespace DXLib
{
	LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		return D3DApp::GetApp().MsgProc(hwnd, msg, wParam, lParam);
	}


	HWND Window::GetWindowHandle() const
	{
		return hWnd;
	}

	void Window::Destroy()
	{
		if (hWnd)
		{
			swapChain.Reset();
			swapChain = nullptr;
			DestroyWindow(hWnd);
			hWnd = nullptr;
		}

		for (auto && back_buffer : backBuffers)
		{
			back_buffer.Reset();
		}

		backBuffers.clear();
	}


	const std::wstring& Window::GetWindowName() const
	{
		return windowName;
	}

	int Window::GetClientWidth() const
	{
		return width;
	}

	int Window::GetClientHeight() const
	{
		return height;
	}

	bool Window::IsVSync() const
	{
		return vSync;
	}

	void Window::SetVSync(bool vSync)
	{
		this->vSync = vSync;
	}


	bool Window::IsFullScreen() const
	{
		return fullscreen;
	}

	// Set the fullscreen state of the window.
	void Window::SetFullscreen(bool fullscreen)
	{
		if (fullscreen != fullscreen)
		{
			fullscreen = fullscreen;

			if (fullscreen) // Switching to fullscreen.
			{
				// Store the current window dimensions so they can be restored 
				// when switching out of fullscreen state.
				GetWindowRect(hWnd, &windowRect);

				// Set the window style to a borderless window so the client area fills
				// the entire screen.
				UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX |
					WS_MAXIMIZEBOX);

				SetWindowLongW(hWnd, GWL_STYLE, windowStyle);

				// Query the name of the nearest display device for the window.
				// This is required to set the fullscreen dimensions of the window
				// when using a multi-monitor setup.
				HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
				MONITORINFOEX monitorInfo = {};
				monitorInfo.cbSize = sizeof(MONITORINFOEX);
				::GetMonitorInfo(hMonitor, &monitorInfo);

				SetWindowPos(hWnd, HWND_TOPMOST,
				             monitorInfo.rcMonitor.left,
				             monitorInfo.rcMonitor.top,
				             monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
				             monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
				             SWP_FRAMECHANGED | SWP_NOACTIVATE);

				ShowWindow(hWnd, SW_MAXIMIZE);
			}
			else
			{
				// Restore all the window decorators.
				::SetWindowLong(hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

				SetWindowPos(hWnd, HWND_NOTOPMOST,
				             windowRect.left,
				             windowRect.top,
				             windowRect.right - windowRect.left,
				             windowRect.bottom - windowRect.top,
				             SWP_FRAMECHANGED | SWP_NOACTIVATE);

				ShowWindow(hWnd, SW_NORMAL);
			}
		}
	}


	void Window::Show()
	{
		ShowWindow(hWnd, SW_SHOW);
	}

	void Window::Hide()
	{
		ShowWindow(hWnd, SW_HIDE);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE Window::GetCurrentBackBufferView() const
	{
		return rtvDescriptorHeap.GetCPUHandle(currentBackBufferIndex);
	}

	GTexture& Window::GetCurrentBackBuffer() 
	{
		return backBuffers[currentBackBufferIndex];
	}

	void Window::SetHeight(int height)
	{
		this->height = height;
	}

	void Window::SetWidth(int width)
	{
		this->width = width;
	}

	D3D12_VIEWPORT Window::GetViewPort() const
	{
		return screenViewport;
	}

	D3D12_RECT Window::GetRect() const
	{
		return scissorRect;
	}

	UINT Window::GetCurrentBackBufferIndex() const
	{
		return currentBackBufferIndex;
	}

	UINT Window::Present()
	{
		UINT syncInterval = vSync ? 1 : 0;
		UINT presentFlags = isTearingSupported && !vSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
		ThrowIfFailed(swapChain->Present(syncInterval, presentFlags));
		currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

		return currentBackBufferIndex;
	}


	void Window::Initialize()
	{
		D3DApp& app = D3DApp::GetApp();

		isTearingSupported = app.IsTearingSupported();

		swapChain = CreateSwapChain();


		for (int i = 0; i < BufferCount; ++i)
		{
			backBuffers.push_back(GTexture(windowName + L" Backbuffer[" + std::to_wstring(i) + L"]", TextureUsage::RenderTarget));
		}

		auto queue = D3DApp::GetApp().GetCommandQueue();
		queue->Flush();
	}

	Window::Window(WNDCLASS hwnd, const std::wstring& windowName, int clientWidth, int clientHeight, bool vSync)
		: windowClass(hwnd)
		  , windowName(windowName)
		  , width(clientWidth)
		  , height(clientHeight)
		  , vSync(vSync)
		  , fullscreen(false)
		  , frameCounter(0)
	{

		RECT R = { 0, 0, clientWidth, clientHeight };
		AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, FALSE);
		int width = R.right - R.left;
		int height = R.bottom - R.top;

		hWnd = CreateWindowW(windowClass.lpszClassName, windowName.c_str(),
			WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
			width,
			height,
			nullptr, nullptr, windowClass.hInstance, nullptr);


		assert(hWnd, "Could not create the render window.");	

		ShowWindow(hWnd, SW_SHOW);
		UpdateWindow(hWnd);

		RAWINPUTDEVICE rid;

		rid.usUsagePage = 0x01; //Mouse
		rid.usUsage = 0x02;
		rid.dwFlags = 0;
		rid.hwndTarget = nullptr;

		if (RegisterRawInputDevices(&rid, 1, sizeof(rid)) == FALSE)
		{
			OutputDebugString(L"Failed to register raw input devices.");
			OutputDebugString(std::to_wstring(GetLastError()).c_str());
			exit(-1);
		}


		Initialize();
	}

	Window::~Window()
	{	
		Destroy();
		assert(!hWnd && "Use Application::DestroyWindow before destruction.");
		::CloseHandle(m_SwapChainEvent);
	}

	void Window::OnUpdate()
	{
		updateClock.Tick();
	}

	void Window::CalculateFrameStats()
	{		

		frameCnt++;

		if ((renderClock.TotalTime() - timeElapsed) >= 1.0f)
		{
			
			
			float fps = static_cast<float>(frameCnt); // fps = frameCnt / 1
			float mspf = 1000.0f / fps;

			auto fpsStr = std::to_wstring(fps);
			std::wstring mspfStr = std::to_wstring(mspf);

			std::wstring windowText = windowName +
				L"    fps: " + fpsStr +
				L"   mspf: " + mspfStr;

			SetWindowText(hWnd, windowText.c_str());

			frameCnt = 0;
			timeElapsed += 1.0f;
		}
	}

	void Window::OnRender()
	{
		renderClock.Tick();

		CalculateFrameStats();

	}

	void Window::ResetTimer()
	{
		renderClock.Reset();
		updateClock.Reset();
	}

	void Window::OnResize()
	{
		assert(swapChain);

		auto queue = D3DApp::GetApp().GetCommandQueue();

		queue->Flush();

		for (int i = 0; i < BufferCount; ++i)
		{
			GResourceStateTracker::RemoveGlobalResourceState(backBuffers[i].GetD3D12Resource().Get());
			backBuffers[i].Reset();
		}
		
		for (int i = 0; i < BufferCount; ++i)
		{
			backBuffers[i].Reset();
		}

		ThrowIfFailed(swapChain->ResizeBuffers(
			BufferCount,
			width, height,
			backBufferFormat,
			isTearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

		currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();
		
		UpdateRenderTargets();
		

		screenViewport.TopLeftX = 0;
		screenViewport.TopLeftY = 0;
		screenViewport.Width = static_cast<float>(width);
		screenViewport.Height = static_cast<float>(height);
		screenViewport.MinDepth = 0.0f;
		screenViewport.MaxDepth = 1.0f;

		scissorRect = {0, 0, width, height};
	}

	ComPtr<IDXGISwapChain4> Window::GetSwapChain()
	{
		if (!swapChain)
			swapChain = CreateSwapChain();


		return swapChain;
	}

	ComPtr<IDXGISwapChain4> Window::CreateSwapChain()
	{
		ComPtr<IDXGISwapChain4> swapChain;


		auto& app = (D3DApp::GetApp());

		ComPtr<IDXGISwapChain4> dxgiSwapChain4;
		ComPtr<IDXGIFactory4> dxgiFactory4;
		UINT createFactoryFlags = 0;
#if defined(_DEBUG)
		createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

		ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = width;
		swapChainDesc.Height = height;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc = {1, 0};
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = BufferCount;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
		// It is recommended to always allow tearing if tearing support is available.
		swapChainDesc.Flags = isTearingSupported
			                      ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
			                      : DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		ID3D12CommandQueue* pCommandQueue = app.GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->GetD3D12CommandQueue().
		                                        Get();

		ComPtr<IDXGISwapChain1> swapChain1;
		ThrowIfFailed(dxgiFactory4->CreateSwapChainForHwnd(
			pCommandQueue,
			hWnd,
			&swapChainDesc,
			nullptr,
			nullptr,
			&swapChain1));

		// Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
		// will be handled manually.
		ThrowIfFailed(dxgiFactory4->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));

		ThrowIfFailed(swapChain1.As(&swapChain));

		m_SwapChainEvent = swapChain->GetFrameLatencyWaitableObject();
		currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

		return swapChain;
	}

	void Window::UpdateRenderTargets()
	{
		assert(swapChain);

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = GetSRGBFormat(backBufferFormat);
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

		for (int i = 0; i < BufferCount; ++i)
		{
			ComPtr<ID3D12Resource> backBuffer;
			ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
			GResourceStateTracker::AddGlobalResourceState(backBuffer.Get(), D3D12_RESOURCE_STATE_COMMON);
			
			backBuffers[i].SetD3D12Resource(backBuffer);
			backBuffers[i].CreateRenderTargetView(&rtvDesc, &rtvDescriptorHeap, i);
		}
	}
}
