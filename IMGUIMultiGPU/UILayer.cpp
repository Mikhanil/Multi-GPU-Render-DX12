#include "UILayer.h"

#include <utility>


#include "GCommandList.h"
#include "GDescriptorHeap.h"

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT UILayer::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
		return true;
}


void UILayer::SetupRenderBackends()
{
	srvMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, globalCountFrameResources);

	
	// Setup Platform/Renderer backends
	ImGui_ImplWin32_Init(this->hwnd);
	ImGui_ImplDX12_Init(device->GetDXDevice().Get(), 1,
	                    DXGI_FORMAT_R8G8B8A8_UNORM, srvMemory.GetDescriptorHeap()->GetDirectxHeap(), srvMemory.GetCPUHandle(), srvMemory.GetGPUHandle());
	
	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsClassic();
}

void UILayer::Initialize()
{

	
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	//

	SetupRenderBackends();

	
}

void UILayer::CreateDeviceObject()
{
	ImGui_ImplDX12_CreateDeviceObjects();
}

void UILayer::Invalidate()
{
	ImGui_ImplDX12_InvalidateDeviceObjects();
}

UILayer::UILayer(std::shared_ptr<GDevice> device , const HWND hwnd):  hwnd(hwnd), device((device))
{
	Initialize();
	CreateDeviceObject();
}

UILayer::~UILayer()
{
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void UILayer::ChangeDevice(std::shared_ptr<GDevice> device)
{
	if (this->device == device)
	{
		return;
	}
	this->device = device;

	Invalidate();
	SetupRenderBackends();
	CreateDeviceObject();
}


void UILayer::Render(const std::shared_ptr<GCommandList> cmdList)
{	
	cmdList->SetDescriptorsHeap(&srvMemory);
	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGui::ShowDemoWindow();
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList->GetGraphicsCommandList().Get());
}
