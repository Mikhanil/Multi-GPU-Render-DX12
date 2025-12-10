#pragma once

#include "d3dUtil.h"
#include "GCrossAdapterResource.h"
#include "GraphicPSO.h"
#include "GDescriptor.h"
#include "GRenderTarger.h"
#include "GTexture.h"
#include "MathHelper.h"
#include "RenderModeFactory.h"
#include "ShaderBuffersData.h"

using namespace DirectX::SimpleMath;

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

class SSAOCrossResources;
class SharedSSAO;

static constexpr int MaxBlurRadius = 5;

class SSAOResources
{
protected:
    static constexpr DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;
    static constexpr DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT DepthMapFormat = DXGI_FORMAT_R32_TYPELESS;


    std::shared_ptr<GDevice> device;
    std::shared_ptr<GRootSignature> aoRootSignature;

    std::shared_ptr<GraphicPSO> ssaoPSO;
    std::shared_ptr<GraphicPSO> blurPSO;
    GTexture randomVectorMap;
    GDescriptor randomVectorMapSRV;

    GTexture normalMap;
    GDescriptor normalMapSRV;
    GDescriptor normalMapRTV;

    GTexture ambientMap0;
    GTexture ambientMap1;
    GDescriptor ambientMapSRV;
    GDescriptor ambientMapRTV;

    GTexture depthMap;
    GDescriptor depthMapSRV;
    GDescriptor depthMapDSV;

    Vector4 offsetsVectors[14];

public:
    virtual ~SSAOResources() = default;
    const GTexture& GetRandomVectorMap() const { return randomVectorMap; }
    const GTexture& GetNormalMap() const { return normalMap; }
    const GTexture& GetAmbientMap() const { return ambientMap0; }
    const GTexture& GetBluredAmbientMap() const { return ambientMap1; }
    const GTexture& GetDepthMap() const { return depthMap; }

    const GDescriptor* GetNormalMapSRV() const { return &normalMapSRV; }
    const GDescriptor* GetNormalMapRTV() const { return &normalMapRTV; }
    const GDescriptor* GetAmbientMapSRV() const { return &ambientMapSRV; }
    const GDescriptor* GetAmbientMapRTV() const { return &ambientMapRTV; }
    const GDescriptor* GetDepthMapSRV() const { return &depthMapSRV; }
    const GDescriptor* GetDepthMapDSV() const { return &depthMapDSV; }
    const GDescriptor* GetRandomVectorSRV() const { return &randomVectorMapSRV; }

    const GRootSignature& GetRootSignature() const  { return *aoRootSignature;}
    const GraphicPSO& GetSSAOPSO() const { return *ssaoPSO;}
    const GraphicPSO& GetBlurPSO() const { return *blurPSO;}

    void virtual InitializeRS();
    void virtual Initialize(const std::shared_ptr<GDevice>& Device, const D3D12_INPUT_LAYOUT_DESC& layout);

    void virtual OnResize(uint32_t width, uint32_t height);

    void GetOffsetVectors(Vector4 offsets[14]) const
    {
        std::copy(&offsetsVectors[0], &offsetsVectors[14], &offsets[0]);
    }

protected:
    void virtual RebuildDescriptors() const;

    void virtual BuildPSO(const D3D12_INPUT_LAYOUT_DESC& layout);

    void virtual BuildOffsetVectors();

    void virtual BuildRandomTexture();
};

class SSAOCrossResources final
{
    std::shared_ptr<GCrossAdapterResource> sharedNormalMap;
    std::shared_ptr<GCrossAdapterResource> sharedDepthMap;
    std::shared_ptr<GCrossAdapterResource> sharedAmbientMap;

public:
    void Initialize(const SSAOResources& Resources, const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice);

    void OnResize(uint32_t width, uint32_t height) const;

    const GCrossAdapterResource& GetNormalMap() const { return *sharedNormalMap; }
    const GCrossAdapterResource& GetDepthMap() const { return *sharedDepthMap; }
    const GCrossAdapterResource& GetAmbientMap() const { return *sharedAmbientMap; }
};


class SharedSSAO final
{
public:
    SharedSSAO();
    SharedSSAO(const SharedSSAO& rhs) = delete;
    SharedSSAO& operator=(const SharedSSAO& rhs) = delete;
    ~SharedSSAO();

    UINT SsaoMapWidth() const;
    UINT SsaoMapHeight() const;

    void GetOffsetVectors(Vector4 offsets[]);
    static std::vector<float> CalcGaussWeights(float sigma);

    const SSAOResources& GetPrimeResources() { return primeResources; }
    const SSAOResources& GetSecondResource() { return secondResources; }
    const SSAOCrossResources& GetCrossResources() { return crossResources; }
    
    void Initialize(const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice,
                    const D3D12_INPUT_LAYOUT_DESC& layout, UINT width, UINT height);

    void OnResize(UINT newWidth, UINT newHeight);


    void ComputeSsao(
        const std::shared_ptr<GCommandList>& cmdList,
        const std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>& currFrame,
        const SSAOResources& Resources, int blurCount);
    static void ClearAmbientMap(const std::shared_ptr<GCommandList>& cmdList, const SSAOResources& Resources);
    static void BlurAmbientMap(const std::shared_ptr<GCommandList>& cmdList, const SSAOResources& Resources, const std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>& currFrame, int blurCount);
    static void BlurAmbientMap(const std::shared_ptr<GCommandList>& cmdList, const SSAOResources& Resources, bool horzBlur);

private:
    SSAOResources primeResources;
    SSAOResources secondResources;
    SSAOCrossResources crossResources;

    UINT RenderTargetWidth;
    UINT RenderTargetHeight;

    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissorRect;
};
