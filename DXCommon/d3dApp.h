#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#endif

#include "GameTimer.h"

// Link necessary d3d12 libraries.
#pragma comment(lib,"d3dcompiler.lib")

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib,"dwrite.lib")
#pragma comment(lib,"d3d11.lib")

#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "RuntimeObject.lib")


#include <dxgi1_6.h>
#include "Camera.h"
#include "Lazy.h"
#include "GDevice.h"
#include "KeyboardDevice.h"
#include "Mousepad.h"



using Microsoft::WRL::ComPtr;

class GDevice;

namespace DXLib
{
	class Window;
	class GCommandQueue;

	class D3DApp
	{
	protected:


		KeyboardDevice keyboard;
		Mousepad mouse;
		std::shared_ptr<Camera> camera = nullptr;
		
		D3DApp(HINSTANCE hInstance);
		D3DApp(const D3DApp& rhs) = delete;
		D3DApp& operator=(const D3DApp& rhs) = delete;
		virtual ~D3DApp();

	public:

		KeyboardDevice* GetKeyboard();

		Mousepad* GetMouse();

		Camera* GetMainCamera() const;
		

		static void Destroy();

		std::shared_ptr<Window> CreateRenderWindow(std::shared_ptr<GDevice> device, const std::wstring& windowName, int clientWidth,
		                                           int clientHeight, bool vSync = true);

		void DestroyWindow(const std::wstring& windowName) const;

		static void DestroyWindow(std::shared_ptr<Window> window);

		static std::shared_ptr<Window> GetWindowByName(const std::wstring& windowName);


		static void Quit(int exitCode = 0);

		void virtual  Flush();

		GameTimer* GetTimer();

		static D3DApp& GetApp();

		HINSTANCE AppInst() const;
		std::shared_ptr<Window> MainWnd() const;
		float AspectRatio() const;

		bool Get4xMsaaState() const;
		void Set4xMsaaState(bool value);

		int virtual Run();

		virtual bool Initialize();
		virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

		uint64_t GetFrameCount() const
		{
			return frameCount;
		}

	protected:

		virtual void OnResize();
		virtual void Update(const GameTimer& gt) = 0;
		virtual void Draw(const GameTimer& gt) = 0;

	protected:


		WNDCLASS windowClass;
		std::shared_ptr<Window> MainWindow;

		std::wstring fpsStr;
		std::wstring mainWindowCaption = L"d3d App";


		static D3DApp* instance;
		HINSTANCE appInstance = nullptr;

		bool isAppPaused = false;
		bool isMinimized = false;
		bool isMaximized = false;
		bool isResizing = false;
		bool isFullscreen = false;


		bool isM4xMsaa = false;
		UINT m4xMsaaQuality = 0;


		uint64_t frameCount = 0;
		float timeElapsed = 0.0f;
		GameTimer timer;

		bool virtual InitMainWindow();


		bool InitDirect3D();


		void virtual CalculateFrameStats();

		void LogAdapters();
	};
}
