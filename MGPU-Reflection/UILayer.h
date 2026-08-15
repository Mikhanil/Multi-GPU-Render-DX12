#pragma once

#include <array>
#include <memory>
#include <string>

#include "GDescriptor.h"

struct ImGui_ImplDX12_InitInfo;

namespace PEPEngine::Graphics { class GCommandList; class GDevice; }

class ReflectionRenderer;

class UILayer final
{
    static constexpr UINT DescriptorCount = 16;
    static constexpr size_t FrameTimeHistorySize = 120;

public:
    UILayer(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device, HWND hwnd,
            DXGI_FORMAT renderTargetFormat, ReflectionRenderer& renderer);
    ~UILayer();
    UILayer(const UILayer&) = delete;
    UILayer& operator=(const UILayer&) = delete;

    void Update();
    void Render(const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList) const;
    void Invalidate();
    void CreateDeviceObjects();
    void ForwardMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) const;
    bool WantsMouseCapture() const;
    bool WantsKeyboardCapture() const;

private:
    friend void ReflectionUiAllocateDescriptor(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE*, D3D12_GPU_DESCRIPTOR_HANDLE*);
    friend void ReflectionUiFreeDescriptor(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE);

    PEPEngine::Graphics::GDescriptor descriptors;
    std::array<bool, DescriptorCount> descriptorInUse{};
    std::shared_ptr<PEPEngine::Graphics::GDevice> device;
    ReflectionRenderer& renderer;
    bool showDemoWindow = true;
    std::wstring cachedPrimaryGpuName;
    std::wstring cachedSecondaryGpuName;
    std::string cachedPrimaryGpuNameUtf8;
    std::string cachedSecondaryGpuNameUtf8;
    std::array<float, FrameTimeHistorySize> frameTimeHistory{};
    size_t frameTimeHistoryOffset = 0;
    size_t frameTimeHistoryCount = 0;
};
