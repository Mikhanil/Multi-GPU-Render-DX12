#include "pch.h"

#include <array>

#include "SharedSSAO.h"
#include <DirectXPackedVector.h>


#include "GCommandList.h"
#include "GDevice.h"
#include "GResourceStateTracker.h"
#include "MathHelper.h"
#include "ShaderBuffersData.h"

using namespace Microsoft::WRL;

void SSAOResources::InitializeRS()
{
    aoRootSignature = std::make_shared<GRootSignature>();

    CD3DX12_DESCRIPTOR_RANGE texTable0;
    texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

    CD3DX12_DESCRIPTOR_RANGE texTable1;
    texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

    aoRootSignature->AddConstantBufferParameter(0);
    aoRootSignature->AddConstantParameter(1, 1);
    aoRootSignature->AddDescriptorParameter(&texTable0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    aoRootSignature->AddDescriptorParameter(&texTable1, 1, D3D12_SHADER_VISIBILITY_PIXEL);

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
        aoRootSignature->AddStaticSampler(sampler);
    }
    aoRootSignature->Initialize(device);
}

void SSAOResources::Initialize(const std::shared_ptr<GDevice>& Device, const D3D12_INPUT_LAYOUT_DESC& layout)
{
    this->device = Device;
    normalMapSRV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    normalMapRTV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
    depthMapSRV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    depthMapDSV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);
    randomVectorMapSRV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    ambientMapSRV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
    ambientMapRTV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2);

    InitializeRS();

    BuildOffsetVectors();
    BuildPSO(layout);
}

void SSAOResources::OnResize(uint32_t width, uint32_t height)
{
    D3D12_RESOURCE_DESC texDesc;
    ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (normalMap.GetD3D12Resource() == nullptr)
    {
        texDesc.Format = NormalMapFormat;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        float normalClearColor[] = {0.0f, 0.0f, 1.0f, 0.0f};
        CD3DX12_CLEAR_VALUE optClear(NormalMapFormat, normalClearColor);

        normalMap = GTexture(device, texDesc, L"SSAO NormalMap " + device->GetName(), TextureUsage::Normalmap, &optClear);
    }
    else
    {
        GTexture::Resize(normalMap, width, height, 1);
    }

    if (depthMap.GetD3D12Resource() == nullptr)
    {
        texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE optClear;
        optClear.Format = DXGI_FORMAT_D32_FLOAT;
        optClear.DepthStencil.Depth = 1.0f;

        depthMap = GTexture(device, texDesc, L"SSAO Depth Normal Map " + device->GetName(), TextureUsage::Depth, &optClear);
    }
    else
    {
        GTexture::Resize(depthMap, width, height, 1);
    }

    if (ambientMap0.GetD3D12Resource() == nullptr)
    {
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        texDesc.Format = AmbientMapFormat;

        float ambientClearColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
        auto optClear = CD3DX12_CLEAR_VALUE(AmbientMapFormat, ambientClearColor);

        ambientMap0 = GTexture(device, texDesc, L"SSAO AmbientMap0", TextureUsage::Normalmap, &optClear);
        ambientMap1 = GTexture(device, texDesc, L"SSAO AmbientMap1", TextureUsage::Normalmap, &optClear);
    }
    else
    {
        GTexture::Resize(ambientMap0, width, height, 1);
        GTexture::Resize(ambientMap1, width, height, 1);
    }

    RebuildDescriptors();
}

void SSAOResources::RebuildDescriptors() const
{
    // Prime GPU
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.Texture2D.MipSlice = 0;
    depthMap.CreateDepthStencilView(&dsvDesc, &depthMapDSV);

    
    
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = NormalMapFormat;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    normalMap.CreateShaderResourceView(&srvDesc, &normalMapSRV);

    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthMap.CreateShaderResourceView(&srvDesc, &depthMapSRV);

    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    randomVectorMap.CreateShaderResourceView(&srvDesc, &randomVectorMapSRV);

    srvDesc.Format = AmbientMapFormat;
    ambientMap0.CreateShaderResourceView(&srvDesc, &ambientMapSRV);
    ambientMap1.CreateShaderResourceView(&srvDesc, &ambientMapSRV, 1);

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Format = NormalMapFormat;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
    normalMap.CreateRenderTargetView(&rtvDesc, &normalMapRTV);

    rtvDesc.Format = AmbientMapFormat;
    ambientMap0.CreateRenderTargetView(&rtvDesc, &ambientMapRTV);
    ambientMap1.CreateRenderTargetView(&rtvDesc, &ambientMapRTV, 1);
}

void SSAOResources::BuildPSO(const D3D12_INPUT_LAYOUT_DESC& layout)
{
    RenderModeFactory::LoadDefaultShaders();

    D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;

    ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    basePsoDesc.InputLayout = layout;
    basePsoDesc.pRootSignature = aoRootSignature->GetNativeSignature().Get();
    basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    basePsoDesc.SampleMask = UINT_MAX;
    basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    basePsoDesc.NumRenderTargets = 1;
    basePsoDesc.RTVFormats[0] = GetSRGBFormat(BackBufferFormat);
    basePsoDesc.SampleDesc.Count = 1;
    basePsoDesc.SampleDesc.Quality = 0;
    basePsoDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;

    ssaoPSO = std::make_shared<GraphicPSO>(RenderMode::Ssao);
    ssaoPSO->SetPsoDesc(basePsoDesc);
    ssaoPSO->SetRootSignature(*aoRootSignature.get());
    ssaoPSO->SetInputLayout({nullptr, 0});
    ssaoPSO->SetShader(RenderModeFactory::GetShader("ssaoVS").get());
    ssaoPSO->SetShader(RenderModeFactory::GetShader("ssaoPS").get());
    ssaoPSO->SetRTVFormat(0, AmbientMapFormat);
    ssaoPSO->SetSampleCount(1);
    ssaoPSO->SetSampleQuality(0);
    ssaoPSO->SetDSVFormat(DXGI_FORMAT_UNKNOWN);
    auto depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    depthStencilDesc.DepthEnable = false;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    ssaoPSO->SetDepthStencilState(depthStencilDesc);
    ssaoPSO->Initialize(device);


    blurPSO = std::make_shared<GraphicPSO>(RenderMode::SsaoBlur);
    blurPSO->SetPsoDesc(ssaoPSO->GetPsoDescription());
    blurPSO->SetShader(RenderModeFactory::GetShader("ssaoBlurVS").get());
    blurPSO->SetShader(RenderModeFactory::GetShader("ssaoBlurPS").get());
    blurPSO->Initialize(device);
}

void SSAOResources::BuildOffsetVectors()
{
    // Start with 14 uniformly distributed vectors.  We choose the 8 corners of the cube
    // and the 6 center points along each cube face.  We always alternate the points on 
    // opposites sides of the cubes.  This way we still get the vectors spread out even
    // if we choose to use less than 14 samples.

    // 8 cube corners
    offsetsVectors[0] = Vector4(+1.0f, +1.0f, +1.0f, 0.0f);
    offsetsVectors[1] = Vector4(-1.0f, -1.0f, -1.0f, 0.0f);

    offsetsVectors[2] = Vector4(-1.0f, +1.0f, +1.0f, 0.0f);
    offsetsVectors[3] = Vector4(+1.0f, -1.0f, -1.0f, 0.0f);

    offsetsVectors[4] = Vector4(+1.0f, +1.0f, -1.0f, 0.0f);
    offsetsVectors[5] = Vector4(-1.0f, -1.0f, +1.0f, 0.0f);

    offsetsVectors[6] = Vector4(-1.0f, +1.0f, -1.0f, 0.0f);
    offsetsVectors[7] = Vector4(+1.0f, -1.0f, +1.0f, 0.0f);

    // 6 centers of cube faces
    offsetsVectors[8] = Vector4(-1.0f, 0.0f, 0.0f, 0.0f);
    offsetsVectors[9] = Vector4(+1.0f, 0.0f, 0.0f, 0.0f);

    offsetsVectors[10] = Vector4(0.0f, -1.0f, 0.0f, 0.0f);
    offsetsVectors[11] = Vector4(0.0f, +1.0f, 0.0f, 0.0f);

    offsetsVectors[12] = Vector4(0.0f, 0.0f, -1.0f, 0.0f);
    offsetsVectors[13] = Vector4(0.0f, 0.0f, +1.0f, 0.0f);

    for (auto& mOffset : offsetsVectors)
    {
        // Create random lengths in [0.25, 1.0].
        float s = MathHelper::RandF(0.25f, 1.0f);
        mOffset.Normalize();
        mOffset = s * mOffset;
    }

    BuildRandomTexture();
}

void SSAOResources::BuildRandomTexture()
{
    D3D12_RESOURCE_DESC texDesc;
    ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = 256;
    texDesc.Height = 256;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    randomVectorMap = GTexture(device, texDesc, L"SSAO Random Vector Map", TextureUsage::Normalmap);

    auto queue = device->GetCommandQueue();
    auto cmdList = queue->GetCommandList();

    std::vector<Vector4> data;
    data.resize(256 * 256);

    for (int i = 0; i < 256; ++i)
    {
        for (int j = 0; j < 256; ++j)
        {
            // Random vector in [0,1].  We will decompress in shader to [-1,1].
            Vector3 v(MathHelper::RandF(), MathHelper::RandF(), MathHelper::RandF());
            v.Normalize();
            data[i * 256 + j] = Vector4(v.x, v.y, v.z, 0.0f);
        }
    }

    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData = data.data();
    subResourceData.RowPitch = 256 * sizeof(Vector4);
    subResourceData.SlicePitch = subResourceData.RowPitch * 256;

    cmdList->TransitionBarrier(randomVectorMap.GetD3D12Resource(), D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->FlushResourceBarriers();

    cmdList->UpdateSubresource(randomVectorMap, &subResourceData, 1);

    cmdList->TransitionBarrier(randomVectorMap.GetD3D12Resource(), D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->FlushResourceBarriers();

    queue->ExecuteCommandList(cmdList);
    device->Flush();
}

void SSAOCrossResources::Initialize(const SSAOResources& Resources, const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice)
{
    sharedNormalMap = std::make_shared<GCrossAdapterResource>(Resources.GetNormalMap().GetD3D12ResourceDesc(), primeDevice, secondDevice);
    sharedDepthMap = std::make_shared<GCrossAdapterResource>(Resources.GetDepthMap().GetD3D12ResourceDesc(), primeDevice, secondDevice,
                                                             L"Cross Adapter Depth Map");
    sharedAmbientMap = std::make_shared<GCrossAdapterResource>(Resources.GetAmbientMap().GetD3D12ResourceDesc(), primeDevice, secondDevice,
                                                               L"Cross Adapter Ambient Map");
}

void SSAOCrossResources::OnResize(uint32_t width, uint32_t height) const
{
    sharedNormalMap->Resize(width, height);
    sharedDepthMap->Resize(width, height);
    sharedAmbientMap->Resize(width, height);
}

SharedSSAO::SharedSSAO()
{
}

SharedSSAO::~SharedSSAO() = default;


UINT SharedSSAO::SsaoMapWidth() const
{
    return RenderTargetWidth / 2;
}

UINT SharedSSAO::SsaoMapHeight() const
{
    return RenderTargetHeight / 2;
}

std::vector<float> SharedSSAO::CalcGaussWeights(const float sigma)
{
    float twoSigma2 = 2.0f * sigma * sigma;

    // Estimate the blur radius based on sigma since sigma controls the "width" of the bell curve.
    // For example, for sigma = 3, the width of the bell curve is 
    int blurRadius = static_cast<int>(ceil(2.0f * sigma));

    assert(blurRadius <= MaxBlurRadius);

    std::vector<float> weights;
    weights.resize(2 * blurRadius + 1);

    float weightSum = 0.0f;

    for (int i = -blurRadius; i <= blurRadius; ++i)
    {
        float x = static_cast<float>(i);

        weights[i + blurRadius] = expf(-x * x / twoSigma2);

        weightSum += weights[i + blurRadius];
    }

    // Divide by the sum so all the weights add up to 1.0.
    for (int i = 0; i < weights.size(); ++i)
    {
        weights[i] /= weightSum;
    }

    return weights;
}


void SharedSSAO::Initialize(const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice, const D3D12_INPUT_LAYOUT_DESC& layout, UINT width, UINT height)
{
    primeResources.Initialize(primeDevice, layout);
    primeResources.OnResize(width, height);
    
    secondResources.Initialize(secondDevice, layout);
    secondResources.OnResize(width, height);
    
    crossResources.Initialize(primeResources, primeDevice, secondDevice);
    crossResources.OnResize(width, height);
    OnResize(width, height);
}

void SharedSSAO::OnResize(const UINT newWidth, const UINT newHeight)
{
    if (RenderTargetWidth == newWidth && RenderTargetHeight == newHeight)
        return;
    RenderTargetWidth = newWidth;
    RenderTargetHeight = newHeight;

    mViewport.TopLeftX = 0.0f;
    mViewport.TopLeftY = 0.0f;
    mViewport.Width = RenderTargetWidth;
    mViewport.Height = RenderTargetHeight;
    mViewport.MinDepth = 0.0f;
    mViewport.MaxDepth = 1.0f;

    mScissorRect = {0, 0, static_cast<int>(RenderTargetWidth), static_cast<int>(RenderTargetHeight)};

    primeResources.OnResize(newWidth, newHeight);
    secondResources.OnResize(newWidth, newHeight);
    crossResources.OnResize(newWidth, newHeight);
}

void SharedSSAO::ComputeSsao(
    const std::shared_ptr<GCommandList>& cmdList,
    const std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>& currFrame,
    const SSAOResources& Resources,
    const int blurCount)
{
    cmdList->StartMark(L"SSAO");
    cmdList->SetViewports(&mViewport, 1);
    cmdList->SetScissorRects(&mScissorRect, 1);

    cmdList->TransitionBarrier(Resources.GetAmbientMap(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();


    float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
    cmdList->ClearRenderTarget(Resources.GetAmbientMapRTV(), 0, clearValue);

    cmdList->SetRenderTargets(1, Resources.GetAmbientMapRTV(), 0);

    cmdList->SetRootSignature(Resources.GetRootSignature());
    cmdList->SetPipelineState(Resources.GetSSAOPSO());

    cmdList->SetDescriptorsHeap(Resources.GetAmbientMapSRV());

    cmdList->SetRootConstantBufferView(0, *currFrame.get());
    cmdList->SetRoot32BitConstant(1, 0, 0);

    cmdList->SetRootDescriptorTable(2, Resources.GetNormalMapSRV());

    cmdList->SetRootDescriptorTable(3, Resources.GetRandomVectorSRV());


    cmdList->SetVBuffer(0, 0, nullptr);
    cmdList->SetIBuffer(nullptr);
    cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->Draw(6, 1, 0, 0);


    cmdList->TransitionBarrier(Resources.GetAmbientMap(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();

    BlurAmbientMap(cmdList, Resources, currFrame, blurCount);
    cmdList->EndMark();
}

void SharedSSAO::ClearAmbientMap(const std::shared_ptr<GCommandList>& cmdList, const SSAOResources& Resources)
{
    cmdList->TransitionBarrier(Resources.GetAmbientMap(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();

    float clearValue[] = {0.0f, 0.0f, 0.0f, 1.0f};
    cmdList->ClearRenderTarget(Resources.GetAmbientMapRTV(), 0, clearValue);


    cmdList->TransitionBarrier(Resources.GetAmbientMap(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}

void SharedSSAO::BlurAmbientMap(const std::shared_ptr<GCommandList>& cmdList, const SSAOResources& Resources,
                                const std::shared_ptr<ConstantUploadBuffer<SsaoConstants>>& currFrame, const int blurCount)
{
    cmdList->SetPipelineState(Resources.GetBlurPSO());

    cmdList->SetRootConstantBufferView(0, *currFrame.get());

    for (int i = 0; i < blurCount; ++i)
    {
        BlurAmbientMap(cmdList, Resources, true);
        BlurAmbientMap(cmdList, Resources, false);
    }
}

void SharedSSAO::BlurAmbientMap(const std::shared_ptr<GCommandList>& cmdList, const SSAOResources& Resources, const bool horzBlur)
{
    GTexture output;
    size_t inputSrv;
    size_t outputRtv;

    if (horzBlur == true)
    {
        output = Resources.GetBluredAmbientMap();
        inputSrv = 0;
        outputRtv = 1;
        cmdList->SetRoot32BitConstant(1, 1, 0);
    }
    else
    {
        output = Resources.GetAmbientMap();
        inputSrv = 1;
        outputRtv = 0;
        cmdList->SetRoot32BitConstant(1, 0, 0);
    }

    cmdList->TransitionBarrier(output, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->FlushResourceBarriers();

    float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
    cmdList->ClearRenderTarget(Resources.GetAmbientMapRTV(), outputRtv, clearValue);

    cmdList->SetRenderTargets(1, Resources.GetAmbientMapRTV(), outputRtv);

    cmdList->SetRootDescriptorTable(2, Resources.GetNormalMapSRV());

    cmdList->SetRootDescriptorTable(3, Resources.GetAmbientMapSRV(), inputSrv);

    // Draw fullscreen quad.
    cmdList->SetVBuffer(0, 0, nullptr);
    cmdList->SetIBuffer(nullptr);
    cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->Draw(6, 1, 0, 0);

    cmdList->TransitionBarrier(output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
}