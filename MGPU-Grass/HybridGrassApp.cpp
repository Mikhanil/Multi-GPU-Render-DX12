#include "HybridGrassApp.h"

#include "d3dUtil.h"
#include "imgui.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <locale>
#include <string>
#include <thread>
#include <DirectXPackedVector.h>
#include "CameraController.h"
#include "CrossAdapterParticleEmitter.h"
#include "GameObject.h"
#include "GCommandQueue.h"
#include "GDescriptorHeap.h"
#include "GDeviceFactory.h"
#include "GResourceStateTracker.h"
#include "GModel.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include "MathHelper.h"
#include "ModelRenderer.h"
#include "ParticleEmitter.h"
#include "Rotater.h"
#include "SkyBox.h"
#include "Transform.h"
#include "Window.h"
#include "d3dx12.h"

namespace
{
    using DirectX::SimpleMath::Vector2;
    using DirectX::SimpleMath::Vector3;
    using DirectX::SimpleMath::Vector4;
    using Microsoft::WRL::ComPtr;

    /// Cross-adapter ID3D12CommandQueue::Wait on shared fences often raises DXGI invalid-call (0x87A) in validation;
    /// waiting on the fence from the CPU preserves the dependency without using queue Wait.
    void WaitForSharedFenceValueCpu(const ComPtr<ID3D12Fence>& fence, UINT64 value)
    {
        if (!fence)
            return;
        if (fence->GetCompletedValue() >= value)
            return;

        static HANDLE s_completionEvent = nullptr;
        if (!s_completionEvent)
        {
            s_completionEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!s_completionEvent)
                return;
        }

        if (FAILED(fence->SetEventOnCompletion(value, s_completionEvent)))
            return;

        WaitForSingleObject(s_completionEvent, INFINITE);
    }

    // Matches `SampleWindGradient` in GrassDraw.hlsl / ComputeGrass.hlsl:
    // radial gust + optional directional bias from WindDirectionData.xy (blend in z).
    Vector2 SampleWindGradientHlslXZ(
        const Vector3& worldPos,
        uint32_t originCount,
        const std::array<Vector4, 4>& windOriginData,
        const std::array<Vector4, 4>& windDirectionData,
        const float windMapFalloff,
        Vector2 /*fallbackDirXZ*/)
    {
        const uint32_t count = std::min(originCount, 4u);

        if (count == 0)
            return Vector2::Zero;

        Vector2 accum = Vector2::Zero;

        for (uint32_t i = 0; i < count; ++i)
        {
            const Vector4 originPacked = windOriginData[i];
            const Vector4 dirPacked = windDirectionData[i];
            const Vector3 origin = Vector3(originPacked.x, originPacked.y, originPacked.z);
            const float radius = std::max(1.0f, originPacked.w);
            const float strength = std::max(0.0f, dirPacked.w);

            const float dx = worldPos.x - origin.x;
            const float dz = worldPos.z - origin.z;
            const float d = sqrtf(dx * dx + dz * dz);
            if (strength <= 1e-8f)
                continue;

            const float radialMask = std::clamp(1.f - d / std::max(radius, 1e-4f), 0.f, 1.f);
            const Vector2 packedDir(dirPacked.x, dirPacked.y);
            const float dirLen = packedDir.Length();
            const float dirBlend = dirLen > 1e-6f ? std::clamp(dirPacked.z, 0.0f, 1.0f) : 0.0f;
            Vector2 radialDir;
            const float dn = d;
            const float eps =
                std::max(0.06f * std::max(radius, 1.f), 0.5f);
            if (dn <= eps)
            {
                const float offsLen2 = dx * dx + dz * dz;
                if (offsLen2 < 1e-8f)
                    radialDir = Vector2(1.f, 0.f);
                else
                    radialDir = Vector2(dx, dz) * (1.f / std::max(dn, 1e-4f));
            }
            else
                radialDir = Vector2(dx, dz) * (1.f / dn);

            const Vector2 directionalDir = dirLen > 1e-6f ? (packedDir / dirLen) : radialDir;
            const float directionalMask = 1.0f;
            const float mask = radialMask + (directionalMask - radialMask) * dirBlend;
            if (mask <= 1e-6f)
                continue;

            const float radialWeight = powf(std::max(radialMask, 1e-5f), std::max(0.1f, windMapFalloff)) * strength;
            const float directionalWeight = directionalMask * strength;
            const float w = radialWeight + (directionalWeight - radialWeight) * dirBlend;

            Vector2 flowDir = radialDir + (directionalDir - radialDir) * dirBlend;
            const float flowLen = flowDir.Length();
            if (flowLen > 1e-6f)
                flowDir /= flowLen;
            else
                flowDir = radialDir;

            accum += flowDir * w;
        }

        const float lenAccum = sqrtf(std::max(accum.x * accum.x + accum.y * accum.y, 0.f));
        if (lenAccum > 1e-8f)
        {
            const float capped = std::min(8.0f, lenAccum * 2.85f);
            accum *= (capped / lenAccum);
            return accum;
        }

        return Vector2::Zero;
    }

    float HalfBitsToFloat(const uint16_t bits)
    {
        union Bits
        {
            uint16_t u;
            DirectX::PackedVector::HALF h;
        };
        Bits b{};
        b.u = bits;
        return static_cast<float>(b.h);
    }

    Vector2 SampleFluidRg16Nearest(const UINT8* row0, const UINT rowPitchBytes, const UINT grid, const int ix, const int iy)
    {
        const int gx = std::clamp(ix, 0, static_cast<int>(grid) - 1);
        const int gy = std::clamp(iy, 0, static_cast<int>(grid) - 1);
        const UINT8* p =
            row0 + static_cast<size_t>(gy) * rowPitchBytes + static_cast<size_t>(gx) * sizeof(uint16_t) * 2;
        uint16_t hx = 0, hy = 0;
        std::memcpy(&hx, p, sizeof(uint16_t));
        std::memcpy(&hy, p + sizeof(uint16_t), sizeof(uint16_t));
        return Vector2(HalfBitsToFloat(hx), HalfBitsToFloat(hy));
    }

    Vector2 SampleFluidRg16Bilinear(const UINT8* row0, const UINT rowPitchBytes, const UINT gridW,
                                   const UINT gridH, const float su, const float sv)
    {
        const UINT gridX = std::max(gridW, 1u);
        const UINT gridY = std::max(gridH, gridX);
        const float gx = su * static_cast<float>(gridX) - 0.5f;
        const float gy = sv * static_cast<float>(gridY) - 0.5f;

        const int x0 = static_cast<int>(std::floorf(gx));
        const int y0 = static_cast<int>(std::floorf(gy));

        const float tx = gx - static_cast<float>(x0);
        const float ty = gy - static_cast<float>(y0);

        Vector2 v00 = SampleFluidRg16Nearest(row0, rowPitchBytes, gridX, x0, y0);
        Vector2 v10 = SampleFluidRg16Nearest(row0, rowPitchBytes, gridX, x0 + 1, y0);
        Vector2 v01 = SampleFluidRg16Nearest(row0, rowPitchBytes, gridX, x0, y0 + 1);
        Vector2 v11 = SampleFluidRg16Nearest(row0, rowPitchBytes, gridX, x0 + 1, y0 + 1);

        const Vector2 lerpBottom = v00 + (v10 - v00) * tx;
        const Vector2 lerpTop = v01 + (v11 - v01) * tx;
        return lerpBottom + (lerpTop - lerpBottom) * ty;
    }

    Vector2 WorldPosToFluidUv(const Vector3& worldPos, const Vector4& windFieldWorldParams)
    {
        const float he = std::max(windFieldWorldParams.z, 1e-4f);
        const float su =
            (worldPos.x - windFieldWorldParams.x) / (2.0f * he) + 0.5f;
        const float sv =
            (worldPos.z - windFieldWorldParams.y) / (2.0f * he) + 0.5f;
        return Vector2(std::clamp(su, 0.0f, 1.0f), std::clamp(sv, 0.0f, 1.0f));
    }

    Vector2 WorldPosToGrassPatchUv(const Vector3& worldPos, const float worldSize,
                                 const Transform* grassTransform)
    {
        Vector3 localPos = worldPos;
        if (grassTransform != nullptr)
        {
            const Matrix invWorld = grassTransform->GetWorldMatrix().Invert();
            const Vector4 h = Vector4::Transform(
                Vector4(worldPos.x, worldPos.y, worldPos.z, 1.0f), invWorld);
            localPos = Vector3(h.x, h.y, h.z);
        }
        const float halfGrass = std::max(worldSize * 0.5f, 1e-4f);
        return Vector2(
            std::clamp((localPos.x + halfGrass) / (2.0f * halfGrass), 0.0f, 1.0f),
            std::clamp((localPos.z + halfGrass) / (2.0f * halfGrass), 0.0f, 1.0f));
    }

    Vector2 SampleLod0DebugGradientWindLocal(const Vector3& grassLocalPos, const float worldSize,
                                             const float minWorldSpeed, const float maxWorldSpeed,
                                             const int axis, const Vector2& flowDir)
    {
        const float halfGrass = std::max(worldSize * 0.5f, 1e-4f);
        const float u = std::clamp((grassLocalPos.x + halfGrass) / (2.0f * halfGrass), 0.0f, 1.0f);
        const float v = std::clamp((grassLocalPos.z + halfGrass) / (2.0f * halfGrass), 0.0f, 1.0f);
        const float t = (axis == 0) ? u : v;
        const float mag = minWorldSpeed + (maxWorldSpeed - minWorldSpeed) * t;
        Vector2 dir = flowDir;
        if (dir.LengthSquared() < 1e-8f)
            dir = Vector2(1.0f, 0.0f);
        dir.Normalize();
        return dir * std::max(mag, 0.0f);
    }

    Vector2 SampleLod0DebugGradientWindFromNormalizedUv(const float u, const float v,
                                                       const float minWorldSpeed,
                                                       const float maxWorldSpeed, const int axis)
    {
        const float t = (axis == 0) ? u : v;
        const float mag = minWorldSpeed + (maxWorldSpeed - minWorldSpeed) * std::clamp(t, 0.0f, 1.0f);
        return Vector2(std::max(mag, 0.0f), 0.0f);
    }

    float SampleScalarBilinear(const std::vector<float>& buf, const UINT w, const UINT h,
                               const float su, const float sv)
    {
        if (buf.empty() || w == 0 || h == 0)
            return 0.0f;
        const float gx = su * static_cast<float>(w) - 0.5f;
        const float gy = sv * static_cast<float>(h) - 0.5f;
        const int x0 = static_cast<int>(std::floorf(gx));
        const int y0 = static_cast<int>(std::floorf(gy));
        const float tx = gx - static_cast<float>(x0);
        const float ty = gy - static_cast<float>(y0);
        auto at = [&](int x, int y) -> float
        {
            x = std::clamp(x, 0, static_cast<int>(w) - 1);
            y = std::clamp(y, 0, static_cast<int>(h) - 1);
            return buf[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)];
        };
        const float v00 = at(x0, y0);
        const float v10 = at(x0 + 1, y0);
        const float v01 = at(x0, y0 + 1);
        const float v11 = at(x0 + 1, y0 + 1);
        const float b = v00 + (v10 - v00) * tx;
        const float t = v01 + (v11 - v01) * tx;
        return b + (t - b) * ty;
    }

    float PreviewWallMask(const float su, const float sv, const bool wallEnabled,
                          const float wallU, const float wallV, const float /*wallAngleRad*/,
                          const float radius, const float /*halfWidth*/)
    {
        if (!wallEnabled)
            return 0.0f;
        const float dx = su - wallU;
        const float dy = sv - wallV;
        const float r = std::max(radius, 0.01f);
        return (dx * dx + dy * dy) < (r * r) ? 1.0f : 0.0f;
    }

    Vector3 WindVectorToPreviewColorAbs(const Vector2& wind, const float visScale)
    {
        // Shadertoy-like preview: abs(velocity) * scale (no signed direction hue mapping).
        return Vector3(std::abs(wind.x), std::abs(wind.y), 0.0f) * visScale;
    }

    Vector3 WindVectorToPreviewColorSigned(const Vector2& wind, const float visScale)
    {
        const float mag = wind.Length();
        if (mag <= 1e-6f)
            return Vector3::Zero;
        float vis = std::clamp(mag * visScale, 0.0f, 1.0f);
        vis = std::pow(vis, 0.58f);
        const float angle = std::atan2(wind.y, wind.x);
        const float hue = angle * (0.5f / 3.1415926535f) + 0.5f;
        const float h6 = hue * 6.0f;
        const float x = 1.0f - std::abs(std::fmod(h6, 2.0f) - 1.0f);
        if (h6 < 1.0f)
            return Vector3(vis, x * vis, 0.0f);
        if (h6 < 2.0f)
            return Vector3(x * vis, vis, 0.0f);
        if (h6 < 3.0f)
            return Vector3(0.0f, vis, x * vis);
        if (h6 < 4.0f)
            return Vector3(0.0f, x * vis, vis);
        if (h6 < 5.0f)
            return Vector3(x * vis, 0.0f, vis);
        return Vector3(vis, 0.0f, x * vis);
    }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void HybridGrassApp_ImGuiSrvAllocFn(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
                                    D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle)
{
    auto* app = static_cast<HybridGrassApp*>(info->UserData);
    IM_ASSERT(app != nullptr && !app->imguiFontDescriptorInUse);
    app->imguiFontDescriptorInUse = true;
    *out_cpu_handle = app->imguiSrvDescriptors.GetCPUHandle(0);
    *out_gpu_handle = app->imguiSrvDescriptors.GetGPUHandle(0);
}

void HybridGrassApp_ImGuiSrvFreeFn(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE,
                                   D3D12_GPU_DESCRIPTOR_HANDLE)
{
    auto* app = static_cast<HybridGrassApp*>(info->UserData);
    IM_ASSERT(app != nullptr && app->imguiFontDescriptorInUse);
    app->imguiFontDescriptorInUse = false;
}

namespace
{
    const char* IntegratedDiscreteLabel(const DXGI_ADAPTER_DESC3& d)
    {
        constexpr UINT64 halfGb = 512ull * 1024 * 1024;
        if (d.DedicatedVideoMemory >= halfGb)
            return "Discrete-class (high dedicated VRAM)";
        if (d.DedicatedVideoMemory == 0)
            return "Integrated-class (no dedicated VRAM)";
        return "Hybrid / mixed (low dedicated VRAM)";
    }

    void ImGuiGpuSection(const char* sectionTitle, const char* workloadRole,
                         const std::shared_ptr<GDevice>& device,
                         const bool descValid, const DXGI_ADAPTER_DESC3& desc,
                         const UINT64 renderTimeTicks, const UINT64 computeTimeTicks)
    {
        ImGui::SeparatorText(sectionTitle);
        ImGui::Text("Workload: %s", workloadRole);
        ImGui::Text("D3D12 name: %ls", device->GetName().c_str());
        if (descValid)
        {
            ImGui::Text("DXGI description: %ls", desc.Description);
            ImGui::Text("%s", IntegratedDiscreteLabel(desc));
            ImGui::Text("Dedicated GPU memory: %.1f MB",
                        static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024.0));
            ImGui::Text("Dedicated system memory: %.1f MB",
                        static_cast<double>(desc.DedicatedSystemMemory) / (1024.0 * 1024.0));
            ImGui::Text("Shared system memory: %.1f MB",
                        static_cast<double>(desc.SharedSystemMemory) / (1024.0 * 1024.0));
            ImGui::Text("VendorId: 0x%04X  DeviceId: 0x%04X", desc.VendorId, desc.DeviceId);
            ImGui::Text("Adapter flags: 0x%X", static_cast<unsigned>(desc.Flags));
        }
        else
        {
            ImGui::TextUnformatted("Extended DXGI adapter info unavailable.");
        }
        ImGui::Text("Cross-adapter resource sharing: %s",
                    device->IsCrossAdapterTextureSupported() ? "yes" : "no");
        ImGui::Text("GPU timestamp (render path): %llu", renderTimeTicks);
        ImGui::Text("GPU timestamp (compute path): %llu", computeTimeTicks);
    }
}

HybridGrassApp::HybridGrassApp(const HINSTANCE hInstance): D3DApp(hInstance)
{
    mSceneBounds.Center = Vector3(0.0f, 0.0f, 0.0f);
    mSceneBounds.Radius = 200;
}

HybridGrassApp::~HybridGrassApp()
{
    Flush();
    ShutdownImGui();
}

void HybridGrassApp::EnablePerformanceTestMode(int warmupSeconds, int sampleSeconds)
{
    performanceTestMode = true;
    performanceSweepMode = false;
    perfWarmupSeconds = std::max(1, warmupSeconds);
    perfSampleSeconds = std::max(3, sampleSeconds);
    perfCurrentStage = 0;
    perfStageStartTime = -1.0;
    perfStageInitialized = false;
    perfAggregates = {};
    perfScenarios.clear();
    perfScenarioAggregates.clear();
    perfResultPath.clear();
    fpsLimitEnabled = false;
}

void HybridGrassApp::EnablePerformanceSweepMode(int warmupSeconds, int sampleSeconds)
{
    performanceTestMode = true;
    performanceSweepMode = true;
    perfWarmupSeconds = std::max(1, warmupSeconds);
    perfSampleSeconds = std::max(3, sampleSeconds);
    perfCurrentStage = 0;
    perfStageStartTime = -1.0;
    perfStageInitialized = false;
    perfAggregates = {};
    perfResultPath.clear();
    fpsLimitEnabled = false;

    perfScenarios = {
        {L"baseline_mixed_lod", 5000, 350.0f, 1000.0f, 3, 1, 1.0f},
        {L"lod0_heavy", 5000, 1800.0f, 1800.0f, 4, 1, 1.25f},
        {L"lod1_favoring", 5000, 120.0f, 1700.0f, 2, 1, 1.0f},
        {L"dense_mixed", 12000, 350.0f, 1000.0f, 3, 1, 1.0f},
        {L"ultra_dense_lod0_heavy", 20000, 1800.0f, 1800.0f, 4, 1, 1.5f}
    };
    perfScenarioAggregates.assign(perfScenarios.size(), std::array<PerfAggregate, 2>{});
}

void HybridGrassApp::Update(const GameTimer& gt)
{
    if (imguiInitialized)
    {
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
    }

    if (pendingGrassBladeCount > 0)
    {
        Flush();
        for (auto* emitter : crossGrassEmitters)
        {
            emitter->SetGrassCount(static_cast<uint32_t>(pendingGrassBladeCount));
        }
        pendingGrassBladeCount = -1;
    }

    if (pendingGrassWorldSize > 0.0f)
    {
        Flush();
        for (auto* emitter : crossGrassEmitters)
        {
            emitter->SetWorldSize(pendingGrassWorldSize);
        }
        pendingGrassWorldSize = -1.0f;
    }

    if (grassFieldTransform)
    {
        grassFieldTransform->SetScale(Vector3(grassFieldScaleXZ, grassFieldScaleY, grassFieldScaleXZ));
    }

    float fieldCenterX = 0.0f;
    float fieldCenterZ = 0.0f;
    float fieldHalf = 200.0f;
    GetGrassWindFieldExtents(fieldCenterX, fieldCenterZ, fieldHalf);
    const float baseAngleRad = grassWindBaseAngleDeg * (3.1415926535f / 180.0f);
    const Vector2 baseFlowDir(std::cos(baseAngleRad), std::sin(baseAngleRad));
    bool clickWindActive = false;
    Vector2 clickFluidUv = Vector2(0.5f, 0.5f);
    float clickFluidRadiusSq = 0.01f;

    grassWindOriginCount = 0;

    const bool imguiBlocksSceneInput =
        imguiInitialized && ImGui::GetIO().WantCaptureMouse;

    if (!isAppPaused && camera != nullptr && MainWindow != nullptr && GetMouse()->IsLeftDown() &&
        !imguiBlocksSceneInput)
    {
        Vector3 cursorHit{};
        if (TryPickGrassGroundFromMouse(GetMouse()->GetPosX(), GetMouse()->GetPosY(), cursorHit))
        {
            clickWindActive = true;
            const int idx = std::clamp(grassWindOriginCount, 0, 3);
            grassWindOrigins[idx] = Vector4(
                cursorHit.x,
                cursorHit.y,
                cursorHit.z,
                std::max(12.0f, grassWindCursorRadius));
            // z=0 => pure radial disturbance from click.
            grassWindDirections[idx] =
                Vector4(0.f, 0.f, 0.f, std::clamp(grassWindCursorStrength * 4.0f, 0.0f, 16.0f));
            grassWindOriginCount = std::min(4, idx + 1);

            clickFluidUv = WorldPosToGrassPatchUv(cursorHit, grassWorldSize, grassFieldTransform.get());
            const float fieldSpan = std::max(grassWorldSize, 1.0f);
            const float radiusUv = std::clamp(grassWindCursorRadius / fieldSpan, 0.01f, 0.35f);
            clickFluidRadiusSq = radiusUv * radiusUv;
        }
    }

    if (!clickWindActive && std::max(0.0f, grassWindBaseStrength) > 1e-5f)
    {
        const float baseRadius = std::max(
            50.0f, fieldHalf * std::max(0.25f, grassWindBaseCoverage) * 2.1f);
        const Vector2 inlet2 = Vector2(fieldCenterX, fieldCenterZ) - baseFlowDir * (fieldHalf * 0.95f);

        const int idx = std::clamp(grassWindOriginCount, 0, 3);
        grassWindOrigins[idx] = Vector4(inlet2.x, 0.0f, inlet2.y, baseRadius);
        // z=1 => fully directional field, w=strength.
        grassWindDirections[idx] = Vector4(
            baseFlowDir.x, baseFlowDir.y, 1.0f, std::clamp(grassWindBaseStrength, 0.0f, 4.0f));
        grassWindOriginCount = std::min(4, idx + 1);
    }

    for (auto* emitter : crossGrassEmitters)
    {
        emitter->AdvanceTime(static_cast<float>(gt.DeltaTime()));
        emitter->SetLodBladeCounts(static_cast<uint32_t>(std::max(1, grassLod0BladeCount)),
                                   static_cast<uint32_t>(std::max(1, grassLod1BladeCount)));
        emitter->SetWindIntensity(std::max(0.0f, grassWindIntensity));
        emitter->SetWindAmplitude(std::max(0.0f, grassWindAmplitude));
        emitter->SetWindStrength(std::max(0.5f, grassWindAmplitude));
        emitter->SetLod0Sdof(grassLod0SdofNaturalFreq, grassLod0SdofDampingRatio);
        emitter->SetLodBladeSize(grassLod0BladeWidthScale, grassLod0BladeHeightScale,
                                 grassLod1BladeWidthScale, grassLod1BladeHeightScale);
        emitter->SetWindGradient(static_cast<uint32_t>(std::clamp(grassWindOriginCount, 0, 4)),
                                 std::max(0.1f, grassWindMapFalloff),
                                 grassWindOrigins.data(), grassWindDirections.data());
        emitter->SetFieldInfluenceScale(grassFieldInfluenceScale);
        emitter->SetLod0LeanGain(grassLod0LeanGain);
        emitter->SetDebugNearestOriginTint(debugNearestOriginTint);
        emitter->SetWindDirection(baseFlowDir);
        emitter->SetClickWindBoost(clickWindActive ? 1.0f : 0.0f);
        if (clickWindActive && UseCrossAdapter && grassGpuWindFluid)
        {
            emitter->SetWindFluidClickImpulse(
                clickFluidUv.x,
                clickFluidUv.y,
                grassWindCursorStrength * 120.0f,
                clickFluidRadiusSq);
        }
        else
        {
            emitter->SetWindFluidClickImpulse(0.0f, 0.0f, 0.0f, 0.0f);
        }
        const float fluidEn =
            UseCrossAdapter && grassGpuWindFluid ? 1.0f : 0.0f;
        emitter->SetGpuWindFluid(fluidEn, grassGpuWindFluidBlend,
                                 static_cast<uint32_t>(
                                     std::clamp(grassGpuWindJacobiIterations, 2, 40)));
        emitter->SetWindFluidSimulationTuning(
            grassGpuWindInjectStrength,
            grassGpuWindDissipation,
            grassGpuWindDt,
            grassGpuWindVorticityEps,
            static_cast<uint32_t>(std::clamp(grassGpuWindGridResolution, 32, 512)));
        const float wallAngleRad = grassGpuWindWallAngleDeg * (3.1415926535f / 180.0f);
        emitter->SetWindFluidWall(grassGpuWindWallEnable,
                                  grassGpuWindWallPosU,
                                  grassGpuWindWallPosV,
                                  wallAngleRad,
                                  grassGpuWindWallHalfLength,
                                  grassGpuWindWallHalfWidth,
                                  grassGpuWindWallDrag,
                                  grassGpuWindWallWake);
        emitter->SetLod0DebugGradientWind(
            grassLod0DebugGradient,
            grassLod0DebugGradMin,
            grassLod0DebugGradMax,
            grassLod0DebugGradAxis == 1 ? 1.0f : 0.0f);
    }

    const UINT olderIndex = currentFrameResourceIndex - 1 > globalCountFrameResources
                                ? 0
                                : static_cast<UINT>(currentFrameResourceIndex);
    primeGPURenderingTime = primeDevice->GetCommandQueue()->GetTimestamp(olderIndex);
    secondGPURenderingTime = secondDevice->GetCommandQueue()->GetTimestamp(olderIndex);

    primeGPUComputingTime = primeDevice->GetCommandQueue(GQueueType::Compute)->GetTimestamp(olderIndex);
    secondGPUComputingTime = secondDevice->GetCommandQueue(GQueueType::Compute)->GetTimestamp(olderIndex);

    const auto commandQueue = primeDevice->GetCommandQueue(GQueueType::Graphics);

    currentFrameResource = frameResources[currentFrameResourceIndex];

    if (currentFrameResource->PrimeRenderFenceValue != 0 && !commandQueue->IsFinish(
        currentFrameResource->PrimeRenderFenceValue))
    {
        commandQueue->WaitForFenceValue(currentFrameResource->PrimeRenderFenceValue);
    }

    mLightRotationAngle += 0.1f * gt.DeltaTime();

    const Matrix R = Matrix::CreateRotationY(mLightRotationAngle);
    for (int i = 0; i < 3; ++i)
    {
        auto lightDir = mBaseLightDirections[i];
        lightDir = Vector3::TransformNormal(lightDir, R);
        mRotatedLightDirections[i] = lightDir;
    }

    for (auto& e : gameObjects)
    {
        e->Update();
    }

    UpdateMaterials();

    UpdateShadowTransform(gt);
    UpdateMainPassCB(gt);
    UpdateShadowPassCB(gt);
    UpdateSsaoCB(gt);

    for (auto* grassEmitter : crossGrassEmitters)
    {
        grassEmitter->SetFrustumCullingData(
            mainPassCB.ViewProj,
            mainPassCB.EyePosW,
            grassCullMaxDistance,
            grassLod0Distance,
            grassLod1Distance,
            static_cast<uint32_t>(grassLod0BaseSegments),
            grassWindTessellationScale);
    }
}

void HybridGrassApp::PopulateShadowMapCommands(std::shared_ptr<GCommandList> cmdList)
{
    cmdList->SetRootSignature(*primeDeviceSignature.get());
    cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                       *currentFrameResource->MaterialBuffer, 1);
    cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory);
    cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                       *currentFrameResource->PrimePassConstantUploadBuffer, 1);

    shadowPath->PopulatePreRenderCommands(cmdList);

    cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::ShadowMapOpaque));
    PopulateDrawCommands(cmdList, RenderMode::Opaque);
    PopulateDrawCommands(cmdList, RenderMode::OpaqueAlphaDrop);

    cmdList->TransitionBarrier(shadowPath->GetTexture(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

void HybridGrassApp::PopulateNormalMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Draw Normals
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory);
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffer);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory);

        cmdList->SetViewports(&fullViewport, 1);
        cmdList->SetScissorRects(&fullRect, 1);

        const auto normalMap = ambientPrimePath->NormalMap();
        const auto normalDepthMap = ambientPrimePath->NormalDepthMap();
        const auto normalMapRtv = ambientPrimePath->NormalMapRtv();
        const auto normalMapDsv = ambientPrimePath->NormalMapDSV();

        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->FlushResourceBarriers();
        float clearValue[] = {0.0f, 0.0f, 1.0f, 0.0f};
        cmdList->ClearRenderTarget(normalMapRtv, 0, clearValue);
        cmdList->ClearDepthStencil(normalMapDsv, 0,
                                   D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

        cmdList->SetRenderTargets(1, normalMapRtv, 0, normalMapDsv);
        cmdList->SetRootConstantBufferView(1, *currentFrameResource->PrimePassConstantUploadBuffer);

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::DrawNormalsOpaque));
        PopulateDrawCommands(cmdList, RenderMode::Opaque);
        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::DrawNormalsOpaqueDrop));
        PopulateDrawCommands(cmdList, RenderMode::OpaqueAlphaDrop);


        cmdList->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(normalDepthMap, D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();
    }
}

void HybridGrassApp::PopulateAmbientMapCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Draw Ambient
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory);
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffer);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory);

        cmdList->SetRootSignature(*ssaoPrimeRootSignature.get());
        ambientPrimePath->ComputeSsao(cmdList, currentFrameResource->SsaoConstantUploadBuffer, 3);
    }
}

void HybridGrassApp::PopulateForwardPathCommands(const std::shared_ptr<GCommandList>& cmdList)
{
    //Forward Path with SSAA
    {
        cmdList->SetDescriptorsHeap(&srvTexturesMemory);
        cmdList->SetRootSignature(*primeDeviceSignature.get());
        cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData,
                                           *currentFrameResource->MaterialBuffer);
        cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvTexturesMemory);

        cmdList->SetViewports(&antiAliasingPrimePath->GetViewPort(), 1);
        cmdList->SetScissorRects(&antiAliasingPrimePath->GetRect(), 1);

        cmdList->TransitionBarrier((antiAliasingPrimePath->GetRenderTarget()), D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->TransitionBarrier(antiAliasingPrimePath->GetDepthMap(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
        cmdList->FlushResourceBarriers();

        cmdList->ClearRenderTarget(antiAliasingPrimePath->GetRTV(), 0, Colors::Black);
        cmdList->ClearDepthStencil(antiAliasingPrimePath->GetDSV(), 0,
                                   D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

        cmdList->SetRenderTargets(1, antiAliasingPrimePath->GetRTV(), 0,
                                  antiAliasingPrimePath->GetDSV());


        cmdList->
            SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                      *currentFrameResource->PrimePassConstantUploadBuffer);

        cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowPath->GetSrv());
        cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, ambientPrimePath->AmbientMapSrv(), 0);


        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::SkyBox));
        PopulateDrawCommands(cmdList, (RenderMode::SkyBox));

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Opaque));
        PopulateDrawCommands(cmdList, (RenderMode::Opaque));

        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::OpaqueAlphaDrop));
        PopulateDrawCommands(cmdList, (RenderMode::OpaqueAlphaDrop));

        for (auto* grassEmitter : crossGrassEmitters)
        {
            grassEmitter->SetWorldConstantsBuffer(currentFrameResource->PrimePassConstantUploadBuffer.get());
            grassEmitter->SetFrustumCullingData(mainPassCB.ViewProj, mainPassCB.EyePosW,
                                                grassCullMaxDistance, grassLod0Distance, grassLod1Distance,
                                                static_cast<uint32_t>(grassLod0BaseSegments),
                                                grassWindTessellationScale);
        }
        cmdList->SetPipelineState(*defaultPrimePipelineResources.GetPSO(RenderMode::Transparent));
        PopulateDrawCommands(cmdList, (RenderMode::Transparent));

        cmdList->SetRootConstantBufferView(StandardShaderSlot::CameraData,
                                           *currentFrameResource->PrimePassConstantUploadBuffer.get(), 0);
        PopulateDrawCommands(cmdList, RenderMode::Particle);


        cmdList->TransitionBarrier(antiAliasingPrimePath->GetRenderTarget(),
                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier((antiAliasingPrimePath->GetDepthMap()), D3D12_RESOURCE_STATE_DEPTH_READ);
        cmdList->FlushResourceBarriers();
    }
}


void HybridGrassApp::PopulateDrawCommands(std::shared_ptr<GCommandList> cmdList,
                                             RenderMode type)
{
    for (auto&& renderer : typedRenderer[static_cast<int>(type)])
    {
        renderer->Draw(cmdList);
    }
}

void HybridGrassApp::PopulateInitRenderTarget(const std::shared_ptr<GCommandList>& cmdList, GTexture& renderTarget,
                                                 GDescriptor* rtvMemory, const UINT offsetRTV)
{
    cmdList->SetViewports(&fullViewport, 1);
    cmdList->SetScissorRects(&fullRect, 1);

    cmdList->TransitionBarrier(renderTarget, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();
    cmdList->ClearRenderTarget(rtvMemory, offsetRTV, Colors::Black);

    cmdList->SetRenderTargets(1, rtvMemory, offsetRTV);
}

void HybridGrassApp::PopulateDrawFullQuadTexture(const std::shared_ptr<GCommandList>& cmdList,
                                                    GDescriptor* renderTextureSRVMemory,
                                                    const UINT renderTextureMemoryOffset, GraphicPSO& pso)
{
    cmdList->SetRootSignature(*primeDeviceSignature.get());
    cmdList->SetDescriptorsHeap(renderTextureSRVMemory);

    cmdList->SetRootDescriptorTable(StandardShaderSlot::AmbientMap, renderTextureSRVMemory, renderTextureMemoryOffset);

    cmdList->SetPipelineState(pso);
    PopulateDrawCommands(cmdList, (RenderMode::Quad));
}


void HybridGrassApp::Draw(const GameTimer& gt)
{
    if (isResizing) return;

    const UINT timestampHeapIndex = 2 * currentFrameResourceIndex;

    const bool crossFencesReady = secondRenderFence && secondComputeFence && primeRenderFence &&
                                  primeComputeFence;
    // Use second GPU + cross-adapter fences only when enabled and fences exist; otherwise same-GPU queue waits.
    const bool useCrossGpuPath = UseCrossAdapter && crossFencesReady;

    std::shared_ptr<GCommandQueue> computeQueue;

    if (useCrossGpuPath)
    {
        computeQueue = secondDevice->GetCommandQueue(GQueueType::Compute);
    }
    else
    {
        computeQueue = primeDevice->GetCommandQueue(GQueueType::Compute);
    }

    auto renderQueue = primeDevice->GetCommandQueue(GQueueType::Graphics);

    if (useCrossGpuPath)
    {
        WaitForSharedFenceValueCpu(secondRenderFence, sharedRenderFenceValue);
    }
    else
    {
        computeQueue->Wait(renderQueue);
    }


    {
        const auto cmdList = computeQueue->GetCommandList();

        cmdList->EndQuery(timestampHeapIndex);

        for (auto* grassEmitter : crossGrassEmitters)
        {
            grassEmitter->SetFrustumCullingData(
                mainPassCB.ViewProj,
                mainPassCB.EyePosW,
                grassCullMaxDistance,
                grassLod0Distance,
                grassLod1Distance,
                static_cast<uint32_t>(grassLod0BaseSegments),
                grassWindTessellationScale);
        }

        for (auto emitter : crossEmitter)
        {
            emitter->Dispatch(cmdList);
        }

        for (auto emitter : crossGrassEmitters)
        {
            emitter->Dispatch(cmdList);
        }

        if (showWindFieldDebug && windPreviewLiveGpuReadback_)
            AppendWindFluidPreviewReadbackIfDue(cmdList);

        cmdList->EndQuery(timestampHeapIndex + 1);
        cmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));

        currentFrameResource->ComputeFenceValue = computeQueue->ExecuteCommandList(cmdList);

        if (windFluidReadbackQueued_)
        {
            windFluidReadbackFenceValue_ = currentFrameResource->ComputeFenceValue;
            windFluidReadbackQueued_ = false;
        }

        if (useCrossGpuPath)
        {
            sharedComputeFenceValue = currentFrameResource->ComputeFenceValue;
            computeQueue->Signal(secondComputeFence, sharedComputeFenceValue);
        }
    }


    {
        const auto cmdList = renderQueue->GetCommandList();


        cmdList->EndQuery(timestampHeapIndex);

        if (useCrossGpuPath)
        {
            WaitForSharedFenceValueCpu(primeComputeFence, sharedComputeFenceValue);
        }
        else
        {
            renderQueue->Wait(computeQueue);
        }

        PopulateNormalMapCommands(cmdList);
        PopulateAmbientMapCommands(cmdList);
        PopulateShadowMapCommands(cmdList);
        PopulateForwardPathCommands(cmdList);
        PopulateInitRenderTarget(cmdList, MainWindow->GetCurrentBackBuffer(),
                                 &currentFrameResource->BackBufferRTVMemory, 0);
        PopulateDrawFullQuadTexture(cmdList, antiAliasingPrimePath->GetSRV(),
                                    0, *defaultPrimePipelineResources.GetPSO(RenderMode::Quad));

        DrawImGui(cmdList);

        cmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
        cmdList->FlushResourceBarriers();
        cmdList->EndQuery(timestampHeapIndex + 1);
        cmdList->ResolveQuery(timestampHeapIndex, 2, timestampHeapIndex * sizeof(UINT64));

        currentFrameResource->PrimeRenderFenceValue = renderQueue->ExecuteCommandList(cmdList);

        if (useCrossGpuPath)
        {
            sharedRenderFenceValue = currentFrameResource->PrimeRenderFenceValue;
            renderQueue->Signal(primeRenderFence, sharedRenderFenceValue);
        }
    }

    currentFrameResourceIndex = MainWindow->Present();
}

bool HybridGrassApp::Initialize()
{
    InitDevices();
    InitMainWindow();
    Flush();

    LoadStudyTexture();
    Flush();
    LoadModels();
    Flush();
    CreateMaterials();
    Flush();
    MipMasGenerate();
    Flush();

    InitRenderPaths();
    Flush();
    InitSRVMemoryAndMaterials();
    Flush();
    InitRootSignature();
    Flush();
    InitPipeLineResource();
    Flush();
    CreateGO();
    Flush();
    SortGO();
    Flush();
    InitFrameResource();
    Flush();

    OnResize();

    Flush();

    InitImGui();
    // Wind debug preview texture is created on first use (see EnsureWindGradientPreviewTexture).

    return true;
}

void HybridGrassApp::InitDevices()
{
    // Hardware adapters only. GetAllDevices(true) appends WARP, so with one GPU [0] was discrete and [1] was WARP,
    // which breaks cross-adapter fences and crashes in GCommandQueue::Wait (0x87A in DXGI / validation).
    auto allDevices = GDeviceFactory::GetAllDevices(false);
    ThrowIfFailed(allDevices.empty() ? E_FAIL : S_OK);

    const auto firstDevice = allDevices[0];
    const auto otherDevice = allDevices.size() > 1 ? allDevices[1] : firstDevice;

    if (firstDevice->GetName().find(L"NVIDIA") != std::wstring::npos)
    {
        primeDevice = firstDevice;
        secondDevice = otherDevice;
    }
    else if (otherDevice->GetName().find(L"NVIDIA") != std::wstring::npos)
    {
        primeDevice = otherDevice;
        secondDevice = firstDevice;
    }
    else
    {
        primeDevice = firstDevice;
        secondDevice = otherDevice;
    }

    HaveTwoHardwareAdapters = allDevices.size() >= 2;
    CrossAdapterSharingCapable =
        HaveTwoHardwareAdapters && primeDevice->IsCrossAdapterTextureSupported() &&
        secondDevice->IsCrossAdapterTextureSupported();
    HaveCrossAdapterHardware = HaveTwoHardwareAdapters;

    CrossAdapterFencesCreated = false;
    if (HaveTwoHardwareAdapters)
    {
        TryCreateCrossAdapterFences();
    }

    UseCrossAdapter = CrossAdapterFencesCreated;

    assets = std::make_shared<AssetsLoader>(primeDevice);


    for (int i = 0; i < static_cast<uint8_t>(RenderMode::Count); ++i)
    {
        typedRenderer.push_back(
            MemoryAllocator::CreateVector<std::shared_ptr<Renderer>>());
    }

    if (HaveTwoHardwareAdapters && !CrossAdapterFencesCreated)
    {
        logQueue.Push(
            L"\nCross-adapter shared fences could not be created at init; use \"Use Multi-GPU render\" to retry. Multi-GPU stays off until fences succeed.");
    }


    logQueue.Push(L"\nPrime Device: " + (primeDevice->GetName()));
    logQueue.Push(
        L"\t\n Cross Adapter Texture Support: " + std::to_wstring(
            primeDevice->IsCrossAdapterTextureSupported()));
    logQueue.Push(L"\nSecond Device: " + (secondDevice->GetName()));
    logQueue.Push(
        L"\t\n Cross Adapter Texture Support: " + std::to_wstring(
            secondDevice->IsCrossAdapterTextureSupported()));

    primeAdapterDescValid = primeDevice->TryGetAdapterDesc3(primeAdapterDesc);
    secondAdapterDescValid = secondDevice->TryGetAdapterDesc3(secondAdapterDesc);
}

bool HybridGrassApp::TryCreateCrossAdapterFences()
{
    if (!HaveTwoHardwareAdapters || !primeDevice || !secondDevice)
    {
        CrossAdapterFencesCreated = false;
        return false;
    }

    primeComputeFence.Reset();
    secondComputeFence.Reset();
    primeRenderFence.Reset();
    secondRenderFence.Reset();

    const bool ok =
        primeDevice->TrySharedFence(primeComputeFence, secondDevice, secondComputeFence, sharedComputeFenceValue) &&
        primeDevice->TrySharedFence(primeRenderFence, secondDevice, secondRenderFence, sharedRenderFenceValue);
    CrossAdapterFencesCreated = ok;
    return ok;
}

void HybridGrassApp::InitFrameResource()
{
    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        frameResources.push_back(std::make_unique<FrameResource>(primeDevice,
                                                                 primeDevice, 2,
                                                                 assets->GetMaterials().size()));
    }
    logQueue.Push(std::wstring(L"\nInit FrameResource "));
}

void HybridGrassApp::InitRootSignature()
{
    auto rootSignature = std::make_shared<GRootSignature>();
    CD3DX12_DESCRIPTOR_RANGE texParam[4];
    texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0); //SkyMap
    texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0); //ShadowMap
    texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::AmbientMap - 3, 0); //SsaoMap
    texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
                     assets->GetLoadTexturesCount() > 0 ? assets->GetLoadTexturesCount() : 1,
                     StandardShaderSlot::TexturesMap - 3, 0);


    rootSignature->AddConstantBufferParameter(0);
    rootSignature->AddConstantBufferParameter(1);
    rootSignature->AddShaderResourceView(0, 1);
    rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);
    rootSignature->Initialize(primeDevice);

    primeDeviceSignature = rootSignature;


    logQueue.Push(std::wstring(L"\nInit RootSignature for " + primeDevice->GetName()));

    ssaoPrimeRootSignature = std::make_shared<GRootSignature>();

    CD3DX12_DESCRIPTOR_RANGE texTable0;
    texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

    CD3DX12_DESCRIPTOR_RANGE texTable1;
    texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

    ssaoPrimeRootSignature->AddConstantBufferParameter(0);
    ssaoPrimeRootSignature->AddConstantParameter(1, 1);
    ssaoPrimeRootSignature->AddDescriptorParameter(&texTable0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    ssaoPrimeRootSignature->AddDescriptorParameter(&texTable1, 1, D3D12_SHADER_VISIBILITY_PIXEL);

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressW
        0.0f,
        0,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
    {
        pointClamp, linearClamp, depthMapSam, linearWrap
    };

    for (auto&& sampler : staticSamplers)
    {
        ssaoPrimeRootSignature->AddStaticSampler(sampler);
    }

    ssaoPrimeRootSignature->Initialize(primeDevice);
}

void HybridGrassApp::InitPipeLineResource()
{
    defaultInputLayout =
    {
        {
            "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
        {
            "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
        {
            "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
        {
            "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
        },
    };

    const D3D12_INPUT_LAYOUT_DESC desc = {defaultInputLayout.data(), defaultInputLayout.size()};

    defaultPrimePipelineResources = RenderModeFactory();
    defaultPrimePipelineResources.LoadDefaultShaders();
    defaultPrimePipelineResources.LoadDefaultPSO(primeDevice, primeDeviceSignature, desc,
                                                 BackBufferFormat, DepthStencilFormat, ssaoPrimeRootSignature,
                                                 NormalMapFormat, AmbientMapFormat);

    ambientPrimePath->SetPipelineData(*defaultPrimePipelineResources.GetPSO(RenderMode::Ssao),
                                      *defaultPrimePipelineResources.GetPSO(RenderMode::SsaoBlur));


    logQueue.Push(std::wstring(L"\nInit PSO for " + primeDevice->GetName()));

    const auto primeDeviceShadowMapPso = defaultPrimePipelineResources.GetPSO(RenderMode::ShadowMapOpaque);
}

void HybridGrassApp::CreateMaterials()
{
    auto seamless = std::make_shared<Material>(L"seamless", RenderMode::Opaque);
    seamless->FresnelR0 = Vector3(0.02f, 0.02f, 0.02f);
    seamless->Roughness = 0.1f;

    auto tex = assets->GetTextureIndex(L"seamless");
    seamless->SetDiffuseTexture(assets->GetTexture(tex), tex);

    tex = assets->GetTextureIndex(L"defaultNormalMap");

    seamless->SetNormalMap(assets->GetTexture(tex), tex);
    assets->AddMaterial(seamless);


    models[L"quad"]->SetMeshMaterial(0, assets->GetMaterial(assets->GetMaterialIndex(L"seamless")));

    logQueue.Push(std::wstring(L"\nCreate Materials"));
}

void HybridGrassApp::InitSRVMemoryAndMaterials()
{
    srvTexturesMemory =
        primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, assets->GetTextures().size());

    auto materials = assets->GetMaterials();

    for (int j = 0; j < materials.size(); ++j)
    {
        auto material = materials[j];

        material->InitMaterial(&srvTexturesMemory);
    }

    logQueue.Push(std::wstring(L"\nInit Views for " + primeDevice->GetName()));
    ambientPrimePath->BuildDescriptors();
}

void HybridGrassApp::InitRenderPaths()
{
    auto commandQueue = primeDevice->GetCommandQueue(GQueueType::Graphics);
    auto cmdList = commandQueue->GetCommandList();

    ambientPrimePath = (std::make_shared<SSAO>(
        primeDevice,
        cmdList,
        MainWindow->GetClientWidth(), MainWindow->GetClientHeight()));

    antiAliasingPrimePath = (std::make_shared<SSAA>(primeDevice, 4, MainWindow->GetClientWidth(),
                                                    MainWindow->GetClientHeight()));
    antiAliasingPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

    commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));

    logQueue.Push(std::wstring(L"\nInit Render path data for " + primeDevice->GetName()));

    shadowPath = (std::make_shared<ShadowMap>(primeDevice, 2048, 2048));
}

void HybridGrassApp::LoadStudyTexture()
{
    auto queue = primeDevice->GetCommandQueue(GQueueType::Compute);

    const auto cmdList = queue->GetCommandList();

    // Default scene only: skybox, ground quad material, default normal map.
    // Grass loads its own atlas in GrassEmitter. Other study textures are omitted for faster startup.
    auto skyTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\skymap.dds", cmdList);
    skyTex->SetName(L"skyTex");
    assets->AddTexture(skyTex);

    auto seamless = GTexture::LoadTextureFromFile(L"Data\\Textures\\seamless_grass.jpg", cmdList);
    seamless->SetName(L"seamless");
    assets->AddTexture(seamless);

    auto defaultNormalMap = GTexture::LoadTextureFromFile(
        L"Data\\Textures\\default_nmap.dds", cmdList, TextureUsage::Normalmap);
    defaultNormalMap->SetName(L"defaultNormalMap");
    assets->AddTexture(defaultNormalMap);

    queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));

    logQueue.Push(std::wstring(L"\nLoad DDS Texture (minimal set)"));
}

void HybridGrassApp::LoadModels()
{
    auto queue = primeDevice->GetCommandQueue(GQueueType::Compute);
    const auto cmdList = queue->GetCommandList();

    // Default scene only (see CreateGO). Uncomment others when enabling benchmark / temple props.
    auto desertDragon = assets->CreateModelFromFile(
        cmdList, "Data\\Objects\\DesertDragon\\DesertDragon.FBX");
    desertDragon->scaleMatrix = Matrix::CreateScale(0.1);
    models[L"desertDragon"] = std::move(desertDragon);

    auto sphere = assets->GenerateSphere(cmdList);
    models[L"sphere"] = std::move(sphere);

    auto quad = assets->GenerateQuad(cmdList);
    models[L"quad"] = std::move(quad);

    auto platform = assets->CreateModelFromFile(
        cmdList, "Data\\Objects\\Temple\\SM_PlatformSquare.FBX");
    models[L"platform"] = std::move(platform);

    queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
    queue->Flush();

    logQueue.Push(std::wstring(L"\nLoad Models Data (minimal set)"));
}

void HybridGrassApp::MipMasGenerate()
{
    try
    {
        std::vector<GTexture*> generatedMipTextures;

        auto textures = assets->GetTextures();

        for (auto&& texture : textures)
        {
            texture->ClearTrack();

            if (texture->GetD3D12Resource()->GetDesc().Flags != D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
                continue;

            if (!texture->HasMipMap)
            {
                generatedMipTextures.push_back(texture.get());
            }
        }

        const auto computeQueue = primeDevice->GetCommandQueue(GQueueType::Compute);
        auto computeList = computeQueue->GetCommandList();
        GTexture::GenerateMipMaps(computeList, generatedMipTextures.data(), generatedMipTextures.size());
        computeQueue->WaitForFenceValue(computeQueue->ExecuteCommandList(computeList));
        logQueue.Push(std::wstring(L"\nMip Map Generation for " + primeDevice->GetName()));

        computeList = computeQueue->GetCommandList();
        for (auto&& texture : generatedMipTextures)
            computeList->TransitionBarrier(texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
        computeList->FlushResourceBarriers();
        logQueue.Push(std::wstring(L"\nTexture Barrier Generation for " + primeDevice->GetName()));
        computeQueue->WaitForFenceValue(computeQueue->ExecuteCommandList(computeList));

        logQueue.Push(std::wstring(L"\nMipMap Generation cmd list executing " + primeDevice->GetName()));
        for (auto&& pair : textures)
            pair->ClearTrack();
        logQueue.Push(std::wstring(L"\nFinish Mip Map Generation for " + primeDevice->GetName()));
    }
    catch (DxException& e)
    {
        logQueue.Push(L"\n" + e.Filename + L" " + e.FunctionName + L" " + std::to_wstring(e.LineNumber));
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
    }
    catch (...)
    {
        logQueue.Push(L"\nWTF???? How It Fix");
    }
}

void HybridGrassApp::SortGO()
{
    for (auto&& item : gameObjects)
    {
        auto light = item->GetComponent<Light>();
        if (light != nullptr)
        {
            lights.push_back(light.get());
        }

        auto cam = item->GetComponent<Camera>();
        if (cam != nullptr)
        {
            camera = (cam);
        }
    }
}

void HybridGrassApp::CreateGO()
{
    logQueue.Push(std::wstring(L"\nStart Create GO"));
    auto skySphere = std::make_unique<GameObject>("Sky");
    skySphere->GetTransform()->SetScale({500, 500, 500});
    {
        const auto renderer = std::make_shared<SkyBox>(primeDevice,
                                                       models[L"sphere"],
                                                       *assets->GetTexture(
                                                           assets->
                                                           GetTextureIndex(L"skyTex")).get(),
                                                       &srvTexturesMemory,
                                                       assets->GetTextureIndex(L"skyTex"));

        skySphere->AddComponent(renderer);
        typedRenderer[static_cast<int>(RenderMode::SkyBox)].push_back((renderer));
    }
    gameObjects.push_back(std::move(skySphere));

    auto quadRitem = std::make_unique<GameObject>("Quad");
    {
        auto renderer = std::make_shared<ModelRenderer>(primeDevice,
                                                        models[L"quad"]);
        renderer->SetModel(models[L"quad"]);
        quadRitem->AddComponent(renderer);
        typedRenderer[static_cast<int>(RenderMode::Debug)].push_back(renderer);
        typedRenderer[static_cast<int>(RenderMode::Quad)].push_back(renderer);
    }
    gameObjects.push_back(std::move(quadRitem));


    auto sun1 = std::make_unique<GameObject>("Directional Light");
    auto light = std::make_shared<Light>(Directional);
    light->Direction({0.57735f, -0.57735f, 0.57735f});
    light->Strength({0.8f, 0.8f, 0.8f});
    sun1->AddComponent(light);
    gameObjects.push_back(std::move(sun1));

//     for (int i = 0; i < 11; ++i)
//     {
//         auto nano = std::make_unique<GameObject>();
//         nano->GetTransform()->SetPosition(Vector3::Right * -15 + Vector3::Forward * 12 * i);
//         nano->GetTransform()->SetEulerRotate(Vector3(0, -90, 0));
//         auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"nano"]);
//         nano->AddComponent(renderer);
//         typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
//         gameObjects.push_back(std::move(nano));
// 
// 
//         auto doom = std::make_unique<GameObject>();
//         doom->SetScale(0.08);
//         doom->GetTransform()->SetPosition(Vector3::Right * 15 + Vector3::Forward * 12 * i);
//         doom->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
//         renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"doom"]);
//         doom->AddComponent(renderer);
//         typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
//         gameObjects.push_back(std::move(doom));
//     }
// 
//     for (int i = 0; i < 12; ++i)
//     {
//         for (int j = 0; j < 3; ++j)
//         {
//             auto atlas = std::make_unique<GameObject>();
//             atlas->GetTransform()->SetPosition(
//                 Vector3::Right * -60 + Vector3::Right * -30 * j + Vector3::Up * 11 + Vector3::Forward * 10 * i);
//             auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"atlas"]);
//             atlas->AddComponent(renderer);
//             typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
//             gameObjects.push_back(std::move(atlas));
// 
// 
//             auto pbody = std::make_unique<GameObject>();
//             pbody->GetTransform()->SetPosition(
//                 Vector3::Right * 130 + Vector3::Right * -30 * j + Vector3::Up * 11 + Vector3::Forward * 10 * i);
//             renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"pbody"]);
//             pbody->AddComponent(renderer);
//             typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
//             gameObjects.push_back(std::move(pbody));
//         }
//     }

    
   /* auto particle = std::make_unique<GameObject>();
    particle->GetTransform()->SetPosition(Vector3::Up);
    const auto emitter = std::make_shared<CrossAdapterParticleEmitter>(primeDevice, secondDevice, 100000 * 1);
    particle->AddComponent(emitter);
    typedRenderer[static_cast<int>(RenderMode::Particle)].push_back(emitter);
    crossEmitter.push_back(emitter.get());
    gameObjects.push_back(std::move(particle));
    */
    // ��������� �����:
    //auto grassField = std::make_unique<GameObject>("Grass Field");
    //grassField->GetTransform()->SetPosition(Vector3::Up);
    //grassField->SetScale(6.5f);
    //auto grassEmitter = std::make_shared<GrassEmitter>(primeDevice, 5000, 200.0f); // 50k ��������
    //grassField->AddComponent(grassEmitter);
    //typedRenderer[static_cast<int>(RenderMode::Particle)].push_back(grassEmitter);
    //gameObjects.push_back(std::move(grassField));

    auto grassField = std::make_unique<GameObject>("Grass Field");
    grassFieldTransform = grassField->GetTransform();
    grassFieldTransform->SetPosition(Vector3(0, 0, 0));
    grassFieldTransform->SetScale(Vector3(grassFieldScaleXZ, grassFieldScaleY, grassFieldScaleXZ));
    auto grassEmitter = std::make_shared<CrossAdapterGrassEmitter>(primeDevice, secondDevice,
                                                                   static_cast<uint32_t>(grassBladeCount), grassWorldSize,
                                                                   static_cast<uint32_t>(grassLod0BladeCount),
                                                                   static_cast<uint32_t>(grassLod1BladeCount));
    grassField->AddComponent(grassEmitter);
    typedRenderer[static_cast<int>(RenderMode::Transparent)].push_back(grassEmitter);
    crossGrassEmitters.push_back(grassEmitter.get()); // ���� ����� ������ ��� ����������
    gameObjects.push_back(std::move(grassField));

     auto platform = std::make_unique<GameObject>();
     platformTransform = platform->GetTransform();
     platformTransform->SetScale(Vector3(23.3, 2, 21.1f));
     platformTransform->SetPosition(Vector3(1500, 0, 100));
     auto renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"platform"]);
     platform->AddComponent(renderer);
     typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);



    auto camera = std::make_unique<GameObject>("MainCamera");
    //camera->GetTransform()->SetParent(rotater->GetTransform().get());
    camera->AddComponent(std::make_shared<CameraController>(60.0f, 25.0f, 18.0f));
    camera->GetTransform()->SetEulerRotate(Vector3(-30, 270, 0));
    camera->GetTransform()->SetPosition(Vector3(-500, 190, -32));
    camera->AddComponent(std::make_shared<Camera>(AspectRatio()));

    gameObjects.push_back(std::move(camera));
    //gameObjects.push_back(std::move(rotater));


//     auto stair = std::make_unique<GameObject>();
//     stair->GetTransform()->SetParent(platform->GetTransform().get());
//     stair->SetScale(0.2);
//     stair->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
//     stair->GetTransform()->SetPosition(Vector3::Left * 700);
//     renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"stair"]);
//     stair->AddComponent(renderer);
//     typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);


//     auto columns = std::make_unique<GameObject>();
//     columns->GetTransform()->SetParent(stair->GetTransform().get());
//     columns->SetScale(0.8);
//     columns->GetTransform()->SetEulerRotate(Vector3(0, 0, 90));
//     columns->GetTransform()->SetPosition(Vector3::Up * 2000 + Vector3::Forward * 900);
//     renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"columns"]);
//     columns->AddComponent(renderer);
//     typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);

//     auto fountain = std::make_unique<GameObject>();
//     fountain->SetScale(0.005);
//     fountain->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
//     fountain->GetTransform()->SetPosition(Vector3::Up * 35 + Vector3::Backward * 77);
//     renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"fountain"]);
//     fountain->AddComponent(renderer);
//     typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);

   gameObjects.push_back(std::move(platform));
   // gameObjects.push_back(std::move(stair));
   // gameObjects.push_back(std::move(columns));
   // gameObjects.push_back(std::move(fountain));

    auto desertDragon = std::make_unique<GameObject>();
    desertDragon->GetTransform()->SetEulerRotate(Vector3(90, 0, 0));
    desertDragon->GetTransform()->SetPosition(Vector3::Right * 960 + Vector3::Up * -5 + Vector3::Backward * 775);
    renderer = std::make_shared<ModelRenderer>(primeDevice, models[L"desertDragon"]);
    desertDragon->AddComponent(renderer);
    typedRenderer[static_cast<int>(RenderMode::Opaque)].push_back(renderer);
    gameObjects.push_back(std::move(desertDragon));

    for (auto* emitter : crossEmitter)
    {
        if (UseCrossAdapter)
            emitter->EnableShared();
        else
            emitter->DisableShared();
    }
    for (auto* emitter : crossGrassEmitters)
    {
        if (UseCrossAdapter)
            emitter->EnableShared();
        else
            emitter->DisableShared();
    }

    //logQueue.Push(std::wstring(L"\nFinish create GO"));
}

void HybridGrassApp::CalculateFrameStats()
{
    if (performanceTestMode)
    {
        frameCount++;
        if ((timer.TotalTime() - timeElapsed) < 1.0f)
        {
            return;
        }

        const double fps = static_cast<double>(frameCount);
        frameCount = 0;
        timeElapsed += 1.0f;

        if (!perfStageInitialized)
        {
            Flush();
            // Disable VSync for benchmark mode to avoid refresh-rate capping.
            MainWindow->SetVSync(false);
            const int scenarioIndex = performanceSweepMode ? (perfCurrentStage / 2) : 0;
            const int modeIndex = performanceSweepMode ? (perfCurrentStage % 2) : perfCurrentStage;
            const bool multi = modeIndex == 1;

            if (performanceSweepMode && scenarioIndex >= 0 && scenarioIndex < static_cast<int>(perfScenarios.size()))
            {
                const PerfScenario& s = perfScenarios[scenarioIndex];
                grassBladeCount = std::max(1, s.grassCount);
                pendingGrassBladeCount = grassBladeCount;
                grassLod0Distance = std::clamp(s.lod0Distance, 25.0f, grassCullMaxDistance);
                grassLod1Distance = std::clamp(std::max(s.lod1Distance, grassLod0Distance), grassLod0Distance, grassCullMaxDistance);
                grassLod0BladeCount = std::clamp(s.lod0BladeCount, 1, 4);
                grassLod1BladeCount = std::clamp(s.lod1BladeCount, 1, 4);
                grassFieldInfluenceScale = std::max(0.0f, s.fieldInfluenceScale);
            }

            UseCrossAdapter = multi && CrossAdapterFencesCreated;
            for (auto* emitter : crossEmitter)
            {
                if (UseCrossAdapter) emitter->EnableShared();
                else emitter->DisableShared();
            }
            for (auto* emitter : crossGrassEmitters)
            {
                if (UseCrossAdapter) emitter->EnableShared();
                else emitter->DisableShared();
            }
            perfStageStartTime = timer.TotalTime();
            perfStageInitialized = true;
        }

        const double stageElapsed = timer.TotalTime() - perfStageStartTime;
        const double warmupEnd = static_cast<double>(perfWarmupSeconds);
        const double stageEnd = warmupEnd + static_cast<double>(perfSampleSeconds);
        if (stageElapsed >= warmupEnd && stageElapsed < stageEnd)
        {
            PerfAggregate* aggregate = nullptr;
            if (performanceSweepMode)
            {
                const int scenarioIndex = perfCurrentStage / 2;
                const int modeIndex = perfCurrentStage % 2;
                if (scenarioIndex >= 0 && scenarioIndex < static_cast<int>(perfScenarioAggregates.size()))
                {
                    aggregate = &perfScenarioAggregates[scenarioIndex][modeIndex];
                }
            }
            else
            {
                aggregate = &perfAggregates[perfCurrentStage];
            }

            if (!aggregate)
            {
                return;
            }

            PerfAggregate& a = *aggregate;
            a.samples++;
            a.fpsSum += fps;
            a.primeRenderSum += static_cast<double>(primeGPURenderingTime);
            a.secondRenderSum += static_cast<double>(secondGPURenderingTime);
            a.primeComputeSum += static_cast<double>(primeGPUComputingTime);
            a.secondComputeSum += static_cast<double>(secondGPUComputingTime);
            a.minFps = std::min(a.minFps, fps);
            a.maxFps = std::max(a.maxFps, fps);
        }

        if (stageElapsed >= stageEnd)
        {
            perfCurrentStage++;
            perfStageInitialized = false;
            const int stageCount = performanceSweepMode
                                       ? static_cast<int>(perfScenarios.size() * 2)
                                       : 2;
            if (perfCurrentStage >= stageCount)
            {
                if (performanceSweepMode)
                {
                    WritePerformanceSweepResults();
                }
                else
                {
                    WritePerformanceTestResults();
                }
                IsStop = true;
            }
        }

        const int modeIndex = performanceSweepMode ? (perfCurrentStage % 2) : perfCurrentStage;
        std::wstring title = L"Perf test: ";
        title += (modeIndex == 0 ? L"Single GPU" : L"Multi GPU");
        if (performanceSweepMode)
        {
            const int scenarioIndex = perfCurrentStage / 2;
            if (scenarioIndex >= 0 && scenarioIndex < static_cast<int>(perfScenarios.size()))
            {
                title += L" | " + perfScenarios[scenarioIndex].name;
            }
        }
        title += L" (" + std::to_wstring(static_cast<int>(stageElapsed)) + L"s)";
        MainWindow->SetWindowTitle(title);
        return;
    }

    static float minFps = std::numeric_limits<float>::max();
    static float minMspf = std::numeric_limits<float>::max();
    static float maxFps = std::numeric_limits<float>::min();
    static float maxMspf = std::numeric_limits<float>::min();
    static UINT writeStaticticCount = 0;
    static UINT64 primeGPUTimeMax = std::numeric_limits<UINT64>::min();
    static UINT64 primeGPUTimeMin = std::numeric_limits<UINT64>::max();
    static UINT64 secondGPUTimeMax = std::numeric_limits<UINT64>::min();
    static UINT64 secondGPUTimeMin = std::numeric_limits<UINT64>::max();

    static UINT64 primeGPUComputingTimeMax = std::numeric_limits<UINT64>::min();
    static UINT64 primeGPUComputingTimeMin = std::numeric_limits<UINT64>::max();
    static UINT64 secondGPUComputingTimeMax = std::numeric_limits<UINT64>::min();
    static UINT64 secondGPUComputingTimeMin = std::numeric_limits<UINT64>::max();
    frameCount++;

    if ((timer.TotalTime() - timeElapsed) >= 1.0f)
    {
        const float fps = static_cast<float>(frameCount); // fps = frameCnt / 1
        const float mspf = 1000.0f / fps;

        minFps = std::min(fps, minFps);
        minMspf = std::min(mspf, minMspf);
        maxFps = std::max(fps, maxFps);
        maxMspf = std::max(mspf, maxMspf);

        primeGPUTimeMin = std::min(primeGPURenderingTime, primeGPUTimeMin);
        primeGPUTimeMax = std::max(primeGPURenderingTime, primeGPUTimeMax);
        secondGPUTimeMin = std::min(secondGPURenderingTime, secondGPUTimeMin);
        secondGPUTimeMax = std::max(secondGPURenderingTime, secondGPUTimeMax);

        primeGPUComputingTimeMin = std::min(primeGPUComputingTime, primeGPUComputingTimeMin);
        primeGPUComputingTimeMax = std::max(primeGPUComputingTime, primeGPUComputingTimeMax);
        secondGPUComputingTimeMin = std::min(secondGPUComputingTime, secondGPUComputingTimeMin);
        secondGPUComputingTimeMax = std::max(secondGPUComputingTime, secondGPUComputingTimeMax);


        frameCount = 0;
        timeElapsed += 1.0f;


        const std::wstring title = L"FPS " + std::to_wstring(fps) + L" Step:" + (
                UseCrossAdapter ? L"2" : L"1") + L"/2" + L" Progress: " + std::to_wstring(
                (static_cast<float>(writeStaticticCount) / StatisticStepSecondsCount) * 100.0f) + L"/" +
            std::to_wstring(100);

        if (writeStaticticCount >= StatisticStepSecondsCount)
        {
            const std::wstring staticticStr =
                L"\nUse Cross Adapter: " + std::to_wstring(UseCrossAdapter)
                + L"\n\tMin FPS:" + std::to_wstring(minFps)
                + L"\n\tMin MSPF:" + std::to_wstring(minMspf)
                + L"\n\tMax FPS:" + std::to_wstring(maxFps)
                + L"\n\tMax MSPF:" + std::to_wstring(maxMspf)
                + L"\n\tMax Prime GPU Rendering Time:" + std::to_wstring(primeGPUTimeMax) +
                +L"\n\tMin Prime GPU Rendering Time:" + std::to_wstring(primeGPUTimeMin) +
                +L"\n\tMax Second GPU Rendering Time:" + std::to_wstring(secondGPUTimeMax)
                + L"\n\tMin Second GPU Rendering Time:" + std::to_wstring(secondGPUTimeMin)
                + L"\n\tMax Prime GPU Computing Time:" + std::to_wstring(primeGPUComputingTimeMax) +
                +L"\n\tMin Prime GPU Computing Time:" + std::to_wstring(primeGPUComputingTimeMin) +
                +L"\n\tMax Second GPU Computing Time:" + std::to_wstring(secondGPUComputingTimeMax)
                + L"\n\tMin Second GPU Computing Time:" + std::to_wstring(secondGPUComputingTimeMin);

            logQueue.Push(staticticStr);


            writeStaticticCount = 0;
            minFps = std::numeric_limits<float>::max();
            minMspf = std::numeric_limits<float>::max();
            maxFps = std::numeric_limits<float>::min();
            maxMspf = std::numeric_limits<float>::min();
            primeGPUTimeMax = std::numeric_limits<UINT64>::min();
            primeGPUTimeMin = std::numeric_limits<UINT64>::max();
            secondGPUTimeMax = std::numeric_limits<UINT64>::min();
            secondGPUTimeMin = std::numeric_limits<UINT64>::max();
            primeGPUComputingTimeMax = std::numeric_limits<UINT64>::min();
            primeGPUComputingTimeMin = std::numeric_limits<UINT64>::max();
            secondGPUComputingTimeMax = std::numeric_limits<UINT64>::min();
            secondGPUComputingTimeMin = std::numeric_limits<UINT64>::max();

            if (!HaveTwoHardwareAdapters || !CrossAdapterFencesCreated)
            {
                IsStop = true;
            }
            else if (UseCrossAdapter == false)
            {
                Flush();
                for (auto&& emitter : crossEmitter)
                {
                    emitter->EnableShared();
                }

                for (auto& emitter : crossGrassEmitters) //crossGrassEmitters
                {
                    emitter->EnableShared();
                }
                UseCrossAdapter = true;
              
            }
            else
            {
                IsStop = true;
            }
        }
        else
        {
            const std::wstring staticticStr =
                L"\n\tFPS:" + std::to_wstring(fps)
                + L"\n\tMSPF:" + std::to_wstring(mspf)
                + L"\n\tPrime GPU Rendering Time:" + std::to_wstring(primeGPURenderingTime)
                + L"\n\tSecond GPU Rendering Time:" + std::to_wstring(secondGPURenderingTime)
                + L"\n\tPrime GPU Computing Time:" + std::to_wstring(primeGPUComputingTime)
                + L"\n\tSecond GPU Computing Time:" + std::to_wstring(secondGPUComputingTime);

            logQueue.Push(staticticStr);

            writeStaticticCount++;
        }


        MainWindow->SetWindowTitle(title);
    }
}

void HybridGrassApp::WritePerformanceTestResults()
{
    const auto path = std::filesystem::current_path().wstring() + L"\\grass-perf-results.csv";
    perfResultPath = path;
    std::wofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open())
    {
        logQueue.Push(L"\nFailed to open grass-perf-results.csv");
        return;
    }

    // Keep numeric formatting locale-independent for stable spreadsheet import.
    file.imbue(std::locale::classic());
    // Use semicolon delimiter to avoid conflicts on locales using comma decimals.
    file << L"mode;samples;avg_fps;min_fps;max_fps;avg_prime_render_ticks;avg_second_render_ticks;avg_prime_compute_ticks;avg_second_compute_ticks\n";
    auto writeRow = [&](const wchar_t* mode, const PerfAggregate& a)
    {
        const double n = std::max(1, a.samples);
        const double avgFps = a.fpsSum / n;
        const double avgPrimeRender = a.primeRenderSum / n;
        const double avgSecondRender = a.secondRenderSum / n;
        const double avgPrimeCompute = a.primeComputeSum / n;
        const double avgSecondCompute = a.secondComputeSum / n;
        const double minFps = a.samples > 0 ? a.minFps : 0.0;
        const double maxFps = a.samples > 0 ? a.maxFps : 0.0;
        file << mode << L";" << a.samples << L";"
             << std::fixed << std::setprecision(2)
             << avgFps << L";" << minFps << L";" << maxFps << L";"
             << avgPrimeRender << L";" << avgSecondRender << L";"
             << avgPrimeCompute << L";" << avgSecondCompute << L"\n";
    };
    writeRow(L"single_gpu", perfAggregates[0]);
    writeRow(L"multi_gpu", perfAggregates[1]);
    file.close();
    logQueue.Push(L"\nPerformance test saved: " + perfResultPath);
}

void HybridGrassApp::WritePerformanceSweepResults()
{
    const auto path = std::filesystem::current_path().wstring() + L"\\grass-perf-sweep-results.csv";
    perfResultPath = path;
    std::wofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open())
    {
        logQueue.Push(L"\nFailed to open grass-perf-sweep-results.csv");
        return;
    }

    file.imbue(std::locale::classic());
    file << L"scenario;mode;grass_count;lod0_distance;lod1_distance;lod0_blades;lod1_blades;field_influence_scale;samples;avg_fps;min_fps;max_fps;avg_prime_render_ticks;avg_second_render_ticks;avg_prime_compute_ticks;avg_second_compute_ticks\n";

    for (size_t i = 0; i < perfScenarios.size(); ++i)
    {
        const PerfScenario& s = perfScenarios[i];
        for (int mode = 0; mode < 2; ++mode)
        {
            const PerfAggregate& a = perfScenarioAggregates[i][mode];
            const double n = std::max(1, a.samples);
            const double avgFps = a.fpsSum / n;
            const double avgPrimeRender = a.primeRenderSum / n;
            const double avgSecondRender = a.secondRenderSum / n;
            const double avgPrimeCompute = a.primeComputeSum / n;
            const double avgSecondCompute = a.secondComputeSum / n;
            const double minFps = a.samples > 0 ? a.minFps : 0.0;
            const double maxFps = a.samples > 0 ? a.maxFps : 0.0;

            file << s.name << L";" << (mode == 0 ? L"single_gpu" : L"multi_gpu") << L";"
                 << s.grassCount << L";" << std::fixed << std::setprecision(1)
                 << s.lod0Distance << L";" << s.lod1Distance << L";"
                 << s.lod0BladeCount << L";" << s.lod1BladeCount << L";"
                 << std::setprecision(2) << s.fieldInfluenceScale << L";"
                 << a.samples << L";"
                 << avgFps << L";" << minFps << L";" << maxFps << L";"
                 << avgPrimeRender << L";" << avgSecondRender << L";"
                 << avgPrimeCompute << L";" << avgSecondCompute << L"\n";
        }
    }

    file.close();
    logQueue.Push(L"\nPerformance sweep saved: " + perfResultPath);
}

void HybridGrassApp::LogWriting()
{
    const std::filesystem::path filePath(
        L"SharedParticle " + primeDevice->GetName() + L"+" + secondDevice->GetName() + L".txt");

    const auto path = std::filesystem::current_path().wstring() + L"\\" + filePath.wstring();

    OutputDebugStringW(path.c_str());

    std::wofstream fileSteam;
    fileSteam.open(path.c_str(), std::ios::out | std::ios::in | std::ios::binary | std::ios::trunc);
    if (fileSteam.is_open())
    {
        fileSteam << L"Information" << std::endl << L"Statistic step seconds:" << std::to_wstring(
            StatisticStepSecondsCount) << std::endl;
    }

    std::wstring line;

    while (logQueue.Size() > 0)
    {
        while (logQueue.TryPop(line))
        {
            fileSteam << line;
        }
    }

    fileSteam << L"\nFinish Logs" << std::endl;

    fileSteam.flush();
    fileSteam.close();
}

int HybridGrassApp::Run()
{
    MSG msg = {nullptr};

    timer.Reset();

    while (msg.message != WM_QUIT)
    {
        // If there are Window messages then process them.
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        // Otherwise, do animation/game stuff.
        else
        {
            const auto frameStart = std::chrono::steady_clock::now();

            if (IsStop)
            {
                MainWindow->SetWindowTitle(MainWindow->GetWindowName() + L" Finished. Wait...");
                LogWriting();
                Quit();
                break;
            }

            timer.Tick();

            //if (!isAppPaused)
            {
                CalculateFrameStats();
                Update(timer);
                Draw(timer);
            }
            //else
            {
                //Sleep(100);
            }

            primeDevice->ResetAllocators(frameCount);
            secondDevice->ResetAllocators(frameCount);

            if (fpsLimitEnabled && fpsLimitTarget > 0)
            {
                const auto frameBudget = std::chrono::duration<double>(1.0 / static_cast<double>(fpsLimitTarget));
                const auto frameElapsed = std::chrono::steady_clock::now() - frameStart;
                if (frameElapsed < frameBudget)
                {
                    std::this_thread::sleep_for(frameBudget - frameElapsed);
                }
            }
        }
    }

    return static_cast<int>(msg.wParam);
}

void HybridGrassApp::UpdateMaterials()
{
    {
        auto currentMaterialBuffer = currentFrameResource->MaterialBuffer;

        for (auto&& material : assets->GetMaterials())
        {
            material->Update();
            auto constantData = material->GetMaterialConstantData();
            currentMaterialBuffer->CopyData(material->GetIndex(), constantData);
        }
    }
}

void HybridGrassApp::UpdateShadowTransform(const GameTimer& gt)
{
    // Only the first "main" light casts a shadow.
    Vector3 lightDir = mRotatedLightDirections[0];
    Vector3 lightPos = -2.0f * mSceneBounds.Radius * lightDir;
    Vector3 targetPos = mSceneBounds.Center;
    Vector3 lightUp = Vector3::Up;
    Matrix lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

    mLightPosW = lightPos;


    // Transform bounding sphere to light space.
    Vector3 sphereCenterLS = Vector3::Transform(targetPos, lightView);


    // Ortho frustum in light space encloses scene.
    float l = sphereCenterLS.x - mSceneBounds.Radius;
    float b = sphereCenterLS.y - mSceneBounds.Radius;
    float n = sphereCenterLS.z - mSceneBounds.Radius;
    float r = sphereCenterLS.x + mSceneBounds.Radius;
    float t = sphereCenterLS.y + mSceneBounds.Radius;
    float f = sphereCenterLS.z + mSceneBounds.Radius;

    mLightNearZ = n;
    mLightFarZ = f;
    Matrix lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

    // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
    Matrix T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);

    Matrix S = lightView * lightProj * T;
    mLightView = lightView;
    mLightProj = lightProj;
    mShadowTransform = S;
}

void HybridGrassApp::UpdateShadowPassCB(const GameTimer& gt)
{
    auto view = mLightView;
    auto proj = mLightProj;

    auto viewProj = (view * proj);
    auto invView = view.Invert();
    auto invProj = proj.Invert();
    auto invViewProj = viewProj.Invert();

    shadowPassCB.View = view.Transpose();
    shadowPassCB.InvView = invView.Transpose();
    shadowPassCB.Proj = proj.Transpose();
    shadowPassCB.InvProj = invProj.Transpose();
    shadowPassCB.ViewProj = viewProj.Transpose();
    shadowPassCB.InvViewProj = invViewProj.Transpose();
    shadowPassCB.EyePosW = mLightPosW;
    shadowPassCB.NearZ = mLightNearZ;
    shadowPassCB.FarZ = mLightFarZ;

    UINT w = shadowPath->Width();
    UINT h = shadowPath->Height();
    shadowPassCB.RenderTargetSize = Vector2(static_cast<float>(w), static_cast<float>(h));
    shadowPassCB.InvRenderTargetSize = Vector2(1.0f / w, 1.0f / h);

    auto currPassCB = currentFrameResource->PrimePassConstantUploadBuffer;
    currPassCB->CopyData(1, shadowPassCB);
}

void HybridGrassApp::UpdateMainPassCB(const GameTimer& gt)
{
    auto view = camera->GetViewMatrix();
    auto proj = camera->GetProjectionMatrix();

    auto viewProj = (view * proj);
    auto invView = view.Invert();
    auto invProj = proj.Invert();
    auto invViewProj = viewProj.Invert();
    auto shadowTransform = mShadowTransform;

    // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
    Matrix T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);
    Matrix viewProjTex = XMMatrixMultiply(viewProj, T);
    mainPassCB.View = view.Transpose();
    mainPassCB.InvView = invView.Transpose();
    mainPassCB.Proj = proj.Transpose();
    mainPassCB.InvProj = invProj.Transpose();
    mainPassCB.ViewProj = viewProj.Transpose();
    mainPassCB.InvViewProj = invViewProj.Transpose();
    mainPassCB.ViewProjTex = viewProjTex.Transpose();
    mainPassCB.ShadowTransform = shadowTransform.Transpose();
    mainPassCB.EyePosW = camera->gameObject->GetTransform()->GetWorldPosition();
    mainPassCB.RenderTargetSize = Vector2(static_cast<float>(MainWindow->GetClientWidth()),
                                          static_cast<float>(MainWindow->GetClientHeight()));
    mainPassCB.InvRenderTargetSize = Vector2(1.0f / mainPassCB.RenderTargetSize.x,
                                             1.0f / mainPassCB.RenderTargetSize.y);
    mainPassCB.NearZ = 1.0f;
    mainPassCB.FarZ = 1000.0f;
    mainPassCB.TotalTime = gt.TotalTime();
    mainPassCB.DeltaTime = gt.DeltaTime();
    mainPassCB.AmbientLight = Vector4{0.25f, 0.25f, 0.35f, 1.0f};

    for (int i = 0; i < MaxLights; ++i)
    {
        if (i < lights.size())
        {
            mainPassCB.Lights[i] = lights[i]->GetData();
        }
        else
        {
            break;
        }
    }

    mainPassCB.Lights[0].Direction = mRotatedLightDirections[0];
    mainPassCB.Lights[0].Strength = Vector3{0.9f, 0.8f, 0.7f};
    mainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
    mainPassCB.Lights[1].Strength = Vector3{0.4f, 0.4f, 0.4f};
    mainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
    mainPassCB.Lights[2].Strength = Vector3{0.2f, 0.2f, 0.2f};

    auto currentPassCB = currentFrameResource->PrimePassConstantUploadBuffer;
    currentPassCB->CopyData(0, mainPassCB);
}

void HybridGrassApp::UpdateSsaoCB(const GameTimer& gt)
{
    SsaoConstants ssaoCB;

    auto P = camera->GetProjectionMatrix();

    // Transform NDC space [-1,+1]^2 to texture space [0,1]^2
    Matrix T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f);

    ssaoCB.Proj = mainPassCB.Proj;
    ssaoCB.InvProj = mainPassCB.InvProj;
    XMStoreFloat4x4(&ssaoCB.ProjTex, XMMatrixTranspose(P * T));

    //for (int i = 0; i < GraphicAdapterCount; ++i)
    {
        ambientPrimePath->GetOffsetVectors(ssaoCB.OffsetVectors);

        auto blurWeights = ambientPrimePath->CalcGaussWeights(2.5f);
        ssaoCB.BlurWeights[0] = Vector4(&blurWeights[0]);
        ssaoCB.BlurWeights[1] = Vector4(&blurWeights[4]);
        ssaoCB.BlurWeights[2] = Vector4(&blurWeights[8]);

        ssaoCB.InvRenderTargetSize = Vector2(1.0f / ambientPrimePath->SsaoMapWidth(),
                                             1.0f / ambientPrimePath->SsaoMapHeight());

        // Coordinates given in view space.
        ssaoCB.OcclusionRadius = 0.5f;
        ssaoCB.OcclusionFadeStart = 0.2f;
        ssaoCB.OcclusionFadeEnd = 1.0f;
        ssaoCB.SurfaceEpsilon = 0.05f;

        auto currSsaoCB = currentFrameResource->SsaoConstantUploadBuffer;
        currSsaoCB->CopyData(0, ssaoCB);
    }
}

bool HybridGrassApp::InitMainWindow()
{
    MainWindow = CreateRenderWindow(primeDevice, mainWindowCaption, 1920, 1080, false);

    logQueue.Push(std::wstring(L"\nInit Window"));
    return true;
}

void HybridGrassApp::OnResize()
{
    D3DApp::OnResize();

    fullViewport.Height = static_cast<float>(MainWindow->GetClientHeight());
    fullViewport.Width = static_cast<float>(MainWindow->GetClientWidth());
    fullViewport.MinDepth = 0.0f;
    fullViewport.MaxDepth = 1.0f;
    fullViewport.TopLeftX = 0;
    fullViewport.TopLeftY = 0;
    fullRect = D3D12_RECT{0, 0, MainWindow->GetClientWidth(), MainWindow->GetClientHeight()};


    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = GetSRGBFormat(BackBufferFormat);
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    for (int i = 0; i < globalCountFrameResources; ++i)
    {
        MainWindow->GetBackBuffer(i).CreateRenderTargetView(&rtvDesc, &frameResources[i]->BackBufferRTVMemory);
    }


    if (camera != nullptr)
    {
        camera->SetAspectRatio(AspectRatio());
    }

    if (ambientPrimePath != nullptr)
    {
        ambientPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
        ambientPrimePath->RebuildDescriptors();
    }

    if (antiAliasingPrimePath != nullptr)
    {
        antiAliasingPrimePath->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
    }

    currentFrameResourceIndex = MainWindow->GetCurrentBackBufferIndex();
}

void HybridGrassApp::Flush()
{
    primeDevice->Flush();
    secondDevice->Flush();
}

void HybridGrassApp::InitImGui()
{
    if (!MainWindow || !primeDevice)
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    imguiIniFilePath = (std::filesystem::current_path() / "imgui-settings.ini").string();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = imguiIniFilePath.c_str();
    ImGui::LoadIniSettingsFromDisk(io.IniFilename);

    imguiSrvDescriptors = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                         globalCountFrameResources);
    imguiFontDescriptorInUse = false;

    ImGui_ImplWin32_Init(MainWindow->GetWindowHandle());

    ImGui_ImplDX12_InitInfo init_info = {};
    init_info.Device = primeDevice->GetDXDevice().Get();
    init_info.CommandQueue = primeDevice->GetCommandQueue(GQueueType::Graphics)->GetD3D12CommandQueue().Get();
    init_info.NumFramesInFlight = globalCountFrameResources;
    init_info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    init_info.UserData = this;
    {
        const std::shared_ptr<GDescriptorHeap> heap = imguiSrvDescriptors.GetDescriptorHeap();
        init_info.SrvDescriptorHeap = heap->GetDirectxHeap();
    }

    init_info.SrvDescriptorAllocFn = HybridGrassApp_ImGuiSrvAllocFn;
    init_info.SrvDescriptorFreeFn = HybridGrassApp_ImGuiSrvFreeFn;

    ImGui_ImplDX12_Init(&init_info);
    ImGui_ImplDX12_CreateDeviceObjects();
    imguiInitialized = true;
}

void HybridGrassApp::ShutdownImGui()
{
    if (!imguiInitialized)
        return;

    Flush();
    ReleaseWindGradientPreviewTexture();

    if (!imguiIniFilePath.empty())
    {
        ImGui::SaveIniSettingsToDisk(imguiIniFilePath.c_str());
    }
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();

    ImGui::DestroyContext();
    imguiSrvDescriptors = GDescriptor();
    imguiInitialized = false;
}

void HybridGrassApp::GetGrassWindFieldExtents(float& outCenterX, float& outCenterZ, float& outHalfExtent) const
{
    outCenterX = 0.0f;
    outCenterZ = 0.0f;
    outHalfExtent = std::max(200.0f, grassWorldSize * 0.5f);

    if (grassFieldTransform)
    {
        const Vector3 p = grassFieldTransform->GetWorldPosition();
        const Vector3 s = grassFieldTransform->GetScale();
        outCenterX = p.x;
        outCenterZ = p.z;
        outHalfExtent = std::max(outHalfExtent, grassWorldSize * 0.5f *
                                                   std::max(std::fabs(s.x), std::fabs(s.z)));
        return;
    }

    if (!crossGrassEmitters.empty() && crossGrassEmitters[0] && crossGrassEmitters[0]->gameObject)
    {
        const auto t = crossGrassEmitters[0]->gameObject->GetTransform();
        if (t)
        {
            const Vector3 p = t->GetWorldPosition();
            const Vector3 s = t->GetScale();
            outCenterX = p.x;
            outCenterZ = p.z;
            outHalfExtent = std::max(outHalfExtent, grassWorldSize * 0.5f *
                                                       std::max(std::fabs(s.x), std::fabs(s.z)));
        }
    }
}

bool HybridGrassApp::TryPickGrassGroundFromMouse(const int clientX, const int clientY, Vector3& outHitWorld) const
{
    if (!camera || !MainWindow || !camera->gameObject)
        return false;

    const float winW = static_cast<float>(MainWindow->GetClientWidth());
    const float winH = static_cast<float>(MainWindow->GetClientHeight());
    if (winW <= 1.0f || winH <= 1.0f)
        return false;

    const float ndcX = (2.f * static_cast<float>(clientX) / winW) - 1.f;
    const float ndcY = -(2.f * static_cast<float>(clientY) / winH) + 1.f;

    const Matrix invVP = (camera->GetViewMatrix() * camera->GetProjectionMatrix()).Invert();
    Vector4 hFar = Vector4::Transform(Vector4(ndcX, ndcY, 1.f, 1.f), invVP);
    if (fabsf(hFar.w) < 1e-5f)
        return false;
    hFar /= hFar.w;

    const Vector3 worldFar(hFar.x, hFar.y, hFar.z);
    const Vector3 eye = camera->gameObject->GetTransform()->GetWorldPosition();

    Vector3 rayDir = worldFar - eye;
    const float rLen = rayDir.Length();
    if (rLen < 1e-5f)
        return false;
    rayDir /= rLen;

    float groundY = 0.f;
    if (grassFieldTransform)
        groundY = grassFieldTransform->GetWorldPosition().y;
    else if (!crossGrassEmitters.empty() && crossGrassEmitters[0] && crossGrassEmitters[0]->gameObject)
        groundY = crossGrassEmitters[0]->gameObject->GetTransform()->GetWorldPosition().y;

    if (fabsf(rayDir.y) < 1e-5f)
        return false;

    const float t = (groundY - eye.y) / rayDir.y;
    if (!(std::isfinite(t)) || t < 0.f)
        return false;

    outHitWorld = eye + rayDir * t;
    outHitWorld.y = groundY;
    return true;
}

void HybridGrassApp::EnsureWindGradientPreviewTexture()
{
    if (windGradientPreviewReady)
        return;
    InitWindGradientPreviewTexture();
}

void HybridGrassApp::InitWindGradientPreviewTexture()
{
    windGradientPreviewReady = false;

    if (!primeDevice ||
        windGradientPreviewSrvIndex >= static_cast<UINT>(globalCountFrameResources))
        return;

    const UINT w = windGradientPreviewW;
    const UINT h = windGradientPreviewH;

    ID3D12Device* device = primeDevice->GetDXDevice().Get();

    CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
    const CD3DX12_RESOURCE_DESC texDesc =
        CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM,
                                     static_cast<UINT64>(w), h, 1, 1);

    if (FAILED(device->CreateCommittedResource(
            &heapDefault,
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(windGradientPreviewTexture.GetAddressOf()))))
        return;

    const unsigned rowBytesUnaligned = static_cast<unsigned>(w) * sizeof(uint32_t);
    const UINT rowPitchAligned = (rowBytesUnaligned + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
    windGradientPreviewRowPitch = rowPitchAligned;
    const UINT64 uploadByteSize = static_cast<UINT64>(rowPitchAligned) * static_cast<UINT64>(h);

    CD3DX12_HEAP_PROPERTIES heapUpload(D3D12_HEAP_TYPE_UPLOAD);
    const CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadByteSize);

    if (FAILED(device->CreateCommittedResource(
            &heapUpload,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(windGradientPreviewUpload.GetAddressOf()))))
    {
        windGradientPreviewTexture.Reset();
        return;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MostDetailedMip = 0;
    srv.Texture2D.MipLevels = 1;
    srv.Texture2D.ResourceMinLODClamp = 0.0f;
    srv.Texture2D.PlaneSlice = 0;

    device->CreateShaderResourceView(
        windGradientPreviewTexture.Get(),
        &srv,
        imguiSrvDescriptors.GetCPUHandle(windGradientPreviewSrvIndex));
    windGradientPreviewSrvGpu = imguiSrvDescriptors.GetGPUHandle(windGradientPreviewSrvIndex);
    windGradientPreviewReady = true;
    windFluidGpuPreviewCacheValid_ = false;
    windFluidGpuPreviewCache_.clear();
}

void HybridGrassApp::ReleaseWindGradientPreviewTexture()
{
    windGradientPreviewTexture.Reset();
    windGradientPreviewUpload.Reset();
    windGradientPreviewSrvGpu.ptr = 0;
    windGradientPreviewReady = false;
    windGradientPreviewShowsGpuFluid_ = false;
    windFluidReadbackSecond_.Reset();
    windFluidReadbackGrid_ = 0;
    windFluidRbTotalBytes_ = 0;
    windFluidRbLayout_ = {};
    windFluidReadbackFenceValue_ = 0;
    windFluidReadbackQueued_ = false;
    windFluidReadbackCpu_.clear();
    windFluidGpuPreviewCacheValid_ = false;
    windFluidGpuPreviewCache_.clear();
    windFluidGpuPreviewFrameCounter_ = 0;
    windPreviewDye_.clear();
    windPreviewDyeTmp_.clear();
    windPreviewDyeValid_ = false;
}

void HybridGrassApp::EnsureWindFluidReadbackMatchesVelocity(ID3D12Resource* velocityTex)
{
    if (!velocityTex || !secondDevice)
        return;

    const D3D12_RESOURCE_DESC desc = velocityTex->GetDesc();
    const UINT grid = static_cast<UINT>(desc.Width);
    if (grid == 0 || grid != desc.Height)
        return;

    UINT numRows = 0;
    UINT64 rowSize = 0;
    UINT64 totalBytes = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    secondDevice->GetDXDevice()->GetCopyableFootprints(&desc, 0u, 1u, 0ull, &footprint, &numRows, &rowSize,
                                                       &totalBytes);

    const bool unchanged = windFluidReadbackSecond_ && windFluidReadbackGrid_ == grid &&
                           windFluidRbTotalBytes_ == totalBytes;

    windFluidRbLayout_ = footprint;
    if (unchanged)
        return;

    windFluidReadbackSecond_.Reset();
    windFluidReadbackGrid_ = grid;
    windFluidRbTotalBytes_ = totalBytes;

    CD3DX12_HEAP_PROPERTIES heapReadback(D3D12_HEAP_TYPE_READBACK);
    const CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(totalBytes);

    HRESULT hr = secondDevice->GetDXDevice()->CreateCommittedResource(
        &heapReadback,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(windFluidReadbackSecond_.GetAddressOf()));
    if (FAILED(hr))
    {
        windFluidReadbackSecond_.Reset();
        windFluidReadbackGrid_ = 0;
        windFluidRbTotalBytes_ = 0;
        windFluidRbLayout_ = {};
    }
    else
    {
        windFluidReadbackFenceValue_ = 0;
        windFluidReadbackCpu_.clear();
    }
}

void HybridGrassApp::AppendWindFluidPreviewReadbackIfDue(const std::shared_ptr<GCommandList>& cmdList)
{
    if (!cmdList || !UseCrossAdapter || !secondDevice || crossGrassEmitters.empty() ||
        crossGrassEmitters[0] == nullptr)
        return;

    CrossAdapterGrassEmitter* emitter = crossGrassEmitters[0];
    if (!emitter->IsCrossAdapterSharedComputeActive() || !emitter->IsWindFluidGpuReady())
        return;

    const uint32_t intervalFrames = (windPreviewMode_ == 2) ? 20u : 8u;
    if ((++windFluidGpuPreviewFrameCounter_ % intervalFrames) != 0u)
        return;

    Microsoft::WRL::ComPtr<ID3D12Resource> vel = emitter->GetExpandWindVelocityResource();
    if (!vel)
        return;

    EnsureWindFluidReadbackMatchesVelocity(vel.Get());
    if (!windFluidReadbackSecond_ || windFluidRbTotalBytes_ == 0)
        return;

    cmdList->TransitionBarrier(vel, D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->TransitionBarrier(windFluidReadbackSecond_, D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->FlushResourceBarriers();

    CD3DX12_TEXTURE_COPY_LOCATION dstRb(windFluidReadbackSecond_.Get(), windFluidRbLayout_);
    CD3DX12_TEXTURE_COPY_LOCATION srcVel(vel.Get(), 0u);
    cmdList->GetGraphicsCommandList()->CopyTextureRegion(&dstRb, 0, 0, 0, &srcVel, nullptr);

    cmdList->TransitionBarrier(vel, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(windFluidReadbackSecond_, D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();

    windFluidReadbackQueued_ = true;
}

bool HybridGrassApp::TryRebuildWindGradientPreviewFromSecondGpu(UINT8* mapped)
{
    if (!mapped || !secondDevice || crossGrassEmitters.empty() || crossGrassEmitters[0] == nullptr)
        return false;

    const size_t cacheBytes =
        static_cast<size_t>(windGradientPreviewRowPitch) * static_cast<size_t>(windGradientPreviewH);

    CrossAdapterGrassEmitter* emitter = crossGrassEmitters[0];
    if (!emitter->IsCrossAdapterSharedComputeActive() || !emitter->IsWindFluidGpuReady())
        return false;

    if (!windFluidReadbackSecond_ || windFluidRbTotalBytes_ == 0)
        return false;

    if (windFluidGpuPreviewCacheValid_ && windFluidGpuPreviewCache_.size() == cacheBytes)
    {
        const auto computeQueue = secondDevice->GetCommandQueue(GQueueType::Compute);
        const ComPtr<ID3D12Fence> fence = computeQueue ? computeQueue->GetFence() : nullptr;
        if (!fence || fence->GetCompletedValue() < windFluidReadbackFenceValue_)
        {
            std::memcpy(mapped, windFluidGpuPreviewCache_.data(), cacheBytes);
            return true;
        }
    }

    if (windFluidReadbackFenceValue_ == 0)
        return false;

    const auto computeQueue = secondDevice->GetCommandQueue(GQueueType::Compute);
    if (!computeQueue)
        return false;

    WaitForSharedFenceValueCpu(computeQueue->GetFence(), windFluidReadbackFenceValue_);

    BYTE* mappedReadback = nullptr;
    if (FAILED(windFluidReadbackSecond_->Map(0u, nullptr, reinterpret_cast<void**>(&mappedReadback))))
    {
        if (windFluidGpuPreviewCacheValid_ && windFluidGpuPreviewCache_.size() == cacheBytes)
        {
            std::memcpy(mapped, windFluidGpuPreviewCache_.data(), cacheBytes);
            return true;
        }
        return false;
    }

    const UINT rbPitchBytes = windFluidRbLayout_.Footprint.RowPitch;
    const UINT8* fluidRows =
        reinterpret_cast<const UINT8*>(mappedReadback) + windFluidRbLayout_.Offset;

    if (windFluidReadbackCpu_.size() != windFluidRbTotalBytes_)
        windFluidReadbackCpu_.resize(static_cast<size_t>(windFluidRbTotalBytes_));
    std::memcpy(windFluidReadbackCpu_.data(), mappedReadback, static_cast<size_t>(windFluidRbTotalBytes_));

    const size_t dyeSize = static_cast<size_t>(windGradientPreviewW) * static_cast<size_t>(windGradientPreviewH);
    if (windPreviewDye_.size() != dyeSize)
    {
        windPreviewDye_.assign(dyeSize, 0.0f);
        windPreviewDyeTmp_.assign(dyeSize, 0.0f);
        windPreviewDyeValid_ = false;
        windPreviewDyeExposure_ = 4.0f;
    }
    if (!windPreviewDyeValid_)
    {
        std::fill(windPreviewDye_.begin(), windPreviewDye_.end(), 0.0f);
        windPreviewDyeValid_ = true;
        windPreviewDyeExposure_ = 4.0f;
    }

    const UINT gridW = std::max(windFluidRbLayout_.Footprint.Width, 1u);
    const UINT gridH = std::max(windFluidRbLayout_.Footprint.Height, gridW);

    for (UINT py = 0; py < windGradientPreviewH; ++py)
    {
        UINT8* rowBase = mapped + static_cast<size_t>(py) * windGradientPreviewRowPitch;
        for (UINT px = 0; px < windGradientPreviewW; ++px)
        {
            const float su = (static_cast<float>(px) + 0.5f) / static_cast<float>(windGradientPreviewW);
            const float sv = (static_cast<float>(py) + 0.5f) / static_cast<float>(windGradientPreviewH);
            Vector3 rgb = Vector3::Zero;
            const Vector2 wind =
                SampleFluidRg16Bilinear(fluidRows, rbPitchBytes, gridW, gridH, su, sv);
            if (windPreviewMode_ == 2)
            {
                const float mag = std::sqrt(std::max(wind.x * wind.x + wind.y * wind.y, 0.0f));
                const float vis = std::pow(std::clamp(mag * 0.008f, 0.0f, 1.0f), 0.55f);
                rgb = Vector3(vis * 0.88f, vis * 0.94f, vis);
            }
            else if (windPreviewMode_ == 1)
            {
                rgb = WindVectorToPreviewColorSigned(wind, 0.08f);
            }
            else
            {
                rgb = WindVectorToPreviewColorAbs(wind, 0.008f);
            }
            const float wallMask = PreviewWallMask(su, sv, grassGpuWindWallEnable,
                                                   grassGpuWindWallPosU, grassGpuWindWallPosV,
                                                   grassGpuWindWallAngleDeg * (3.1415926535f / 180.0f),
                                                   grassGpuWindWallHalfLength, grassGpuWindWallHalfWidth);
            if (wallMask > 0.0f)
            {
                const float a = std::clamp(wallMask, 0.0f, 1.0f);
                rgb = rgb * (1.0f - a) + Vector3(0.5f, 0.5f, 0.5f) * a;
            }

            const auto enc = [](const float c) -> UINT8 {
                const int v =
                    static_cast<int>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
                return static_cast<UINT8>(v);
            };

            rowBase[static_cast<size_t>(px) * 4u + 0u] = enc(rgb.x);
            rowBase[static_cast<size_t>(px) * 4u + 1u] = enc(rgb.y);
            rowBase[static_cast<size_t>(px) * 4u + 2u] = enc(rgb.z);
            rowBase[static_cast<size_t>(px) * 4u + 3u] = 255;
        }
    }

    if (windFluidGpuPreviewCache_.size() != cacheBytes)
        windFluidGpuPreviewCache_.resize(cacheBytes);
    std::memcpy(windFluidGpuPreviewCache_.data(), mapped, cacheBytes);
    windFluidGpuPreviewCacheValid_ = true;

    windFluidReadbackSecond_->Unmap(0u, nullptr);
    return true;
}

void HybridGrassApp::RefreshWindGradientPreviewTexture(const std::shared_ptr<GCommandList>& cmdList)
{
    if (!windGradientPreviewReady || !cmdList || !windGradientPreviewTexture ||
        !windGradientPreviewUpload)
        return;

    UINT8* mapped = nullptr;
    const D3D12_RANGE readRange = {0, 0};
    if (FAILED(windGradientPreviewUpload->Map(0, &readRange,
                                               reinterpret_cast<void**>(&mapped))))
        return;

    const bool gpuEligible =
        UseCrossAdapter && secondDevice && !crossGrassEmitters.empty() && crossGrassEmitters[0] &&
        crossGrassEmitters[0]->IsCrossAdapterSharedComputeActive() &&
        crossGrassEmitters[0]->IsWindFluidGpuReady();

    if (grassLod0DebugGradient && showWindFieldDebug)
    {
        for (UINT py = 0; py < windGradientPreviewH; ++py)
        {
            UINT8* rowBase = mapped + static_cast<size_t>(py) * windGradientPreviewRowPitch;
            for (UINT px = 0; px < windGradientPreviewW; ++px)
            {
                const float su = (static_cast<float>(px) + 0.5f) / static_cast<float>(windGradientPreviewW);
                const float sv = (static_cast<float>(py) + 0.5f) / static_cast<float>(windGradientPreviewH);
                const Vector2 wind = SampleLod0DebugGradientWindFromNormalizedUv(
                    su, sv, grassLod0DebugGradMin, grassLod0DebugGradMax, grassLod0DebugGradAxis);
                Vector3 rgb = WindVectorToPreviewColorAbs(wind, 0.008f);
                const auto enc = [](const float c) -> UINT8 {
                    return static_cast<UINT8>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
                };
                rowBase[static_cast<size_t>(px) * 4 + 0] = enc(rgb.x);
                rowBase[static_cast<size_t>(px) * 4 + 1] = enc(rgb.y);
                rowBase[static_cast<size_t>(px) * 4 + 2] = enc(rgb.z);
                rowBase[static_cast<size_t>(px) * 4 + 3] = 255;
            }
        }
        windGradientPreviewShowsGpuFluid_ = false;
    }
    else
    {
    const bool usedGpuFluid =
        windPreviewLiveGpuReadback_ && gpuEligible &&
        TryRebuildWindGradientPreviewFromSecondGpu(mapped);
    windGradientPreviewShowsGpuFluid_ = usedGpuFluid;

    if (!usedGpuFluid)
    {
        float fieldCenterX = 0.f;
        float fieldCenterZ = 0.f;
        float fieldHalf = 50.f;
        GetGrassWindFieldExtents(fieldCenterX, fieldCenterZ, fieldHalf);

        const uint32_t originCount = static_cast<uint32_t>(
            std::clamp(grassWindOriginCount, 0, 4));

        for (UINT py = 0; py < windGradientPreviewH; ++py)
        {
            UINT8* rowBase = mapped + static_cast<size_t>(py) * windGradientPreviewRowPitch;
            for (UINT px = 0; px < windGradientPreviewW; ++px)
            {
                const float fx = (-0.5f +
                                  ((static_cast<float>(px) + 0.5f) / static_cast<float>(windGradientPreviewW)))
                    * 2.0f * fieldHalf;
                const float fz = (-0.5f +
                                  ((static_cast<float>(py) + 0.5f) / static_cast<float>(windGradientPreviewH)))
                    * 2.0f * fieldHalf;

                const Vector3 worldPos(fieldCenterX + fx, 0.f, fieldCenterZ + fz);
                const Vector2 wind = SampleWindGradientHlslXZ(
                    worldPos, originCount, grassWindOrigins, grassWindDirections,
                    grassWindMapFalloff, Vector2::Zero);

                float su = (worldPos.x - fieldCenterX) / (2.0f * std::max(fieldHalf, 1e-4f)) + 0.5f;
                float sv = (worldPos.z - fieldCenterZ) / (2.0f * std::max(fieldHalf, 1e-4f)) + 0.5f;
                su = std::clamp(su, 0.0f, 1.0f);
                sv = std::clamp(sv, 0.0f, 1.0f);
                Vector3 rgb = (windPreviewMode_ == 1)
                                  ? WindVectorToPreviewColorSigned(wind, 0.08f)
                                  : WindVectorToPreviewColorAbs(wind, 0.008f);
                const float wallMask = PreviewWallMask(su, sv, grassGpuWindWallEnable,
                                                       grassGpuWindWallPosU, grassGpuWindWallPosV,
                                                       grassGpuWindWallAngleDeg * (3.1415926535f / 180.0f),
                                                       grassGpuWindWallHalfLength, grassGpuWindWallHalfWidth);
                if (wallMask > 0.0f)
                {
                    const float a = std::clamp(wallMask, 0.0f, 1.0f);
                    rgb = rgb * (1.0f - a) + Vector3(0.5f, 0.5f, 0.5f) * a;
                }

                const auto enc = [=](const float c) -> UINT8 {
                    const int v =
                        static_cast<int>(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f));
                    return static_cast<UINT8>(v);
                };

                rowBase[static_cast<size_t>(px) * 4 + 0] = enc(rgb.x);
                rowBase[static_cast<size_t>(px) * 4 + 1] = enc(rgb.y);
                rowBase[static_cast<size_t>(px) * 4 + 2] = enc(rgb.z);
                rowBase[static_cast<size_t>(px) * 4 + 3] = 255;
            }
        }
    }
    }

    windGradientPreviewUpload->Unmap(0, nullptr);

    cmdList->TransitionBarrier(windGradientPreviewTexture, D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->FlushResourceBarriers();

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    fp.Offset = 0;
    fp.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    fp.Footprint.Width = windGradientPreviewW;
    fp.Footprint.Height = windGradientPreviewH;
    fp.Footprint.Depth = 1;
    fp.Footprint.RowPitch = windGradientPreviewRowPitch;

    CD3DX12_TEXTURE_COPY_LOCATION dstLoc(windGradientPreviewTexture.Get(), 0);
    CD3DX12_TEXTURE_COPY_LOCATION srcLoc(windGradientPreviewUpload.Get(), fp);
    cmdList->GetGraphicsCommandList()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    cmdList->TransitionBarrier(windGradientPreviewTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

void HybridGrassApp::DrawImGui(const std::shared_ptr<GCommandList>& cmdList)
{
    if (!imguiInitialized || !cmdList)
        return;

    cmdList->SetDescriptorsHeap(&imguiSrvDescriptors);

    if (showWindFieldDebug)
        EnsureWindGradientPreviewTexture();

    if (windGradientPreviewReady && showWindFieldDebug)
        RefreshWindGradientPreviewTexture(cmdList);

    if (ImGui::Begin("Navier-Stokes Wind", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        bool useMultiGpuRender = UseCrossAdapter;
        if (!HaveTwoHardwareAdapters)
        {
            ImGui::BeginDisabled();
            useMultiGpuRender = false;
        }
        if (ImGui::Checkbox("Use Multi-GPU render", &useMultiGpuRender))
        {
            if (useMultiGpuRender && !CrossAdapterFencesCreated)
            {
                TryCreateCrossAdapterFences();
            }
            if (!CrossAdapterFencesCreated)
            {
                useMultiGpuRender = false;
            }

            UseCrossAdapter = useMultiGpuRender && CrossAdapterFencesCreated;

            for (auto* emitter : crossEmitter)
            {
                if (UseCrossAdapter)
                    emitter->EnableShared();
                else
                    emitter->DisableShared();
            }

            for (auto* emitter : crossGrassEmitters)
            {
                if (UseCrossAdapter)
                    emitter->EnableShared();
                else
                    emitter->DisableShared();
            }
        }
        if (!HaveTwoHardwareAdapters)
        {
            ImGui::EndDisabled();
            ImGui::TextDisabled("(requires two hardware GPUs)");
        }
        else if (!CrossAdapterFencesCreated)
        {
            ImGui::TextDisabled("(cross-adapter fences not created — check the box to try again)");
        }

        ImGui::Text("Cross-adapter compute: %s", UseCrossAdapter ? "on (second GPU)" : "off (prime GPU)");
        if (UseCrossAdapter)
        {
            ImGui::TextDisabled("Cross-adapter GPU fences: on (automatic shared fences)");
        }

        ImGui::SeparatorText("Grass field");
        ImGui::SliderInt("Blade count (density)", &grassBladeCount, 500, 500000);
        ImGui::SliderFloat("Patch world size", &grassWorldSize, 100.0f, 8000.0f, "%.0f");
        ImGui::SliderFloat("Field scale XZ", &grassFieldScaleXZ, 1.0f, 40.0f, "%.1f");
        ImGui::SliderFloat("Field scale Y", &grassFieldScaleY, 1.0f, 40.0f, "%.1f");
        {
            const float spanXZ = std::max(grassWorldSize, 1.0f) * std::max(grassFieldScaleXZ, 0.1f);
            const float patchArea = spanXZ * spanXZ;
            const float bladesPerSqUnit = static_cast<float>(std::max(1, grassBladeCount)) / patchArea;
            ImGui::TextDisabled("Effective span ~%.0f x %.0f world units", spanXZ, spanXZ);
            ImGui::TextDisabled("Density ~%.4f blades / world unit²", bladesPerSqUnit);
        }
        ImGui::SliderInt("LOD0 blades per tuft", &grassLod0BladeCount, 1, 4);
        ImGui::SliderInt("LOD1 blades per tuft", &grassLod1BladeCount, 1, 4);
        ImGui::SliderFloat("LOD0 blade width", &grassLod0BladeWidthScale, 0.25f, 3.0f, "%.2f");
        ImGui::SliderFloat("LOD0 blade height", &grassLod0BladeHeightScale, 0.25f, 3.0f, "%.2f");
        ImGui::SliderFloat("LOD1 blade width", &grassLod1BladeWidthScale, 0.25f, 3.0f, "%.2f");
        ImGui::SliderFloat("LOD1 blade height", &grassLod1BladeHeightScale, 0.25f, 3.0f, "%.2f");
        ImGui::SliderInt("LOD0 base segments", &grassLod0BaseSegments, 2, 8);
        ImGui::SliderFloat("Wind tessellation scale", &grassWindTessellationScale, 1.0f, 8.0f, "%.1f");
        if (ImGui::Button("Apply field size / density"))
        {
            pendingGrassWorldSize = std::max(1.0f, grassWorldSize);
            pendingGrassBladeCount = std::max(1, grassBladeCount);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Rebuilds grass buffers (can hitch).");

        ImGui::SeparatorText("Flow forcing");
        ImGui::TextDisabled("Base wind is constant directional flow; LMB adds radial disturbances.");
        ImGui::SliderFloat("Base flow strength", &grassWindBaseStrength, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Base flow angle (deg)", &grassWindBaseAngleDeg, -180.0f, 180.0f, "%.1f");
        ImGui::SliderFloat("Base flow coverage", &grassWindBaseCoverage, 0.25f, 2.5f, "%.2f");
        ImGui::SliderFloat("Wind cursor radius", &grassWindCursorRadius, 50.0f, 5000.0f, "%.0f");
        ImGui::SliderFloat("Wind cursor strength", &grassWindCursorStrength, 0.0f, 8.0f, "%.2f");
        ImGui::TextDisabled("Hold LMB on the 3D view (not ImGui) to inject a local pulse into the flow.");
        ImGui::SliderFloat("Wind map falloff", &grassWindMapFalloff, 0.1f, 6.0f, "%.2f");

        ImGui::SeparatorText("GPU wind fluid (cross-adapter expand)");
        if (!UseCrossAdapter)
        {
            ImGui::BeginDisabled();
        }
        ImGui::Checkbox("Enable 2D Navier-Stokes wind field", &grassGpuWindFluid);
        ImGui::SliderFloat("Analytic vs fluid blend", &grassGpuWindFluidBlend, 0.0f, 1.0f, "%.2f");
        if (grassGpuWindFluid && grassGpuWindFluidBlend < 0.95f)
            ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f),
                               "Low blend mixes LMB analytic gusts with fluid and can look intersecting.");
        ImGui::SliderInt("Pressure solve iterations", &grassGpuWindJacobiIterations, 4, 40);
        ImGui::SliderInt("Grid resolution", &grassGpuWindGridResolution, 32, 512);
        ImGui::SliderFloat("Inject strength", &grassGpuWindInjectStrength, 0.0f, 2.0f, "%.3f");
        ImGui::SliderFloat("Dissipation", &grassGpuWindDissipation, 0.90f, 0.9999f, "%.4f");
        ImGui::TextDisabled("Dissipation damps velocity each step (0.97-0.99 = softer, smoother flow).");
        grassGpuWindDt = std::clamp(grassGpuWindDt, 0.001f, 0.05f);
        ImGui::SliderFloat("Time step", &grassGpuWindDt, 0.001f, 0.05f, "%.4f");
        ImGui::SliderFloat("Vorticity epsilon", &grassGpuWindVorticityEps, 0.0f, 2.0f, "%.3f");
        if (ImGui::Button("Apply stable preset (faster)"))
        {
            grassGpuWindJacobiIterations = 20;
            grassGpuWindGridResolution = 128;
            grassGpuWindInjectStrength = 0.35f;
            grassGpuWindFluidBlend = 1.0f;
            grassGpuWindDissipation = 0.985f;
            grassGpuWindDt = 0.014f;
            grassGpuWindVorticityEps = 0.0f;
            windPreviewMode_ = 0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Use this first, then tune slowly.");
        ImGui::SeparatorText("Fluid obstacle (Shadertoy circle)");
        ImGui::Checkbox("Enable obstacle", &grassGpuWindWallEnable);
        ImGui::SliderFloat("Obstacle U", &grassGpuWindWallPosU, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("Obstacle V", &grassGpuWindWallPosV, 0.0f, 1.0f, "%.3f");
        ImGui::SliderFloat("Obstacle radius", &grassGpuWindWallHalfLength, 0.02f, 0.25f, "%.3f");
        ImGui::SliderFloat("Obstacle wake lean", &grassGpuWindWallWake, 0.0f, 200.0f, "%.2f");
        ImGui::TextDisabled("0 = upright under obstacle. 200 = max lean under circle + downstream wake.");
        if (!UseCrossAdapter)
        {
            ImGui::EndDisabled();
            ImGui::TextUnformatted("(Requires cross-adapter compute on second GPU.)");
        }
        if (UseCrossAdapter && !crossGrassEmitters.empty())
        {
            if (crossGrassEmitters[0]->IsWindFluidGpuReady())
                ImGui::TextUnformatted("GPU fluid sim: ready (textures update each compute frame).");
            else
            {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f),
                                   "GPU fluid sim: not initialized — only analytic wind is used.");
                ImGui::TextDisabled(
                    "[WindFluid] / [ComputePSO] lines in Debug Output show the real error (compile, HRESULT).\n"
                    "HLSL loads from Shaders\\WindFluid*.hlsl — cwd, then folders above the .exe (rebuild after fixes).");
            }
        }

        ImGui::SeparatorText("Grass LOD 0 wind (near camera)");
        grassLod1Distance = std::max(grassLod1Distance, grassLod0Distance);
        ImGui::SliderFloat("LOD0 max distance", &grassLod0Distance, 50.0f, grassCullMaxDistance, "%.0f");
        ImGui::SliderFloat("LOD1 max distance", &grassLod1Distance, grassLod0Distance, grassCullMaxDistance,
                           "%.0f");
        ImGui::SliderFloat("Cull max distance", &grassCullMaxDistance, 200.0f, 40000.0f, "%.0f");
        ImGui::SeparatorText("LOD0 debug linear gradient");
        ImGui::Checkbox("Use linear gradient (bypass fluid texture)", &grassLod0DebugGradient);
        if (grassLod0DebugGradient)
        {
            ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f),
                               "Debug gradient ON — GPU fluid map is bypassed for LOD0.");
        }
        else
        {
            ImGui::TextDisabled("Debug gradient OFF — LOD0 uses live GPU Navier-Stokes velocity map.");
        }
        if (grassLod0DebugGradient)
        {
            ImGui::SliderFloat("Gradient min |v| (world)", &grassLod0DebugGradMin, 0.0f, 200.0f, "%.1f");
            ImGui::SliderFloat("Gradient max |v| (world)", &grassLod0DebugGradMax, 0.0f, 200.0f, "%.1f");
            if (grassLod0DebugGradMax < grassLod0DebugGradMin)
                grassLod0DebugGradMax = grassLod0DebugGradMin;
            const char* gradAxes[] = {"Grass local X", "Grass local Z"};
            ImGui::Combo("Gradient axis", &grassLod0DebugGradAxis, gradAxes, IM_ARRAYSIZE(gradAxes));
            const float leanStartSpeed = 1.0f / 0.20f;
            const float minSpeed01 = grassLod0DebugGradMin * 0.20f;
            const float maxSpeed01 = std::min(1.0f, grassLod0DebugGradMax * 0.20f);
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.55f, 1.0f),
                               "Flow +world X. Gradient spans the grass patch (local coords).");
            ImGui::TextDisabled(
                "speed01 = saturate(|v| * 0.20). Visible lean begins above |v| ~%.1f (speed01>0).",
                leanStartSpeed);
            ImGui::TextDisabled(
                "At min: speed01=%.2f. At max: speed01=%.2f. Full bend cap near speed01=1 (|v|>=%.1f).",
                minSpeed01, maxSpeed01, leanStartSpeed);
            ImGui::TextDisabled(
                "Left side of grass = min (upright). Right side = max lean. Tips darken with gradient t.");
        }
        ImGui::SliderFloat("LOD0 SDOF freq", &grassLod0SdofNaturalFreq, 0.5f, 6.0f, "%.2f");
        ImGui::SliderFloat("LOD0 SDOF damping", &grassLod0SdofDampingRatio, 0.05f, 1.5f, "%.2f");
        ImGui::TextDisabled("SDOF sliders apply to single-GPU geometry path only.");
        ImGui::TextDisabled(
            "LOD0 (green gradient blades): driven ONLY by GPU wind map velocity.\n"
            "Requires cross-adapter + Navier-Stokes enabled. Turn OFF debug linear gradient.\n"
            "Circle wake lean comes from fluid flow around the obstacle, not extra sliders.");
        ImGui::SliderFloat("LOD0 fluid lean gain", &grassLod0LeanGain, 0.5f, 12.0f, "%.2f");
        ImGui::TextDisabled("Scales GPU velocity map -> bend (debug gradient ignores this).");
        if (UseCrossAdapter && !crossGrassEmitters.empty() && crossGrassEmitters[0] != nullptr)
        {
            const auto& fp = crossGrassEmitters[0]->GetWindFieldWorldParams();
            ImGui::TextDisabled(
                "Expand field: center (%.0f, %.0f), half %.0f, cell %.2f world units",
                fp.x, fp.y, fp.z, fp.w);
            if (crossGrassEmitters[0]->IsCrossAdapterSharedComputeActive())
                ImGui::TextDisabled("Grass path: GPU expand + VS_Expanded (bend baked in compute).");
            else
                ImGui::TextColored(ImVec4(1.f, 0.55f, 0.2f, 1.f),
                                   "Grass path: single-GPU GS (does NOT read GPU wind map for LOD0).");
        }

        ImGui::SeparatorText("LOD1+ wind response");
        ImGui::SliderFloat("Field influence scale", &grassFieldInfluenceScale, 0.0f, 6.0f, "%.2f");
        ImGui::TextDisabled("Affects textured LOD1+ only; LOD0 ignores this.");
        ImGui::SliderFloat("Wind intensity", &grassWindIntensity, 0.0f, 200.0f, "%.2f");
        ImGui::SliderFloat("Wind amplitude", &grassWindAmplitude, 0.0f, 200.0f, "%.2f");

        ImGui::SeparatorText("Debug");
        const char* previewModes[] = {"Velocity abs (Shadertoy)", "Velocity direction (debug)", "Smoke magnitude"};
        ImGui::Combo("Preview mode", &windPreviewMode_, previewModes, IM_ARRAYSIZE(previewModes));
        if (windPreviewMode_ == 1)
            ImGui::TextColored(ImVec4(1.f, 0.75f, 0.2f, 1.f),
                               "Direction mode: use 'Velocity abs' to judge flow seams.");
        if (ImGui::Button("Use abs preview"))
            windPreviewMode_ = 0;
        ImGui::Checkbox("Show wind field debug", &showWindFieldDebug);
        ImGui::Checkbox("Live GPU preview readback", &windPreviewLiveGpuReadback_);
        ImGui::SliderInt("Wind field grid", &windFieldGridResolution, 1, 40);
        ImGui::SeparatorText(
            grassLod0DebugGradient
                ? "Wind preview (LOD0 debug linear gradient)"
                : (windGradientPreviewShowsGpuFluid_
                       ? "Wind velocity preview (live GPU fluid readback)"
                       : "Wind velocity preview (CPU analytic - GPU fluid unavailable)"));
        if (!showWindFieldDebug)
        {
            ImGui::TextDisabled("Preview updates paused (enable 'Show wind field debug').");
        }
        else if (windGradientPreviewReady && windGradientPreviewSrvGpu.ptr != 0)
        {
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(windGradientPreviewSrvGpu.ptr)),
                         ImVec2(288.0f, 288.0f));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_None))
            {
                if (windGradientPreviewShowsGpuFluid_)
                    ImGui::SetTooltip(
                        "Read-back of DXGI_FORMAT_R16G16_FLOAT wind velocity after the solver on the compute adapter.\n"
                        "Sampling matches ComputeGrass.hlsl SampleFluidWindXZ (world XZ projected into uv 0-1).\n"
                        "RG encodes direction scaled by strength; faint blue marks magnitude.");
                else
                    ImGui::SetTooltip(
                        "CPU reconstruction of analytic wind (constant base flow + LMB disturbances).\n"
                        "Use multi-GPU with shared grass compute and an initialized fluid sim for live GPU read-back.");
            }
            if (windGradientPreviewShowsGpuFluid_)
                ImGui::TextDisabled("%ux%u - GPU velocity read-back throttled (%s mode)",
                                    windGradientPreviewW,
                                    windGradientPreviewH,
                                    windPreviewMode_ == 2 ? "dye" : "velocity");
            else
                ImGui::TextDisabled("%ux%u - CPU analytic origins only, no live fluid read-back this frame",
                                    windGradientPreviewW,
                                    windGradientPreviewH);
        }
    }
    ImGui::End();

    if (showWindFieldDebug && camera != nullptr)
    {
        const Matrix viewProj = camera->GetViewMatrix() * camera->GetProjectionMatrix();
        const ImVec2 screenSize = ImGui::GetIO().DisplaySize;
        const int grid = std::clamp(windFieldGridResolution, 4, 40);
        const int activeOrigins = std::clamp(grassWindOriginCount, 0, 4);

        Vector4 gpuWindFieldParams{};
        bool haveGpuWindFieldParams = false;
        if (!crossGrassEmitters.empty() && crossGrassEmitters[0] != nullptr)
        {
            gpuWindFieldParams = crossGrassEmitters[0]->GetWindFieldWorldParams();
            haveGpuWindFieldParams = gpuWindFieldParams.z > 1e-4f;
        }

        float fieldCenterX = 0.0f;
        float fieldCenterZ = 0.0f;
        float fieldHalf = std::max(200.0f, grassWorldSize * 0.5f);
        if (haveGpuWindFieldParams)
        {
            fieldCenterX = gpuWindFieldParams.x;
            fieldCenterZ = gpuWindFieldParams.y;
            fieldHalf = gpuWindFieldParams.z;
        }
        else
        {
            GetGrassWindFieldExtents(fieldCenterX, fieldCenterZ, fieldHalf);
        }
        float groundY = 0.0f;
        if (grassFieldTransform)
            groundY = grassFieldTransform->GetWorldPosition().y;
        else if (!crossGrassEmitters.empty() && crossGrassEmitters[0] && crossGrassEmitters[0]->gameObject)
            groundY = crossGrassEmitters[0]->gameObject->GetTransform()->GetWorldPosition().y;

        bool mappedFluidForDebug = false;
        const UINT8* debugFluidRows = nullptr;
        UINT debugFluidPitch = 0;
        UINT debugFluidGrid = 0;
        const bool gpuDebugEligible =
            windGradientPreviewShowsGpuFluid_ && !windFluidReadbackCpu_.empty() &&
            windFluidReadbackGrid_ > 0 && windFluidRbLayout_.Footprint.RowPitch > 0;
        if (gpuDebugEligible)
        {
            mappedFluidForDebug = true;
            debugFluidRows =
                windFluidReadbackCpu_.data() + windFluidRbLayout_.Offset;
            debugFluidPitch = windFluidRbLayout_.Footprint.RowPitch;
            debugFluidGrid = windFluidReadbackGrid_;
        }

        const UINT debugGridW = windFluidRbLayout_.Footprint.Width > 0
                                    ? windFluidRbLayout_.Footprint.Width
                                    : debugFluidGrid;
        const UINT debugGridH = windFluidRbLayout_.Footprint.Height > 0
                                    ? windFluidRbLayout_.Footprint.Height
                                    : debugGridW;

        float debugGrassWorldSize = grassWorldSize;
        if (!crossGrassEmitters.empty() && crossGrassEmitters[0] != nullptr)
            debugGrassWorldSize = crossGrassEmitters[0]->GetEmitterWorldSize();
        const float debugGrassHalf = std::max(1.0f, debugGrassWorldSize * 0.5f);
        const float arrowFieldHalf = grassLod0DebugGradient ? debugGrassHalf : fieldHalf;

        const float baseAngleRadDbg = grassWindBaseAngleDeg * (3.1415926535f / 180.0f);
        const Vector2 debugFlowDir(std::cos(baseAngleRadDbg), std::sin(baseAngleRadDbg));

        auto sampleWindGradient = [&](const Vector3& worldPos) -> Vector2
        {
            if (grassLod0DebugGradient)
            {
                Vector3 localPos = worldPos;
                if (grassFieldTransform != nullptr)
                {
                    const Matrix invWorld = grassFieldTransform->GetWorldMatrix().Invert();
                    const Vector4 h = Vector4::Transform(
                        Vector4(worldPos.x, worldPos.y, worldPos.z, 1.0f), invWorld);
                    localPos = Vector3(h.x, h.y, h.z);
                }
                return SampleLod0DebugGradientWindLocal(
                    localPos, debugGrassWorldSize, grassLod0DebugGradMin, grassLod0DebugGradMax,
                    grassLod0DebugGradAxis, debugFlowDir);
            }
            if (mappedFluidForDebug && debugFluidRows)
            {
                const Vector4 fieldParams = haveGpuWindFieldParams
                                                ? gpuWindFieldParams
                                                : Vector4(fieldCenterX, fieldCenterZ, fieldHalf, 0.0f);
                const Vector2 uv = WorldPosToGrassPatchUv(worldPos, debugGrassWorldSize, grassFieldTransform.get());
                Vector2 simVel = SampleFluidRg16Bilinear(
                    debugFluidRows, debugFluidPitch, debugGridW, debugGridH, uv.x, uv.y);
                float cellWorld = fieldParams.w;
                if (cellWorld <= 1e-4f && debugFluidGrid > 0)
                    cellWorld = (2.0f * std::max(fieldParams.z, 1.0f)) / static_cast<float>(debugFluidGrid);
                return simVel * std::max(cellWorld, 1e-4f) * 2.0f;
            }
            return SampleWindGradientHlslXZ(
                worldPos,
                static_cast<uint32_t>(activeOrigins),
                grassWindOrigins,
                grassWindDirections,
                grassWindMapFalloff,
                Vector2::Zero);
        };

        auto project = [&](const Vector3& p, ImVec2& out) -> bool
        {
            const Vector4 clip = Vector4::Transform(Vector4(p.x, p.y, p.z, 1.0f), viewProj);
            if (clip.w <= 1e-4f)
                return false;
            const float invW = 1.0f / clip.w;
            const float ndcX = clip.x * invW;
            const float ndcY = clip.y * invW;
            if (ndcX < -1.6f || ndcX > 1.6f || ndcY < -1.6f || ndcY > 1.6f)
                return false;
            out.x = (ndcX * 0.5f + 0.5f) * screenSize.x;
            out.y = (1.0f - (ndcY * 0.5f + 0.5f)) * screenSize.y;
            return true;
        };

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        const float maxArrowWorldLen = std::clamp(arrowFieldHalf * 0.012f, 5.0f, 22.0f);
        const float arrowGain = 0.022f;
        for (int gx = 0; gx < grid; ++gx)
        {
            for (int gz = 0; gz < grid; ++gz)
            {
                const float fx = (static_cast<float>(gx) / static_cast<float>(grid - 1) - 0.5f) * 2.0f * arrowFieldHalf;
                const float fz = (static_cast<float>(gz) / static_cast<float>(grid - 1) - 0.5f) * 2.0f * arrowFieldHalf;
                const Vector3 p0(fieldCenterX + fx, groundY + 1.0f, fieldCenterZ + fz);
                const Vector2 w = sampleWindGradient(p0);
                const float wLen = w.Length();
                if (wLen < 1e-4f)
                    continue;
                float arrowLen = wLen * arrowGain;
                if (arrowLen < 0.12f)
                    continue;
                Vector2 arrowVec = (w / wLen) * arrowLen;
                float aLen = arrowLen;
                if (aLen > maxArrowWorldLen)
                {
                    arrowVec *= (maxArrowWorldLen / aLen);
                    aLen = maxArrowWorldLen;
                }
                const Vector3 p1 = p0 + Vector3(arrowVec.x, 0.0f, arrowVec.y);
                ImVec2 s0, s1;
                if (!project(p0, s0) || !project(p1, s1))
                    continue;
                const ImU32 col = IM_COL32(80, 220, 255, 200);
                dl->AddLine(s0, s1, col, 1.5f);
                const ImVec2 dir(s1.x - s0.x, s1.y - s0.y);
                const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
                if (len > 4.0f)
                {
                    const ImVec2 n(dir.x / len, dir.y / len);
                    const ImVec2 l(-n.y, n.x);
                    const ImVec2 tip = s1;
                    const ImVec2 a(tip.x - n.x * 6.0f + l.x * 3.0f, tip.y - n.y * 6.0f + l.y * 3.0f);
                    const ImVec2 b(tip.x - n.x * 6.0f - l.x * 3.0f, tip.y - n.y * 6.0f - l.y * 3.0f);
                    dl->AddTriangleFilled(tip, a, b, col);
                }
            }
        }
        dl->AddText(ImVec2(20.0f, 20.0f), IM_COL32(80, 220, 255, 255), "Wind field debug: ON");
    }

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmdList->GetGraphicsCommandList().Get());
}

LRESULT HybridGrassApp::MsgProc(const HWND hwnd, const UINT msg, const WPARAM wParam, const LPARAM lParam)
{
    if (imguiInitialized && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_INPUT:
        {
            UINT dataSize;
            GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dataSize,
                            sizeof(RAWINPUTHEADER));
            //Need to populate data size first

            if (dataSize > 0)
            {
                const auto rawdata = std::make_unique<BYTE[]>(dataSize);
                if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawdata.get(), &dataSize,
                                    sizeof(RAWINPUTHEADER)) == dataSize)
                {
                    auto raw = reinterpret_cast<RAWINPUT*>(rawdata.get());
                    if (raw->header.dwType == RIM_TYPEMOUSE)
                    {
                        mouse.OnMouseMoveRaw(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
                    }
                }
            }

            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    //Mouse Messages
    case WM_MOUSEMOVE:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnMouseMove(x, y);
            return 0;
        }
    case WM_LBUTTONDOWN:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnLeftPressed(x, y);
            return 0;
        }
    case WM_RBUTTONDOWN:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnRightPressed(x, y);
            return 0;
        }
    case WM_MBUTTONDOWN:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnMiddlePressed(x, y);
            return 0;
        }
    case WM_LBUTTONUP:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnLeftReleased(x, y);
            return 0;
        }
    case WM_RBUTTONUP:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnRightReleased(x, y);
            return 0;
        }
    case WM_MBUTTONUP:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            mouse.OnMiddleReleased(x, y);
            return 0;
        }
    case WM_MOUSEWHEEL:
        {
            const int x = LOWORD(lParam);
            const int y = HIWORD(lParam);
            if (GET_WHEEL_DELTA_WPARAM(wParam) > 0)
            {
                mouse.OnWheelUp(x, y);
            }
            else if (GET_WHEEL_DELTA_WPARAM(wParam) < 0)
            {
                mouse.OnWheelDown(x, y);
            }
            return 0;
        }
    case WM_KEYUP:

        {
            const unsigned char keycode = static_cast<unsigned char>(wParam);
            keyboard.OnKeyReleased(keycode);


            return 0;
        }
    case WM_KEYDOWN:
        {
            {
                const unsigned char keycode = static_cast<unsigned char>(wParam);
                if (keyboard.IsKeysAutoRepeat())
                {
                    keyboard.OnKeyPressed(keycode);
                }
                else
                {
                    const bool wasPressed = lParam & 0x40000000;
                    if (!wasPressed)
                    {
                        keyboard.OnKeyPressed(keycode);
                    }
                }
            }
        }

    case WM_CHAR:
        {
            const unsigned char ch = static_cast<unsigned char>(wParam);
            if (keyboard.IsCharsAutoRepeat())
            {
                keyboard.OnChar(ch);
            }
            else
            {
                const bool wasPressed = lParam & 0x40000000;
                if (!wasPressed)
                {
                    keyboard.OnChar(ch);
                }
            }
            return 0;
        }
    }

    return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}
