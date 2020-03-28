#include "RenderWindow.h"
#include "StringConverter.h"
#include "ErrorLogger.h"
#include "WindowContainer.h"
namespace GameEngine
{
	using namespace Logger;
	
	bool RenderWindow::Initialize(WindowContainer* pWindowContainer, HINSTANCE hInstance, std::string window_title, std::string window_class, int width,
		int height)
	{
		this->hInstance = hInstance;
		this->width = width;
		this->height = height;
		this->window_title = Utility::StringConverter::StringToWide(window_title);
		this->window_class = Utility::StringConverter::StringToWide(window_class);

		this->RegisterWindowClass();

		int centerScreenX = GetSystemMetrics(SM_CXSCREEN) / 2 - this->width / 2;
		int centerScreenY = GetSystemMetrics(SM_CYSCREEN) / 2 - this->height / 2;

		RECT wr; //Widow Rectangle
		wr.left = centerScreenX;
		wr.top = centerScreenY;
		wr.right = wr.left + this->width;
		wr.bottom = wr.top + this->height;
		AdjustWindowRect(&wr, WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU, FALSE);

		this->handle = CreateWindowEx(0,
		                              this->window_class.c_str(),
		                              this->window_title.c_str(),
		                              WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU,
		                              wr.left,
		                              wr.top,
		                              wr.right - wr.left,
		                              wr.bottom - wr.top,
		                              nullptr,
		                              nullptr,
		                              this->hInstance,
		                              pWindowContainer);

		if (this->handle == nullptr)
		{
			ErrorLogger::Log(GetLastError(), "CreateWindowEX Failed for window ");
			return false;
		}

		// Bring the window up on the screen and set it as main focus.
		ShowWindow(this->handle, SW_SHOW);
		SetForegroundWindow(this->handle);
		SetFocus(this->handle);

		return true;
	}

	bool RenderWindow::ProcessMessages()
	{
		// Handle the windows messages.
		MSG msg;
		ZeroMemory(&msg, sizeof(MSG)); // Initialize the message structure.

		while (PeekMessage(&msg, //Where to store message (if one exists) See: https://msdn.microsoft.com/en-us/library/windows/desktop/ms644943(v=vs.85).aspx
			this->handle, //Handle to window we are checking messages for
			0,    //Minimum Filter Msg Value - We are not filtering for specific messages, but the min/max could be used to filter only mouse messages for example.
			0,    //Maximum Filter Msg Value
			PM_REMOVE))//Remove message after capturing it via PeekMessage. For more argument options, see: https://msdn.microsoft.com/en-us/library/windows/desktop/ms644943(v=vs.85).aspx
		{
			TranslateMessage(&msg); //Translate message from virtual key messages into character messages so we can dispatch the message. See: https://msdn.microsoft.com/en-us/library/windows/desktop/ms644955(v=vs.85).aspx
			DispatchMessage(&msg); //Dispatch message to our Window Proc for this window. See: https://msdn.microsoft.com/en-us/library/windows/desktop/ms644934(v=vs.85).aspx
		}

		// Check if the window was closed
		if (msg.message == WM_NULL)
		{
			if (!IsWindow(this->handle))
			{
				this->handle = nullptr; //Message processing loop takes care of destroying this window
				UnregisterClass(this->window_class.c_str(), this->hInstance);
				return false;
			}
		}

		return true;
	}

	HWND RenderWindow::GetHWND() const
	{
		return this->handle;
	}

	RenderWindow::~RenderWindow()
	{
		if (this->handle != nullptr)
		{
			UnregisterClass(this->window_class.c_str(), this->hInstance);
			DestroyWindow(handle);
		}
	}

	LRESULT CALLBACK HandleMsgRedirect(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
			// All other messages
		case WM_CLOSE:
			DestroyWindow(hwnd);
			return 0;

		default:
		{
			// retrieve ptr to window class
			const auto pWindow = reinterpret_cast<WindowContainer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
			// forward message to window class handler
			return pWindow->WindowProc(hwnd, uMsg, wParam, lParam);
		}
		}
	}

	LRESULT CALLBACK HandleMessageSetup(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		switch (uMsg)
		{
		case WM_NCCREATE:
		{
			const CREATESTRUCTW* const pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
			auto pWindow = reinterpret_cast<WindowContainer*>(pCreate->lpCreateParams);
			if (pWindow == nullptr) //Sanity check
			{
				ErrorLogger::Log("Critical Error: Pointer to window container is null during WM_NCCREATE.");
				exit(-1);
			}
			SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pWindow));
			SetWindowLongPtr(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HandleMsgRedirect));
			return pWindow->WindowProc(hwnd, uMsg, wParam, lParam);
		}
		default:
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
		}
	}

	void RenderWindow::RegisterWindowClass() const
	{
		WNDCLASSEX wc;
		wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		wc.lpfnWndProc = HandleMessageSetup;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = this->hInstance;
		wc.hIcon = nullptr;
		wc.hIconSm = nullptr;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = nullptr;
		wc.lpszMenuName = nullptr;
		wc.lpszClassName = this->window_class.c_str();
		wc.cbSize = sizeof(WNDCLASSEX);
		RegisterClassEx(&wc);
	}
}
