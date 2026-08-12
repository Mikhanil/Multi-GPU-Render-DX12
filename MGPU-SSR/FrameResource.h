#pragma once
#include "DirectXBuffers.h"
#include "ShaderBuffersData.h"
#include <array>
#include <cstddef>

using namespace PEPEngine;
using namespace Graphics;

inline constexpr UINT kReflectionProbeCount = 4;

struct ReflectionPassConstants : CommonPassConstants
{
    LightData DirectionalLight;
    UINT PointLightCount = 0;
    UINT SpotLightCount = 0;
    UINT LightPad0 = 0;
    UINT LightPad1 = 0;
    Vector4 SsrRaySettings = Vector4{80.0f, 0.45f, 0.18f, 0.85f};
    Vector4 SsrResolveSettings = Vector4{128.0f, 8.0f, 16.0f, 0.0f};
};

struct alignas(16) ReflectionProbeGpuData
{
    Vector4 Position{};
    Vector4 ProxyBoxMin{};
    Vector4 ProxyBoxMax{};
};

struct alignas(16) ReflectionProbeConstants
{
    std::array<ReflectionProbeGpuData, kReflectionProbeCount> Probes{};
};

static_assert(sizeof(Vector4) == 16, "CPU Vector4 must occupy one HLSL float4 register");
static_assert(alignof(ReflectionProbeGpuData) == 16, "Probe data must be 16-byte aligned");
static_assert(offsetof(ReflectionProbeGpuData, Position) == 0, "Position offset must match HLSL");
static_assert(offsetof(ReflectionProbeGpuData, ProxyBoxMin) == 16, "ProxyBoxMin offset must match HLSL");
static_assert(offsetof(ReflectionProbeGpuData, ProxyBoxMax) == 32, "ProxyBoxMax offset must match HLSL");
static_assert(sizeof(ReflectionProbeGpuData) == 48, "Each probe must occupy three HLSL registers");
static_assert(sizeof(ReflectionProbeConstants) == 48 * kReflectionProbeCount,
              "CPU reflection-probe constant buffer must match HLSL");

struct FrameResource
{
    FrameResource(std::shared_ptr<GDevice> primeDevice, std::shared_ptr<GDevice> secondDevice, UINT objectCount,
                  UINT materialCount, UINT pointLightCount, UINT spotLightCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource();

    std::shared_ptr<ConstantUploadBuffer<ReflectionPassConstants>> MainPassConstantUploadBuffer = nullptr;
    std::shared_ptr<ConstantUploadBuffer<ReflectionPassConstants>> ReflectionProbePassConstantUploadBuffer = nullptr;
    std::shared_ptr<ConstantUploadBuffer<ReflectionProbeConstants>> ReflectionProbeConstantUploadBuffer = nullptr;
    std::shared_ptr<ConstantUploadBuffer<ReflectionPassConstants>> SecondMainPassConstantUploadBuffer = nullptr;
    std::shared_ptr<ConstantUploadBuffer<ReflectionPassConstants>> SecondReflectionProbePassConstantUploadBuffer = nullptr;
    std::shared_ptr<ConstantUploadBuffer<ReflectionProbeConstants>> SecondReflectionProbeConstantUploadBuffer = nullptr;
    std::shared_ptr<ConstantUploadBuffer<ReflectionPassConstants>> SecondShadowPassConstantUploadBuffer = nullptr;
    std::shared_ptr<ConstantUploadBuffer<ReflectionPassConstants>> ShadowPassConstantUploadBuffer = nullptr;
    std::shared_ptr<ConstantUploadBuffer<SsaoConstants>> SsaoConstantUploadBuffer = nullptr;
    std::shared_ptr<StructuredUploadBuffer<MaterialConstants>> MaterialBuffer = nullptr;
    std::shared_ptr<StructuredUploadBuffer<MaterialConstants>> SecondMaterialBuffer = nullptr;
    std::shared_ptr<StructuredUploadBuffer<LightData>> PointLightBuffer = nullptr;
    std::shared_ptr<StructuredUploadBuffer<LightData>> SecondPointLightBuffer = nullptr;
    std::shared_ptr<StructuredUploadBuffer<LightData>> SpotLightBuffer = nullptr;
    std::shared_ptr<StructuredUploadBuffer<LightData>> SecondSpotLightBuffer = nullptr;
    UINT PointLightCapacity = 0;
    UINT SpotLightCapacity = 0;

    UINT64 FenceValue = 0;
    UINT64 SecondProbeFenceValue = 0;
};
