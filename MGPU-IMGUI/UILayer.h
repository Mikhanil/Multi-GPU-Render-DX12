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
    bool imguiSrvDescriptorInUse = false;

    void SetupRenderBackends();
    void Initialize();

    friend void UILayer_ImGuiSrvAllocFn(ImGui_ImplDX12_InitInfo* info,
                                        D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
                                        D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle);
    friend void UILayer_ImGuiSrvFreeFn(ImGui_ImplDX12_InitInfo* info,
                                       D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle);

public:
    UILayer(const std::shared_ptr<GDevice>& device, HWND hwnd);

    ~UILayer();

    void CreateDeviceObject();
    void Invalidate();

    void Render(const std::shared_ptr<GCommandList>& cmdList) const;

    void ChangeDevice(const std::shared_ptr<GDevice>& device);

    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};
