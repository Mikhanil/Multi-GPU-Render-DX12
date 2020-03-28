#pragma once


#include "ErrorLogger.h"


namespace GameEngine
{
	class WindowContainer;


	class RenderWindow
	{
	public:
		bool Initialize(WindowContainer* pWindowContainer, HINSTANCE hInstance, std::string window_title, std::string window_class, int width, int
			height);
		bool ProcessMessages();
		HWND GetHWND() const;
		~RenderWindow();
	private:
		
		void RegisterWindowClass() const;
		
		HWND handle = nullptr; 
		HINSTANCE hInstance = nullptr;
		std::wstring window_title = L"";
		std::wstring window_class = L"";
		int width = 0;
		int height = 0;

	};

}