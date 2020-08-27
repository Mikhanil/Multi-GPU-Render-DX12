#pragma once
#include <d2d1_3.h>
#include <Windows.h>

#include <d3d12.h>
#include <dxgi1_5.h>
#include <memory>
#include <string>
#include <wrl.h>
#include "d3d11.h"
#include "d3d11on12.h"
#include "GameTimer.h"
#include "GMemory.h"
#include "Lazy.h"
#include "GTexture.h"

using namespace Microsoft::WRL;

namespace DXLib
{
	class Window
	{
	public:

		HWND GetWindowHandle() const;

		void Destroy();

		const std::wstring& GetWindowName() const;

		int GetClientWidth() const;
		int GetClientHeight() const;


		bool IsVSync() const;
		void SetVSync(bool vSync);


		bool IsFullScreen() const;
		void SetFullscreen(bool fullscreen);

		void Show();

		void Hide();

		UINT GetCurrentBackBufferIndex() const;

		UINT Present();
		void Initialize();


		D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferView() const;

		GMemory* GetRTVMemory()
		{
			return &rtvDescriptorHeap;
		}


		GTexture& GetCurrentBackBuffer();


		void SetHeight(int height);

		void SetWidth(int width);

		D3D12_VIEWPORT GetViewPort() const;

		D3D12_RECT GetRect() const;

		float AspectRatio() const
		{
			return static_cast<float>(width) / height;
		}

	protected:

		friend class D3DApp;

		Window() = delete;
		Window(WNDCLASS hwnd, const std::wstring& windowName, int clientWidth, int clientHeight, bool vSync);
		virtual ~Window();


		virtual void OnUpdate();
		void CalculateFrameStats();
		virtual void OnRender();
		void ResetTimer();

		virtual void OnResize();

		ComPtr<IDXGISwapChain4> GetSwapChain();

		void UpdateRenderTargets();


	private:

		int frameCnt = 0;
		float timeElapsed = 0.0f;
		HANDLE m_SwapChainEvent;


		Window(const Window& copy) = delete;
		Window& operator=(const Window& other) = delete;

		WNDCLASS windowClass;
		HWND hWnd;
		std::wstring windowName;
		int width;
		int height;
		bool vSync;
		bool fullscreen;
		GameTimer updateClock;
		GameTimer renderClock;
		uint64_t frameCounter;


		// Number of swapchain back buffers.
		static const UINT BufferCount = 3;
		DXGI_FORMAT backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		ComPtr<IDXGISwapChain4> CreateSwapChain();
		ComPtr<IDXGISwapChain4> swapChain;		
	
		
		std::vector<GTexture> backBuffers;
		GMemory rtvDescriptorHeap = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, BufferCount);
		
		D3D12_VIEWPORT screenViewport;
		D3D12_RECT scissorRect;

		UINT currentBackBufferIndex;

		RECT windowRect;
		bool isTearingSupported;
	};
}
