#include "pch.h"
#include "Window.h"

#include <cassert>


#include "d3dApp.h"
#include "d3dUtil.h"
#include "GCommandList.h"
#include "GDevice.h"
#include "GDeviceFactory.h"
#include "GResourceStateTracker.h"
#include "Lazy.h"

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

		for (auto&& back_buffer : backBuffers)
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

	float Window::AspectRatio() const
	{
		return static_cast<float>(width) / height;
	}

	GTexture& Window::GetBackBuffer(UINT i)
	{
		return backBuffers[i];
	}

	UINT Window::GetCurrentBackBufferIndex() const
	{
		return currentBackBufferIndex;
	}

	UINT Window::Present()
	{
		UINT syncInterval = vSync ? 1 : 0;
		UINT presentFlags =  GDeviceFactory::IsTearingSupport() && !vSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
		ThrowIfFailed(swapChain->Present(syncInterval, presentFlags));
		currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

		return currentBackBufferIndex;
	}


	void Window::Initialize()
	{
		swapChain = CreateSwapChain();


		for (int i = 0; i < globalCountFrameResources; ++i)
		{
			backBuffers.push_back(GTexture(windowName + L" Backbuffer[" + std::to_wstring(i) + L"]",
			                               TextureUsage::RenderTarget));
		}

		OnResize();
	}

	Window::Window(std::shared_ptr<GDevice> device, WNDCLASS hwnd, const std::wstring& windowName, int clientWidth, int clientHeight, bool vSync)
		: windowClass(hwnd) ,windowName(windowName)
		  , width(clientWidth)
		  , height(clientHeight)
		  , vSync(vSync)
		  , fullscreen(false)
		  , device(device)
	{
		RECT R = {0, 0, clientWidth, clientHeight};
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
		//::SetWindowLong(hWnd, GWL_STYLE, GetWindowLong(hWnd, GWL_STYLE) & ~WS_SIZEBOX);
		

		Initialize();
	}

	Window::~Window()
	{
		Destroy();
		assert(!hWnd && "Use Application::DestroyWindow before destruction.");
		CloseHandle(swapChainEvent);
	}

	void Window::OnUpdate()
	{
	
	}

	void Window::CalculateFrameStats()
	{
		
	}

	void Window::SetWindowTitle(std::wstring text) const
	{
		SetWindowText(hWnd, text.c_str());
	}
	
	void Window::OnRender()
	{
		
	}

	void Window::ResetTimer()
	{
		
	}

	void Window::OnResize()
	{
		assert(swapChain);

		device->Flush();

		for (int i = 0; i < globalCountFrameResources; ++i)
		{
			GResourceStateTracker::RemoveGlobalResourceState(backBuffers[i].GetD3D12Resource().Get());
			backBuffers[i].Reset();
		}

		for (int i = 0; i < globalCountFrameResources; ++i)
		{
			backBuffers[i].Reset();
		}

		DXGI_SWAP_CHAIN_DESC desc;

		swapChain->GetDesc(&desc);

		ThrowIfFailed(swapChain->ResizeBuffers(
			desc.BufferCount,
			width, height,
			desc.BufferDesc.Format,
			desc.Flags));

		currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

		for (int i = 0; i < globalCountFrameResources; ++i)
		{
			ComPtr<ID3D12Resource> backBuffer;
			ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
			GResourceStateTracker::AddGlobalResourceState(backBuffer.Get(), D3D12_RESOURCE_STATE_COMMON);

			backBuffers[i].SetD3D12Resource(device, backBuffer);
		}
	}

	ComPtr<IDXGISwapChain4> Window::GetSwapChain()
	{
		if (!swapChain)
			swapChain = CreateSwapChain();


		return swapChain;
	}

	ComPtr<IDXGISwapChain4> Window::CreateSwapChain()
	{
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
		swapChainDesc.Width = width;
		swapChainDesc.Height = height;
		swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.Stereo = FALSE;
		swapChainDesc.SampleDesc = {1, 0};
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = globalCountFrameResources;
		swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

		swapChain = GDeviceFactory::CreateSwapChain(device,swapChainDesc, hWnd);

		swapChainEvent = swapChain->GetFrameLatencyWaitableObject();
		currentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

		return swapChain;
	}
	
}
