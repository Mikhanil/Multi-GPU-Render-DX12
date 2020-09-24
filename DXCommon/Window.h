#pragma once
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>
#include <memory>
#include <string>
#include <wrl.h>
#include "GameTimer.h"
#include "GMemory.h"
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

		GTexture& GetCurrentBackBuffer();


		void SetHeight(int height);

		void SetWidth(int width);


		float AspectRatio() const;

		GTexture& GetBackBuffer(UINT i);

	protected:

		friend class D3DApp;

		Window() = delete;
		Window(std::shared_ptr<GDevice> device,WNDCLASS hwnd, const std::wstring& windowName, int clientWidth, int clientHeight, bool vSync);
		virtual ~Window();


		virtual void OnUpdate();
		void CalculateFrameStats();
		virtual void OnRender();
		void ResetTimer();

		virtual void OnResize();

		ComPtr<IDXGISwapChain4> GetSwapChain();


	private:

		int frameCnt = 0;
		float timeElapsed = 0.0f;
		HANDLE swapChainEvent;


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
		
		ComPtr<IDXGISwapChain4> CreateSwapChain();
		ComPtr<IDXGISwapChain4> swapChain;		
	
		std::shared_ptr<GDevice> device;
		std::vector<GTexture> backBuffers;
		

		UINT currentBackBufferIndex;

		RECT windowRect;
	};
}
