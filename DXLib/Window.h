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
#include "Lazy.h"

using namespace Microsoft::WRL;

namespace DXLib
{
	class Window
	{
	public:

		HWND GetWindowHandle() const;

		void Destroy();
		D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const;

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


		ComPtr<ID3D12Resource> GetCurrentBackBuffer() const;

		ComPtr<ID3D12Resource> GetDepthStencilBuffer() const;

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

		virtual void OnResize();

		ComPtr<IDXGISwapChain4> GetSwapChain();

		void UpdateRenderTargetViews();


	private:

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
		DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		ComPtr<IDXGISwapChain4> CreateSwapChain();
		ComPtr<IDXGISwapChain4> swapChain;

		ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;

		ComPtr<ID3D12DescriptorHeap> depthStencilViewHeap;
		ComPtr<ID3D12Resource> depthStencilBuffer;
		ComPtr<ID3D12Resource> backBuffers[BufferCount];
		D3D12_VIEWPORT screenViewport;
		D3D12_RECT scissorRect;

		UINT rtvDescriptorSize;
		UINT dsvDescriptorSize;
		UINT currentBackBufferIndex;

		RECT windowRect;
		bool isTearingSupported;
	};
}
