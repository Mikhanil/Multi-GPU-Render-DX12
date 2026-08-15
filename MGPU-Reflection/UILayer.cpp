#include "UILayer.h"

#include "ReflectionRenderer.h"
#include "GCommandList.h"
#include "GCommandQueue.h"
#include "GDescriptorHeap.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace
{
std::string ToUtf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

const char* CaptureModeName(const ReflectionProbeCaptureMode mode)
{
    return mode == ReflectionProbeCaptureMode::FullDynamic
               ? "Full dynamic"
               : "Baked + dynamic overlay";
}

const char* UpdateModeName(const ReflectionProbeUpdateMode mode)
{
    switch (mode)
    {
    case ReflectionProbeUpdateMode::AllProbesPerFrame:
        return "All probes / frame";
    case ReflectionProbeUpdateMode::OneProbePerFrame:
        return "One probe / GPU / frame";
    case ReflectionProbeUpdateMode::OneFacePerFrame:
        return "One face / GPU / frame";
    }
    return "Unknown";
}

const char* SsrModeName(const SsrExecutionMode mode)
{
    return mode == SsrExecutionMode::Secondary ? "Secondary GPU" : "Primary GPU";
}
}

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

    const ImVec2 benchmarkPosition(12.0f, 12.0f);
    const ImVec2 benchmarkSize(500.0f, 0.0f);
    const ImVec2 controlsSize(350.0f, 0.0f);
    constexpr ImGuiWindowFlags pinnedWindowFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    const float frameTimeMs = ImGui::GetIO().DeltaTime * 1000.0f;
    frameTimeHistory[frameTimeHistoryOffset] = frameTimeMs;
    frameTimeHistoryOffset = (frameTimeHistoryOffset + 1) % FrameTimeHistorySize;
    if (frameTimeHistoryCount < FrameTimeHistorySize)
        ++frameTimeHistoryCount;

    ImGui::SetNextWindowPos(benchmarkPosition, ImGuiCond_Always);
    ImGui::SetNextWindowSize(benchmarkSize, ImGuiCond_Always);
    ImGui::Begin("Benchmark status", nullptr, pinnedWindowFlags | ImGuiWindowFlags_AlwaysAutoResize);
    const auto& benchmark = renderer.GetBenchmarkDisplayState();
    if (benchmark.TotalStates == 0)
    {
        ImGui::TextDisabled("Benchmark is not running");
    }
    else
    {
        if (cachedPrimaryGpuName != benchmark.PrimaryGpuName)
        {
            cachedPrimaryGpuName = benchmark.PrimaryGpuName;
            cachedPrimaryGpuNameUtf8 = ToUtf8(cachedPrimaryGpuName);
        }
        if (cachedSecondaryGpuName != benchmark.SecondaryGpuName)
        {
            cachedSecondaryGpuName = benchmark.SecondaryGpuName;
            cachedSecondaryGpuNameUtf8 = ToUtf8(cachedSecondaryGpuName);
        }
        const auto& configuration = renderer.GetReflectionProbeConfiguration();
        ImGui::Text("Capture: %s", CaptureModeName(configuration.CaptureMode));
        ImGui::Text("Update: %s", UpdateModeName(configuration.UpdateMode));
        ImGui::Text("Distribution: Primary %u / Secondary %u",
                    configuration.PrimaryProbeCount,
                    Common::Scene::ReflectionProbeCount - configuration.PrimaryProbeCount);
        ImGui::Text("SSR: %s", SsrModeName(configuration.SsrMode));
        ImGui::Separator();
        ImGui::Text("Primary GPU: %s", cachedPrimaryGpuNameUtf8.c_str());
        ImGui::Text("Secondary GPU: %s", cachedSecondaryGpuNameUtf8.empty() ? "not used" : cachedSecondaryGpuNameUtf8.c_str());
        ImGui::Text("State: %u/%u", benchmark.CurrentState, benchmark.TotalStates);
        ImGui::Text("Phase: %s", benchmark.IsSettling ? "prewarm" : "sampling");
        ImGui::Text("Remaining: %u s", benchmark.RemainingSeconds);
        ImGui::Separator();
        if (benchmark.HasStats)
        {
            ImGui::Text("Latest sample");
            ImGui::Text("FPS: %.2f", benchmark.LatestStats.fps);
            ImGui::Text("MSPF: %.2f", benchmark.LatestStats.mspf);
            ImGui::Text("Min FPS: %.2f", benchmark.LatestStats.minFps);
            ImGui::Text("Min MSPF: %.2f", benchmark.LatestStats.minMspf);
            ImGui::Text("Max FPS: %.2f", benchmark.LatestStats.maxFps);
            ImGui::Text("Max MSPF: %.2f", benchmark.LatestStats.maxMspf);
        }
        else
        {
            ImGui::TextDisabled("Waiting for the first statistics sample...");
        }
    }
    ImGui::Separator();
    ImGui::Text("Frame time: %.3f ms", frameTimeMs);
    ImGui::PushStyleColor(ImGuiCol_PlotLines, ImVec4(1.0f, 0.12f, 0.08f, 1.0f));
    const float plotWidth = ImGui::GetContentRegionAvail().x;
    ImGui::PlotLines("##FrameTimePlot", frameTimeHistory.data(), static_cast<int>(frameTimeHistoryCount),
                     frameTimeHistoryCount == FrameTimeHistorySize ? static_cast<int>(frameTimeHistoryOffset) : 0,
                     nullptr, FLT_MAX, FLT_MAX, ImVec2(plotWidth, 110.0f));
    ImGui::PopStyleColor();
    ImGui::TextUnformatted("Frame time (ms)");
    const float benchmarkWindowHeight = ImGui::GetWindowSize().y;
    ImGui::End();

    const ImVec2 controlsPosition(benchmarkPosition.x, benchmarkPosition.y + benchmarkWindowHeight + 12.0f);
    ImGui::SetNextWindowPos(controlsPosition, ImGuiCond_Always);
    ImGui::SetNextWindowSize(controlsSize, ImGuiCond_Always);
    ImGui::Begin("MGPU-Reflection controls", nullptr, pinnedWindowFlags);
    bool primaryOnly = renderer.GetUseOnlyPrime();
    if (ImGui::Checkbox("Reflection probes on primary only", &primaryOnly))
    {
        renderer.Flush();
        renderer.SetUseOnlyPrime(primaryOnly);
    }
    ImGui::Text("Probe mode: %s", renderer.GetUseOnlyPrime() ? "Primary" : "MGPU");
    auto ssrMode = renderer.GetReflectionProbeConfiguration().SsrMode;
    int ssrAdapter = ssrMode == SsrExecutionMode::Secondary ? 1 : 0;
    if (!renderer.HasSecondaryAdapter()) ImGui::BeginDisabled();
    if (ImGui::RadioButton("SSR on primary GPU", ssrAdapter == 0))
    {
        renderer.SetSsrExecutionMode(SsrExecutionMode::Primary);
    }
    if (ImGui::RadioButton("SSR on secondary GPU", ssrAdapter == 1))
    {
        renderer.SetSsrExecutionMode(SsrExecutionMode::Secondary);
    }
    if (!renderer.HasSecondaryAdapter()) ImGui::EndDisabled();
    ImGui::Checkbox("Show ImGui demo", &showDemoWindow);
    ImGui::End();

    if (showDemoWindow)
    {
        ImGui::SetNextWindowPos(ImVec2(374.0f, 12.0f), ImGuiCond_Always);
        ImGui::ShowDemoWindow(&showDemoWindow);
    }
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
