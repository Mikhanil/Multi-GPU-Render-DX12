#pragma once
#include "RenderWindow.h"
#include "Mouse/MouseInput.h"
#include "Keyboard/KeyboardInput.h"

namespace GameEngine
{	

	class WindowContainer
	{
	public:
		WindowContainer();
		LRESULT WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	protected:
		RenderWindow render_window;
		Input::KeyboardInput keyboard;
		Input::MouseInput mouse;
	};
	
}
