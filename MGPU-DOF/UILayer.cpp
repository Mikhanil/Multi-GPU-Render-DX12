#include "UILayer.h"

#include <utility>

#include "GCommandList.h"
#include "GCommandQueue.h"
#include "GDescriptorHeap.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void UILayer_ImGuiSrvAllocFn(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
                             D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
{
    auto* layer = static_cast<UILayer*>(info->UserData);
    IM_ASSERT(layer != nullptr);

    for (uint32_t index = 0; index < layer->imguiSrvDescriptorUsage.size(); ++index)
    {
        if (layer->imguiSrvDescriptorUsage[index])
            continue;

        layer->imguiSrvDescriptorUsage[index] = true;
        *out_cpu_handle = layer->srvMemory.GetCPUHandle(index);
        *out_gpu_handle = layer->srvMemory.GetGPUHandle(index);
        return;
    }

    IM_ASSERT(false && "ImGui SRV descriptor heap exhausted");
    *out_cpu_handle = {};
    *out_gpu_handle = {};
}

void UILayer_ImGuiSrvFreeFn(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                            D3D12_GPU_DESCRIPTOR_HANDLE)
{
    auto* layer = static_cast<UILayer*>(info->UserData);
    IM_ASSERT(layer != nullptr);

    const auto base = layer->srvMemory.GetCPUHandle(0).ptr;
    const auto descriptorSize = static_cast<SIZE_T>(
        layer->srvMemory.GetDescriptorHeap()->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
    IM_ASSERT(descriptorSize > 0);
    IM_ASSERT(cpu_handle.ptr >= base);

    const auto offset = cpu_handle.ptr - base;
    IM_ASSERT(offset % descriptorSize == 0);
    const auto index = static_cast<size_t>(offset / descriptorSize);
    IM_ASSERT(index < layer->imguiSrvDescriptorUsage.size() && layer->imguiSrvDescriptorUsage[index]);

    layer->imguiSrvDescriptorUsage[index] = false;
}

LRESULT UILayer::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}

void UILayer::SetupRenderBackends()
{
    srvMemory = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, ImGuiSrvDescriptorCount);
    imguiSrvDescriptorUsage.fill(false);

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

    ImGui::StyleColorsDark();
}

void UILayer::Initialize()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
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

UILayer::UILayer(const std::shared_ptr<GDevice>& device, const HWND hwnd) : hwnd(hwnd), device(device)
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
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    this->device = device;
    SetupRenderBackends();
    CreateDeviceObject();
}

void UILayer::Render(const std::shared_ptr<GCommandList>& cmdList) const
{
    cmdList->SetDescriptorsHeap(&srvMemory);
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList->GetGraphicsCommandList().Get());
}

void UILayer::Update()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}
