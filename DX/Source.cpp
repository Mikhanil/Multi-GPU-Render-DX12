#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <d3dcompiler.h>
#include <directxmath.h>
#include "ErrorLogger.h"
#include "Engine.h"

#pragma comment (lib, "d3d12.lib")
#pragma comment (lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;


int WINAPI main(HINSTANCE hInstance,    //Main windows function
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nShowCmd)
{
	HRESULT hr = CoInitialize(NULL);
	if (FAILED(hr))
	{
		GameEngine::Logger::ErrorLogger::Log(hr, "Failed to call CoInitialize.");
		return -1;
	}

	GameEngine::Engine engine;
	if (engine.Initialize(hInstance, "Title", "MyWindowClass", 1200, 800))
	{
		while (engine.ProcessMessages() == true)
		{
			engine.Update();
			engine.RenderFrame();
		}
	}
}



