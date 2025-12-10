#pragma once

#include "d3dUtil.h"
#include "GraphicPSO.h"
#include "GDescriptor.h"
#include "GTexture.h"
#include "ShaderBuffersData.h"

using namespace DirectX::SimpleMath;

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;


class SSAO
{
public:
    SSAO(const std::shared_ptr<GDevice>& device,
         const std::shared_ptr<GCommandList>& cmdList,
         UINT width, UINT height);
    SSAO(const SSAO& rhs) = delete;
    SSAO& operator=(const SSAO& rhs) = delete;
    ~SSAO();

    static constexpr DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;
    static constexpr DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    static constexpr DXGI_FORMAT DepthMapFormat = DXGI_FORMAT_R32_TYPELESS;

    static constexpr int MaxBlurRadius = 5;

    UINT SsaoMapWidth() const;
    UINT SsaoMapHeight() const;

    void GetOffsetVectors(Vector4 offsets[]);
    std::vector<float> CalcGaussWeights(float sigma);


    GTexture& NormalMap();
    GTexture& AmbientMap();
    GTexture& NormalDepthMap();

    GDescriptor* NormalMapDSV();
    GDescriptor* NormalMapRtv();
    GDescriptor* NormalMapSrv();
    GDescriptor* AmbientMapSrv();

    void BuildDescriptors();

    void RebuildDescriptors() const;

    void SetPipelineData(GraphicPSO& ssaoPso, GraphicPSO& ssaoBlurPso);

    void OnResize(UINT newWidth, UINT newHeight);


    void ComputeSsao(
        const std::shared_ptr<GCommandList>& cmdList,
        const std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>& currFrame,
        int blurCount);
    void ClearAmbiantMap(const std::shared_ptr<GCommandList>& cmdList) const;

private:
    std::shared_ptr<GRootSignature> ssaoPrimeRootSignature;
    std::shared_ptr<GDevice> device;

    void BlurAmbientMap(const std::shared_ptr<GCommandList>& cmdList,
                        const std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>& currFrame, int blurCount);
    void BlurAmbientMap(const std::shared_ptr<GCommandList>& cmdList, bool horzBlur) const;
    GTexture CreateNormalMap() const;
    GTexture CreateAmbientMap() const;
    GTexture CreateDepthMap() const;

    void BuildResources();
    void BuildRandomVectorTexture(const std::shared_ptr<GCommandList>& cmdList);

    void BuildOffsetVectors();


    GraphicPSO mSsaoPso;
    GraphicPSO mBlurPso;

    GTexture randomVectorMap;
    GTexture normalMap;
    GTexture ambientMap0;
    GTexture ambientMap1;
    GTexture depthMap;


    GDescriptor normalMapSrvMemory;
    GDescriptor normalMapRtvMemory;

    GDescriptor depthMapSrvMemory;
    GDescriptor depthMapDSVMemory;

    GDescriptor randomVectorSrvMemory;

    GDescriptor ambientMapMapSrvMemory;
    GDescriptor ambientMapRtvMemory;


    UINT mRenderTargetWidth;
    UINT mRenderTargetHeight;

    Vector4 mOffsets[14];

    D3D12_VIEWPORT mViewport;
    D3D12_RECT mScissorRect;
};
