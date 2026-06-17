#include "UILayer.h"

#include <utility>


#include "GCommandList.h"
#include "GCommandQueue.h"
#include "GDescriptorHeap.h"

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void UILayer_ImGuiSrvAllocFn(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
                             D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
{
    auto* layer = static_cast<UILayer*>(info->UserData);
    IM_ASSERT(layer != nullptr && !layer->imguiSrvDescriptorInUse);
    layer->imguiSrvDescriptorInUse = true;
    *out_cpu_handle = layer->srvMemory.GetCPUHandle(0);
    *out_gpu_handle = layer->srvMemory.GetGPUHandle(0);
}

void UILayer_ImGuiSrvFreeFn(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE,
                            D3D12_GPU_DESCRIPTOR_HANDLE)
{
    auto* layer = static_cast<UILayer*>(info->UserData);
    IM_ASSERT(layer != nullptr && layer->imguiSrvDescriptorInUse);
    layer->imguiSrvDescriptorInUse = false;
}

LRESULT UILayer::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    return 0;
}


void UILayer::SetupRenderBackends()
{
    srvMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    imguiSrvDescriptorInUse = false;


    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(this->hwnd);
    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = device->GetDXDevice().Get();
    initInfo.CommandQueue = device->GetCommandQueue()->GetD3D12CommandQueue().Get();
    initInfo.NumFramesInFlight = globalCountFrameResources;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.UserData = this;
    initInfo.SrvDescriptorHeap = srvMemory.GetDescriptorHeap()->GetDirectxHeap();
    initInfo.SrvDescriptorAllocFn = UILayer_ImGuiSrvAllocFn;
    initInfo.SrvDescriptorFreeFn = UILayer_ImGuiSrvFreeFn;
    ImGui_ImplDX12_Init(&initInfo);

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();
}

void UILayer::Initialize()
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    //ImGuiIO& io = ImGui::GetIO(); (void)io;
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

UILayer::UILayer(const std::shared_ptr<GDevice>& device, const HWND hwnd) : hwnd(hwnd), device((device))
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

void UILayer::ChangeDevice(const std::shared_ptr<GDevice>& device)
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


void UILayer::Render(const std::shared_ptr<GCommandList>& cmdList) const
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
