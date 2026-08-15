#include "UILayer.h"

#if defined(DEBUG) || defined(_DEBUG)

#include "ReflectionRenderer.h"
#include "GCommandList.h"
#include "GCommandQueue.h"
#include "GDescriptorHeap.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

void ReflectionUiAllocateDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                                   D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
{
    auto& layer = *static_cast<UILayer*>(info->UserData);
    for (UINT index = 0; index < UILayer::DescriptorCount; ++index)
        if (!layer.descriptorInUse[index])
        {
            layer.descriptorInUse[index] = true;
            *cpu = layer.descriptors.GetCPUHandle(index);
            *gpu = layer.descriptors.GetGPUHandle(index);
            return;
    }
    IM_ASSERT(false && "MGPU-Reflection ImGui descriptor heap exhausted");
    *cpu = {};
    *gpu = {};
}

void ReflectionUiFreeDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                               D3D12_GPU_DESCRIPTOR_HANDLE)
{
    auto& layer = *static_cast<UILayer*>(info->UserData);
    const auto base = layer.descriptors.GetCPUHandle(0).ptr;
    const auto increment = layer.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    IM_ASSERT(increment > 0);
    IM_ASSERT(cpu.ptr >= base);
    const auto offset = cpu.ptr - base;
    IM_ASSERT(offset % increment == 0);
    const UINT index = static_cast<UINT>(offset / increment);
    IM_ASSERT(index < UILayer::DescriptorCount && layer.descriptorInUse[index]);
    layer.descriptorInUse[index] = false;
}

UILayer::UILayer(const std::shared_ptr<PEPEngine::Graphics::GDevice>& deviceValue, const HWND hwndValue,
                 const DXGI_FORMAT renderTargetFormat, ReflectionRenderer& rendererValue)
    : device(deviceValue), renderer(rendererValue)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    descriptors = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, DescriptorCount);
    ImGui_ImplWin32_Init(hwndValue);
    ImGui_ImplDX12_InitInfo info{};
    info.Device = device->GetDXDevice().Get();
    info.CommandQueue = device->GetCommandQueue()->GetD3D12CommandQueue().Get();
    info.NumFramesInFlight = globalCountFrameResources;
    info.RTVFormat = renderTargetFormat;
    info.UserData = this;
    info.SrvDescriptorHeap = descriptors.GetDescriptorHeap()->GetDirectxHeap();
    info.SrvDescriptorAllocFn = ReflectionUiAllocateDescriptor;
    info.SrvDescriptorFreeFn = ReflectionUiFreeDescriptor;
    ImGui_ImplDX12_Init(&info);
    ImGui::StyleColorsDark();
}

UILayer::~UILayer()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void UILayer::Update()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGui::Begin("MGPU-Reflection");
    bool primaryOnly = renderer.GetUseOnlyPrime();
    if (ImGui::Checkbox("Primary GPU only", &primaryOnly))
    {
        renderer.Flush();
        renderer.SetUseOnlyPrime(primaryOnly);
    }
    ImGui::Text("Mode: %s", renderer.GetUseOnlyPrime() ? "Primary" : "MGPU");
    ImGui::End();
}

void UILayer::Render(const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList) const
{
    commandList->SetDescriptorsHeap(&descriptors);
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList->GetGraphicsCommandList().Get());
}

void UILayer::Invalidate() { ImGui_ImplDX12_InvalidateDeviceObjects(); }
void UILayer::CreateDeviceObjects() { ImGui_ImplDX12_CreateDeviceObjects(); }
void UILayer::ForwardMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) const
{ ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam); }

bool UILayer::WantsMouseCapture() const { return ImGui::GetIO().WantCaptureMouse; }
bool UILayer::WantsKeyboardCapture() const { return ImGui::GetIO().WantCaptureKeyboard; }

#endif
