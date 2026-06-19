#pragma once
#include "d3dUtil.h"
#include "DirectXBuffers.h"
#include "GCrossAdapterResource.h"
#include "GraphicPSO.h"
#include "GDescriptor.h"
#include "GRenderTarger.h"
#include "GTexture.h"
#include "MathHelper.h"
#include "RenderModeFactory.h"
#include "ShaderBuffersData.h"
#include "SharedSSAO.h"
#include "Shaders/XeGTAO.h"


using namespace DirectX::SimpleMath;

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

using GTAOConstants = XeGTAO::GTAOConstants;
using GTAOSettings = XeGTAO::GTAOSettings;

struct XeGTAOCompositeConstants
{
    Vector4 GTAOResolutionScale;
    float Intensity;
    float _padding[3];
};

class XeGTAOResources final : public SSAOResources
{
public:
    struct Pass
    {
        std::shared_ptr<GShader> Shader;
        std::shared_ptr<GRootSignature> RootSignature;
        std::shared_ptr<ComputePSO> PSO;
        int srvCount = 0;
        int uavCount = 0;
        int extraCbvCount = 0;
    };
    std::shared_ptr<GDevice> device;
    GDescriptor ambientMapUAV;
    ComputePSO pso;

    void RebuildDescriptors() const override;
    std::shared_ptr<GRootSignature> CreateXeGTAORootSignature(int srvCount,
                                                                int uavCount,
                                                                int extraCbvCount);
    void BuildPSO();
    void BuildPass(Pass& pass,
                   const std::wstring& fileName,
                   const std::string& entryPoint,
                   int srvCount,
                   int uavCount,
                   int cbvCount);
    void ApplyPass(GCommandList& cmdList, Pass& pass) const;

    void OnResize(uint32_t width, uint32_t height) override;

    Pass Prefilter;
    Pass Main;
    Pass Denoise;
    Pass DenoiseLast;
    Pass Composite;

    const GDescriptor* GetAmbientMapUAV() const { return &ambientMapUAV; }
    const ComputePSO& GetPso() const { return pso; }

    void Initialize(const std::shared_ptr<GDevice>& Device, const D3D12_INPUT_LAYOUT_DESC& layout);

    const GTexture& GetXeWorkingDepth() const { return xeWorkingDepth; }
    const GTexture& GetXeAoPing() const { return xeAoPing; }
    const GTexture& GetXeAoPong() const { return xeAoPong; }
    const GTexture& GetXeEdges() const { return xeEdges; }
    const GDescriptor* GetXeDescriptorBase() const { return &xeDescriptorBase; }

private:
    void EnsureWorkingTextures(uint32_t width, uint32_t height);
    void RebuildXeGTAOInternalDescriptors() const;

    GTexture xeWorkingDepth;
    // Ping-pong: two R32_UINT buffers for denoise passes (read one, write the other, then swap roles).
    GTexture xeAoPing;
    GTexture xeAoPong;
    GTexture xeEdges;

    GDescriptor xeDescriptorBase{};
    // Count of consecutive CBV_SRV_UAV descriptors: 0–4 depth mip UAVs, 5–6 SRVs (depth pyramid + normals), 7–13 AO/edges SRV/UAV.
    static constexpr UINT XeDescriptorCount = 14;
};

class SharedXeGTAO
{
    XeGTAOResources primeResources;
    XeGTAOResources secondResources;
    SSAOCrossResources crossResources;

    GTAOSettings gtaoSettings;
    UINT RenderTargetWidth = 0;
    UINT RenderTargetHeight = 0;

    std::shared_ptr<ConstantUploadBuffer<XeGTAOCompositeConstants>> primeCompositeCB;
    std::shared_ptr<ConstantUploadBuffer<XeGTAOCompositeConstants>> secondCompositeCB;

public:
    const XeGTAOResources& GetPrimeResources() const { return primeResources; }
    const XeGTAOResources& GetSecondResources() const { return secondResources; }
    const SSAOCrossResources& GetCrossResources() const { return crossResources; }
    const GTAOSettings& GetSettings() const { return gtaoSettings; }
    GTAOSettings& GetSettings() { return gtaoSettings; }

    void Initialize(const std::shared_ptr<GDevice>& PrimeDevice, const std::shared_ptr<GDevice>& SecondDevice,
                    const D3D12_INPUT_LAYOUT_DESC& layout, UINT width, UINT height);
    void OnResize(UINT width, UINT height);
    void UpdateCompositeConstants();
    void Compute(const std::shared_ptr<GCommandList>& cmdList,
                 const std::shared_ptr<ConstantUploadBuffer<GTAOConstants>>& Constants,
                 const XeGTAOResources& Resources);
};
