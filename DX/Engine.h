#pragma once
#include "WindowContainer.h"
#include "Timer.h"
#include <corecrt_math_defines.h>
#include "DXGraphics.h"

using namespace GameEngine::Utility;
using namespace GameEngine::Input;

namespace GameEngine
{
	class Engine :
		public WindowContainer
	{
	public:
		bool Initialize(HINSTANCE hInstance, std::string window_title, std::string window_class, int width, int height);
		bool ProcessMessages();
		void Update();
		void RenderFrame();
	private:
		Timer timer;
		Graphics::DXGraphics gfx;
	};
}



