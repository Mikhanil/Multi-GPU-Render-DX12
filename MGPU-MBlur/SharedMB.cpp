#include "pch.h"

#include <array>

#include "SharedMB.h"
#include <DirectXPackedVector.h>


#include "GCommandList.h"
#include "GDevice.h"
#include "GResourceStateTracker.h"
#include "MathHelper.h"
#include "ShaderBuffersData.h"

using namespace Microsoft::WRL;


const float SharedMB::maxVelocity = 30.0f;

void MBResources::InitializeRS()
{
    InitializeVelocityRS();
    InitializeTilemaxRS();
    InitializeNeighbourmaxRS();
    InitializeMbRS();
}

void MBResources::InitializeVelocityRS()
{
    velocityRootSignature = std::make_shared<GRootSignature>();

    CD3DX12_DESCRIPTOR_RANGE texTableDepthSrv;
    texTableDepthSrv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0); // t0

    CD3DX12_DESCRIPTOR_RANGE texTableVelocityUav;
    texTableVelocityUav.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0); // u0

    velocityRootSignature->AddConstantBufferParameter(0); // b0, буфер с матрицами

    velocityRootSignature->AddDescriptorParameter(&texTableDepthSrv, 1, D3D12_SHADER_VISIBILITY_ALL); // t0, srv на чтение depth
    velocityRootSignature->AddDescriptorParameter(&texTableVelocityUav, 1, D3D12_SHADER_VISIBILITY_ALL); // u0, uav на запись в velocity

    velocityRootSignature->Initialize(device);
}

void MBResources::InitializeTilemaxRS()
{
    tilemaxRootSignature = std::make_shared<GRootSignature>();

    CD3DX12_DESCRIPTOR_RANGE texTableVelocitySrv;
    texTableVelocitySrv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0); // t1

    CD3DX12_DESCRIPTOR_RANGE texTableTilemaxUav;
    texTableTilemaxUav.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0); // u1

    tilemaxRootSignature->AddConstantBufferParameter(0); // b0, буфер с матрицами
    
    tilemaxRootSignature->AddDescriptorParameter(&texTableVelocitySrv, 1, D3D12_SHADER_VISIBILITY_ALL); // t1, srv на чтение velocity
    tilemaxRootSignature->AddDescriptorParameter(&texTableTilemaxUav, 1, D3D12_SHADER_VISIBILITY_ALL); // u1, uav на запись в tilemax

    tilemaxRootSignature->Initialize(device);
}

void MBResources::InitializeNeighbourmaxRS()
{
    neighbourmaxRootSignature = std::make_shared<GRootSignature>();

    CD3DX12_DESCRIPTOR_RANGE texTableTilemaxSrv;
    texTableTilemaxSrv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0); // t2

    CD3DX12_DESCRIPTOR_RANGE texTableNeighbourmaxUav;
    texTableNeighbourmaxUav.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0); // u2

    neighbourmaxRootSignature->AddConstantBufferParameter(0); // b0, буфер с матрицами
    
    neighbourmaxRootSignature->AddDescriptorParameter(&texTableTilemaxSrv, 1, D3D12_SHADER_VISIBILITY_ALL); // t2, srv на чтение tilemax
    neighbourmaxRootSignature->AddDescriptorParameter(&texTableNeighbourmaxUav, 1, D3D12_SHADER_VISIBILITY_ALL); // u2, uav на запись в neigbourmax
    
    neighbourmaxRootSignature->Initialize(device);
}

void MBResources::InitializeMbRS()
{
    mbRootSignature = std::make_shared<GRootSignature>();

    CD3DX12_DESCRIPTOR_RANGE texTableDepthSrv;
    texTableDepthSrv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0); // t0

    //CD3DX12_DESCRIPTOR_RANGE texTableVelocitySrv;
    //texTableVelocitySrv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0); // t1

    CD3DX12_DESCRIPTOR_RANGE texTableNeighbourmaxSrv;
    texTableNeighbourmaxSrv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0); // t3

    CD3DX12_DESCRIPTOR_RANGE texTableMbUav;
    texTableMbUav.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3, 0); // u3

    CD3DX12_DESCRIPTOR_RANGE texTableFrameSrv;
    texTableFrameSrv.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0); // t4

    mbRootSignature->AddConstantBufferParameter(0); // b0, буфер с матрицами

    mbRootSignature->AddDescriptorParameter(&texTableDepthSrv, 1, D3D12_SHADER_VISIBILITY_ALL); // t0, srv на чтение depth
    //mbRootSignature->AddDescriptorParameter(&texTableVelocitySrv, 1, D3D12_SHADER_VISIBILITY_ALL); // t1, srv на чтение velocity
    mbRootSignature->AddDescriptorParameter(&texTableNeighbourmaxSrv, 1, D3D12_SHADER_VISIBILITY_ALL); // t3, srv на чтение neighbourmax
    mbRootSignature->AddDescriptorParameter(&texTableFrameSrv, 1, D3D12_SHADER_VISIBILITY_ALL); // t4, srv на чтение frame
    mbRootSignature->AddDescriptorParameter(&texTableMbUav, 1, D3D12_SHADER_VISIBILITY_ALL); // u3, uav на запись в mb map

    /*const CD3DX12_STATIC_SAMPLER_DESC linearSampler(
        0, // s0
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP
    );*/
    const CD3DX12_STATIC_SAMPLER_DESC linearSampler(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        0.0f,
        16,
        D3D12_COMPARISON_FUNC_ALWAYS,
        D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK
    );
    const CD3DX12_STATIC_SAMPLER_DESC pointSampler(
        1, // s1
        D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP
    );

    std::array<CD3DX12_STATIC_SAMPLER_DESC, 2> staticSamplers =
    {
        linearSampler, pointSampler
    };

    for (auto&& sampler : staticSamplers)
    {
        mbRootSignature->AddStaticSampler(sampler);
    }

    mbRootSignature->Initialize(device);
}

void MBResources::Initialize(const std::shared_ptr<GDevice>& Device, const D3D12_INPUT_LAYOUT_DESC& layout)
{
    this->device = Device;
 
    depthMapSRV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    velocityMapSRV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    velocityMapUAV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    tilemaxMapSRV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    tilemaxMapUAV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    neighbourmaxMapSRV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    neighbourmaxMapUAV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    mbMapSRV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    mbMapUAV = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

    InitializeRS();
   
    BuildPSO(layout);
}

void MBResources::OnResize(uint32_t width, uint32_t height)
{
    //depth

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

    if (depthMap.GetD3D12Resource() == nullptr)
    {
        texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

        D3D12_CLEAR_VALUE optClear;
        optClear.Format = DXGI_FORMAT_D32_FLOAT;
        optClear.DepthStencil.Depth = 1.0f;

        depthMap = GTexture(device, texDesc, L"MB Depth Map " + device->GetName(), TextureUsage::Depth, &optClear);
    }
    else
    {
        GTexture::Resize(depthMap, width, height, 1);
    }

    // velocity

    D3D12_RESOURCE_DESC texVelocityDesc;
    ZeroMemory(&texVelocityDesc, sizeof(D3D12_RESOURCE_DESC));
    texVelocityDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texVelocityDesc.Alignment = 0;
    texVelocityDesc.Width = width;
    texVelocityDesc.Height = height;
    texVelocityDesc.DepthOrArraySize = 1;
    texVelocityDesc.MipLevels = 1;
    texVelocityDesc.SampleDesc.Count = 1; 
    texVelocityDesc.SampleDesc.Quality = 0;
    texVelocityDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (velocityMap.GetD3D12Resource() == nullptr)
    {
        texVelocityDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        texVelocityDesc.Format = VelocityMapFormat;

        velocityMap = GTexture(device, texVelocityDesc, L"MB velocityMap", TextureUsage::MotionBlur);
    }
    else
    {
        GTexture::Resize(velocityMap, width, height, 1);
    }

    // tilemax

    UINT tileW = (width + SharedMB::tileSize - 1) / SharedMB::tileSize;
    UINT tileH = (height + SharedMB::tileSize - 1) / SharedMB::tileSize;

    D3D12_RESOURCE_DESC texTilemaxDesc;
    ZeroMemory(&texTilemaxDesc, sizeof(D3D12_RESOURCE_DESC));
    texTilemaxDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texTilemaxDesc.Alignment = 0;
    texTilemaxDesc.Width = tileW;
    texTilemaxDesc.Height = tileH;
    texTilemaxDesc.DepthOrArraySize = 1;
    texTilemaxDesc.MipLevels = 1;
    texTilemaxDesc.SampleDesc.Count = 1;
    texTilemaxDesc.SampleDesc.Quality = 0;
    texTilemaxDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (tilemaxMap.GetD3D12Resource() == nullptr)
    {
        texTilemaxDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        texTilemaxDesc.Format = TilemaxMapFormat;

        tilemaxMap = GTexture(device, texTilemaxDesc, L"MB tilemaxMap", TextureUsage::MotionBlur);
    }
    else
    {
        GTexture::Resize(tilemaxMap, tileW, tileH, 1); // TODO: тут точно нужно этот размер передавать?
    }

    // neighbourmax

    D3D12_RESOURCE_DESC texNeighbourmaxDesc;
    ZeroMemory(&texNeighbourmaxDesc, sizeof(D3D12_RESOURCE_DESC));
    texNeighbourmaxDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texNeighbourmaxDesc.Alignment = 0;
    texNeighbourmaxDesc.Width = tileW;
    texNeighbourmaxDesc.Height = tileH;
    texNeighbourmaxDesc.DepthOrArraySize = 1;
    texNeighbourmaxDesc.MipLevels = 1;
    texNeighbourmaxDesc.SampleDesc.Count = 1;
    texNeighbourmaxDesc.SampleDesc.Quality = 0;
    texNeighbourmaxDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (neighbourmaxMap.GetD3D12Resource() == nullptr)
    {
        texNeighbourmaxDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        texNeighbourmaxDesc.Format = NeighbourMaxMapFormat;

        neighbourmaxMap = GTexture(device, texNeighbourmaxDesc, L"MB neighbourmaxMap", TextureUsage::MotionBlur);
    }
    else
    {
        GTexture::Resize(neighbourmaxMap, tileW, tileH, 1); // TODO: тут точно нужно этот размер передавать?
    }

    // mb
    // TODO: нужно ли делать resize для frame??
    D3D12_RESOURCE_DESC texMbDesc;
    ZeroMemory(&texMbDesc, sizeof(D3D12_RESOURCE_DESC));
    texMbDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texMbDesc.Alignment = 0;
    texMbDesc.Width = width;
    texMbDesc.Height = height;
    texMbDesc.DepthOrArraySize = 1;
    texMbDesc.MipLevels = 1;
    texMbDesc.SampleDesc.Count = 1;
    texMbDesc.SampleDesc.Quality = 0;
    texMbDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    if (mbMap.GetD3D12Resource() == nullptr)
    {
        texMbDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        texMbDesc.Format = MbMapFormat;

        mbMap = GTexture(device, texMbDesc, L"MB motionBlurMap", TextureUsage::MotionBlur);
    }
    else
    {
        GTexture::Resize(mbMap, width, height, 1);
    }

    // common

    RebuildDescriptors();
}

void MBResources::RebuildDescriptors() const
{
    // Prime GPU

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = NormalMapFormat;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthMap.CreateShaderResourceView(&srvDesc, &depthMapSRV);

    // velocity

    D3D12_SHADER_RESOURCE_VIEW_DESC srvVelocityDesc = {};
    srvVelocityDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvVelocityDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvVelocityDesc.Texture2D.MostDetailedMip = 0;
    srvVelocityDesc.Texture2D.MipLevels = 1;
    srvVelocityDesc.Format = VelocityMapFormat;

    velocityMap.CreateShaderResourceView(&srvVelocityDesc, &velocityMapSRV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavVelocityDesc = {};
    uavVelocityDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavVelocityDesc.Format = VelocityMapFormat;

    velocityMap.CreateUnorderedAccessView(&uavVelocityDesc, &velocityMapUAV);

    // tilemax

    D3D12_SHADER_RESOURCE_VIEW_DESC srvTilemaxDesc = {};
    srvTilemaxDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvTilemaxDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvTilemaxDesc.Texture2D.MostDetailedMip = 0;
    srvTilemaxDesc.Texture2D.MipLevels = 1;
    srvTilemaxDesc.Format = TilemaxMapFormat;

    tilemaxMap.CreateShaderResourceView(&srvTilemaxDesc, &tilemaxMapSRV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavTilemaxDesc = {};
    uavTilemaxDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavTilemaxDesc.Format = TilemaxMapFormat;

    tilemaxMap.CreateUnorderedAccessView(&uavTilemaxDesc, &tilemaxMapUAV);

    // neighbourmax

    D3D12_SHADER_RESOURCE_VIEW_DESC srvNeighbourmaxDesc = {};
    srvNeighbourmaxDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvNeighbourmaxDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvNeighbourmaxDesc.Texture2D.MostDetailedMip = 0;
    srvNeighbourmaxDesc.Texture2D.MipLevels = 1;
    srvNeighbourmaxDesc.Format = NeighbourMaxMapFormat;

    neighbourmaxMap.CreateShaderResourceView(&srvNeighbourmaxDesc, &neighbourmaxMapSRV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavNeighbourmaxDesc = {};
    uavNeighbourmaxDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavNeighbourmaxDesc.Format = NeighbourMaxMapFormat;

    neighbourmaxMap.CreateUnorderedAccessView(&uavNeighbourmaxDesc, &neighbourmaxMapUAV);

    // mb

    D3D12_SHADER_RESOURCE_VIEW_DESC srvMbDesc = {};
    srvMbDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvMbDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvMbDesc.Texture2D.MostDetailedMip = 0;
    srvMbDesc.Texture2D.MipLevels = 1;
    srvMbDesc.Format = MbMapFormat;

    mbMap.CreateShaderResourceView(&srvMbDesc, &mbMapSRV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavMbDesc = {};
    uavMbDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavMbDesc.Format = MbMapFormat;

    mbMap.CreateUnorderedAccessView(&uavMbDesc, &mbMapUAV);

}

void MBResources::BuildPSO(const D3D12_INPUT_LAYOUT_DESC& layout)
{
    RenderModeFactory::LoadDefaultShaders();

  //  assert(mbRootSignature);
  //  assert(RenderModeFactory::GetShader("velocityCS").get());
  //  assert(RenderModeFactory::GetShader("velocityCS").get()->GetShaderData());

    velocityPSO = std::make_shared<ComputePSO>();
    velocityPSO->SetRootSignature(*velocityRootSignature.get());
    velocityPSO->SetShader(RenderModeFactory::GetShader("velocityCS").get());
    velocityPSO->Initialize(device);

    tilemaxPSO = std::make_shared<ComputePSO>();
    tilemaxPSO->SetRootSignature(*tilemaxRootSignature.get());
    tilemaxPSO->SetShader(RenderModeFactory::GetShader("tilemaxCS").get());
    tilemaxPSO->Initialize(device);
    
    neighbourmaxPSO = std::make_shared<ComputePSO>();
    neighbourmaxPSO->SetRootSignature(*neighbourmaxRootSignature.get());
    neighbourmaxPSO->SetShader(RenderModeFactory::GetShader("neighbourmaxCS").get());
    neighbourmaxPSO->Initialize(device);

    mbPSO = std::make_shared<ComputePSO>();
    mbPSO->SetRootSignature(*mbRootSignature.get());
    mbPSO->SetShader(RenderModeFactory::GetShader("mbCS").get());
    mbPSO->Initialize(device);
}

void MBCrossResources::Initialize(const MBResources& Resources, const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice)
{
   // sharedVelocityMap = std::make_shared<GCrossAdapterResource>(Resources.GetVelocityMap().GetD3D12ResourceDesc(), primeDevice, secondDevice);
    sharedDepthMap = std::make_shared<GCrossAdapterResource>(Resources.GetDepthMap().GetD3D12ResourceDesc(), primeDevice, secondDevice,
        L"Cross Adapter Depth Map");
    sharedNeighbourmaxMap = std::make_shared<GCrossAdapterResource>(Resources.GetNeighbourmaxMap().GetD3D12ResourceDesc(), primeDevice, secondDevice,
        L"Cross Adapter Neighbourmax Map");
}

void MBCrossResources::OnResize(uint32_t width, uint32_t height) const
{
    //sharedVelocityMap->Resize(width, height);
    sharedDepthMap->Resize(width, height);

    UINT tileW = (width + SharedMB::tileSize - 1) / SharedMB::tileSize;
    UINT tileH = (height + SharedMB::tileSize - 1) / SharedMB::tileSize;

    sharedNeighbourmaxMap->Resize(tileW, tileH);
}

SharedMB::SharedMB()
{
}

SharedMB::~SharedMB() = default;

void SharedMB::Initialize(const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondDevice, const D3D12_INPUT_LAYOUT_DESC& layout, UINT width, UINT height)
{
    primeResources.Initialize(primeDevice, layout);
    primeResources.OnResize(width, height);

    secondResources.Initialize(secondDevice, layout);
    secondResources.OnResize(width, height);

    crossResources.Initialize(primeResources, primeDevice, secondDevice);
    crossResources.OnResize(width, height);

    OnResize(width, height);
}

void SharedMB::OnResize(const UINT newWidth, const UINT newHeight)
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

    mScissorRect = { 0, 0, static_cast<int>(RenderTargetWidth), static_cast<int>(RenderTargetHeight) };

    primeResources.OnResize(newWidth, newHeight);
    secondResources.OnResize(newWidth, newHeight);
    crossResources.OnResize(newWidth, newHeight);
}


void SharedMB::ComputeMbTextures(
    const std::shared_ptr<GCommandList>& cmdList,
    const std::shared_ptr<ConstantUploadBuffer<MBConstants>>& currFrame,
    const MBResources& Resources//,
   // const int blurCount,
    //const std::shared_ptr<SharedSSAO> ssaoPass)
    )
{
    ComputeVelocityBuffer(cmdList, currFrame, Resources);//, ssaoPass);
    ComputeTileMax(cmdList, currFrame, Resources);
    ComputeNeighbourMax(cmdList, currFrame, Resources);
}

void SharedMB::ComputeVelocityBuffer(
    const std::shared_ptr<GCommandList>& cmdList,
    const std::shared_ptr<ConstantUploadBuffer<MBConstants>>& currFrame,
    const MBResources& Resources//,
    //const std::shared_ptr<SharedSSAO> ssaoPass
    )
{
    cmdList->StartMark(L"MB velocity");

    cmdList->TransitionBarrier(Resources.GetVelocityMap(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    //assert(ssaoPass->GetPrimeResources().GetDepthMapSRV());

    // depth
    //cmdList->TransitionBarrier(ssaoPass->GetPrimeResources().GetDepthMap(),
    //    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(Resources.GetDepthMap(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    //cmdList->TransitionBarrier(antiAliasingPrimePath->GetSRV(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    cmdList->FlushResourceBarriers();

    //assert(Resources.GetRootSignature());
    //cmdList->SetRootSignature(Resources.GetRootSignature());
    cmdList->SetComputeRootSignature(Resources.GetVelocityRootSignature());
    cmdList->SetPipelineState(Resources.GetVelocityPSO());
    cmdList->SetDescriptorsHeap(Resources.GetVelocityMapUAV());

    //cmdList->SetRootConstantBufferView(0, *currFrame.get());
    cmdList->SetComputeRootConstantBufferView(0, *currFrame.get());
    //cmdList->SetRootDescriptorTable(1, ssaoPass->GetPrimeResources().GetDepthMapSRV());

    assert(Resources.GetVelocityMapUAV());
    //cmdList->SetRootDescriptorTable(1, Resources.GetVelocityMapUAV());

    // depth
    //cmdList->SetComputeRootDescriptorTable(1, ssaoPass->GetPrimeResources().GetDepthMapSRV());
    cmdList->SetComputeRootDescriptorTable(1, Resources.GetDepthMapSRV());
    //cmdList->SetComputeRootDescriptorTable(1, antiAliasingPrimePath->GetSRV());

    cmdList->SetComputeRootDescriptorTable(2, Resources.GetVelocityMapUAV());

    // Dispatch
    auto BlockSizeX = tileSize; // TODO
    auto BlockSizeY = tileSize; // TODO
    UINT groupX = (RenderTargetWidth + BlockSizeX - 1) / BlockSizeX;
    UINT groupY = (RenderTargetHeight + BlockSizeY - 1) / BlockSizeY;
    cmdList->Dispatch(groupX, groupY, 1);

    cmdList->TransitionBarrier(Resources.GetVelocityMap(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    //cmdList->TransitionBarrier(Resources.GetVelocityMap(), D3D12_RESOURCE_STATE_COMMON);

    //cmdList->TransitionBarrier(ssaoPass->GetPrimeResources().GetDepthMap(),
    //    D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(Resources.GetDepthMap(),
        D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();

    // TODO: остальные этапы
    cmdList->EndMark();
}

void SharedMB::ComputeTileMax(
    const std::shared_ptr<GCommandList>& cmdList,
    const std::shared_ptr<ConstantUploadBuffer<MBConstants>>& currFrame,
    const MBResources& Resources)
{
    cmdList->StartMark(L"MB tilemax");

    cmdList->TransitionBarrier(Resources.GetVelocityMap(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(Resources.GetTilemaxMap(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    cmdList->SetComputeRootSignature(Resources.GetTilemaxRootSignature());
    cmdList->SetPipelineState(Resources.GetTilemaxPSO());
    cmdList->SetDescriptorsHeap(Resources.GetTilemaxMapUAV());

    cmdList->SetComputeRootConstantBufferView(0, *currFrame.get());
    cmdList->SetComputeRootDescriptorTable(1, Resources.GetVelocityMapSRV());
    cmdList->SetComputeRootDescriptorTable(2, Resources.GetTilemaxMapUAV());

    // Dispatch
    auto BlockSizeX = tileSize; // TODO
    auto BlockSizeY = tileSize; // TODO
    UINT groupX = (RenderTargetWidth + BlockSizeX - 1) / BlockSizeX;
    UINT groupY = (RenderTargetHeight + BlockSizeY - 1) / BlockSizeY;
    cmdList->Dispatch(groupX, groupY, 1);

    cmdList->TransitionBarrier(Resources.GetVelocityMap(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(Resources.GetTilemaxMap(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();

    cmdList->EndMark();
}

void SharedMB::ComputeNeighbourMax(
    const std::shared_ptr<GCommandList>& cmdList,
    const std::shared_ptr<ConstantUploadBuffer<MBConstants>>& currFrame,
    const MBResources& Resources)
{
    cmdList->StartMark(L"MB neighbourmax");

    cmdList->TransitionBarrier(Resources.GetTilemaxMap(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(Resources.GetNeighbourmaxMap(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    cmdList->SetComputeRootSignature(Resources.GetNeighbourmaxRootSignature());
    cmdList->SetPipelineState(Resources.GetNeighbourmaxPSO());
    cmdList->SetDescriptorsHeap(Resources.GetNeighbourmaxMapUAV());

    cmdList->SetComputeRootConstantBufferView(0, *currFrame.get());
    cmdList->SetComputeRootDescriptorTable(1, Resources.GetTilemaxMapSRV());
    cmdList->SetComputeRootDescriptorTable(2, Resources.GetNeighbourmaxMapUAV());

    // Dispatch
    auto BlockSizeX = tileSize; // TODO
    auto BlockSizeY = tileSize; // TODO
    UINT groupX = (RenderTargetWidth + BlockSizeX - 1) / BlockSizeX;
    UINT groupY = (RenderTargetHeight + BlockSizeY - 1) / BlockSizeY;
    cmdList->Dispatch(groupX, groupY, 1);

    cmdList->TransitionBarrier(Resources.GetTilemaxMap(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(Resources.GetNeighbourmaxMap(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();

    cmdList->EndMark();
}

void SharedMB::ComputeMotionBlur(
    const std::shared_ptr<GCommandList>& cmdList,
    const std::shared_ptr<ConstantUploadBuffer<MBConstants>>& currFrame,
    const MBResources& Resources,
    const std::shared_ptr<SharedSSAO> ssaoPass,
    std::shared_ptr<SSAA> antiAliasingPrimePath)
{
 
    cmdList->StartMark(L"MB miton blur");

    // depth
    cmdList->TransitionBarrier(ssaoPass->GetPrimeResources().GetDepthMap(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    // frame
    cmdList->TransitionBarrier(antiAliasingPrimePath->GetRenderTarget(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    //cmdList->TransitionBarrier(Resources.GetVelocityMap(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(Resources.GetNeighbourmaxMap(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(Resources.GetMbMap(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    cmdList->SetComputeRootSignature(Resources.GetMbRootSignature());
    cmdList->SetPipelineState(Resources.GetMbPSO());
    cmdList->SetDescriptorsHeap(Resources.GetMbMapUAV());

    cmdList->SetComputeRootConstantBufferView(0, *currFrame.get());
    // depth
    cmdList->SetComputeRootDescriptorTable(1, ssaoPass->GetPrimeResources().GetDepthMapSRV());
    //cmdList->SetComputeRootDescriptorTable(2, Resources.GetVelocityMapSRV()); // !!! сдвигает индексы в следующих ресурсах
    cmdList->SetComputeRootDescriptorTable(2, Resources.GetNeighbourmaxMapSRV());
    // frame
    cmdList->SetComputeRootDescriptorTable(3, antiAliasingPrimePath->GetSRV());
    cmdList->SetComputeRootDescriptorTable(4, Resources.GetMbMapUAV());

    // Dispatch
    auto BlockSizeX = tileSize; // TODO
    auto BlockSizeY = tileSize; // TODO
    UINT groupX = (RenderTargetWidth + BlockSizeX - 1) / BlockSizeX;
    UINT groupY = (RenderTargetHeight + BlockSizeY - 1) / BlockSizeY;
    cmdList->Dispatch(groupX, groupY, 1);

    // depth
    cmdList->TransitionBarrier(ssaoPass->GetPrimeResources().GetDepthMap(), D3D12_RESOURCE_STATE_COMMON);
    // frame
    //cmdList->TransitionBarrier(antiAliasingPrimePath->GetRenderTarget(), D3D12_RESOURCE_STATE_COMMON);

   // cmdList->TransitionBarrier(Resources.GetVelocityMap(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(Resources.GetNeighbourmaxMap(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(Resources.GetMbMap(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();

    cmdList->EndMark();
}