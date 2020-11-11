#pragma once
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include "GDescriptor.h"
#include "GTexture.h"
#include "MemoryAllocator.h"
using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;

class UILayer
{
	GDescriptor srvMemory;	
	HWND hwnd;
	std::shared_ptr<GDevice> device;

	void SetupRenderBackends();
	void Initialize();
	

public:
	UILayer(std::shared_ptr<GDevice> device, const HWND hwnd);

	~UILayer();	

	void CreateDeviceObject();
	void Invalidate();
	
	void Render(const std::shared_ptr<GCommandList>& cmdList);

	void ChangeDevice(std::shared_ptr<GDevice> device);

	LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

