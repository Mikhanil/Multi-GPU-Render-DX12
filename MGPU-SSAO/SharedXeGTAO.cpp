#include "SharedXeGTAO.h"

#include <d3dcompiler.h>

#include "GCommandList.h"

namespace
{
    constexpr UINT kDepthMipUavs = 5;
    constexpr UINT kMainSrvBase = 5;
    constexpr UINT kAoPingUav = 7;
    constexpr UINT kEdgesUav = 8;
    constexpr UINT kAoPingSrv = 9;
    constexpr UINT kEdgesSrv0 = 10;
    constexpr UINT kAoPongUav = 11;
    constexpr UINT kAoPongSrv = 12;
    constexpr UINT kEdgesSrv1 = 13;

    UINT IntDivRoundUp(UINT a, UINT b) { return (a + b - 1) / b; }

    static constexpr DXGI_FORMAT kWorkingDepthFormat = DXGI_FORMAT_R16_FLOAT;
    static constexpr DXGI_FORMAT kAoTermFormat = DXGI_FORMAT_R32_UINT;
    static constexpr DXGI_FORMAT kEdgesFormat = DXGI_FORMAT_R8_UNORM;
}

void XeGTAOResources::EnsureWorkingTextures(uint32_t width, uint32_t height)
{
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    desc.MipLevels = 5;
    desc.Format = kWorkingDepthFormat;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (xeWorkingDepth.GetD3D12Resource() == nullptr)
    {
        xeWorkingDepth = GTexture(device, desc, L"XeGTAO WorkingDepth " + device->GetName(), TextureUsage::Normalmap,
                                  nullptr);
    }
    else
    {
        GTexture::Resize(xeWorkingDepth, width, height, 1);
    }

    desc.MipLevels = 1;
    desc.Format = kAoTermFormat;

    if (xeAoPing.GetD3D12Resource() == nullptr)
    {
        xeAoPing = GTexture(device, desc, L"XeGTAO AoPing " + device->GetName(), TextureUsage::Normalmap, nullptr);
        xeAoPong = GTexture(device, desc, L"XeGTAO AoPong " + device->GetName(), TextureUsage::Normalmap, nullptr);
    }
    else
    {
        GTexture::Resize(xeAoPing, width, height, 1);
        GTexture::Resize(xeAoPong, width, height, 1);
    }

    desc.Format = kEdgesFormat;
    if (xeEdges.GetD3D12Resource() == nullptr)
    {
        xeEdges = GTexture(device, desc, L"XeGTAO Edges " + device->GetName(), TextureUsage::Normalmap, nullptr);
    }
    else
    {
        GTexture::Resize(xeEdges, width, height, 1);
    }
}

void XeGTAOResources::RebuildXeGTAOInternalDescriptors() const
{
    if (xeWorkingDepth.GetD3D12Resource() == nullptr)
    {
        return;
    }

    for (UINT mip = 0; mip < kDepthMipUavs; ++mip)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = kWorkingDepthFormat;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = mip;
        uavDesc.Texture2D.PlaneSlice = 0;
        xeWorkingDepth.CreateUnorderedAccessView(&uavDesc, &xeDescriptorBase, mip);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 5;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    srvDesc.Format = kWorkingDepthFormat;
    xeWorkingDepth.CreateShaderResourceView(&srvDesc, &xeDescriptorBase, kMainSrvBase);

    srvDesc.Format = NormalMapFormat;
    srvDesc.Texture2D.MipLevels = 1;
    normalMap.CreateShaderResourceView(&srvDesc, &xeDescriptorBase, kMainSrvBase + 1);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavR32{};
    uavR32.Format = kAoTermFormat;
    uavR32.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavR32.Texture2D.MipSlice = 0;
    uavR32.Texture2D.PlaneSlice = 0;
    xeAoPing.CreateUnorderedAccessView(&uavR32, &xeDescriptorBase, kAoPingUav);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavR8{};
    uavR8.Format = kEdgesFormat;
    uavR8.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavR8.Texture2D.MipSlice = 0;
    uavR8.Texture2D.PlaneSlice = 0;
    xeEdges.CreateUnorderedAccessView(&uavR8, &xeDescriptorBase, kEdgesUav);

    srvDesc.Format = kAoTermFormat;
    srvDesc.Texture2D.MipLevels = 1;
    xeAoPing.CreateShaderResourceView(&srvDesc, &xeDescriptorBase, kAoPingSrv);

    srvDesc.Format = kEdgesFormat;
    xeEdges.CreateShaderResourceView(&srvDesc, &xeDescriptorBase, kEdgesSrv0);
    xeEdges.CreateShaderResourceView(&srvDesc, &xeDescriptorBase, kEdgesSrv1);

    xeAoPong.CreateUnorderedAccessView(&uavR32, &xeDescriptorBase, kAoPongUav);

    srvDesc.Format = kAoTermFormat;
    srvDesc.Texture2D.MipLevels = 1;
    xeAoPong.CreateShaderResourceView(&srvDesc, &xeDescriptorBase, kAoPongSrv);
}

void XeGTAOResources::OnResize(uint32_t width, uint32_t height)
{
    EnsureWorkingTextures(width, height);
    SSAOResources::OnResize(width, height);
}

void XeGTAOResources::RebuildDescriptors() const
{
    SSAOResources::RebuildDescriptors();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = AmbientMapFormat;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.PlaneSlice = 0;
    uavDesc.Texture2D.MipSlice = 0;
    GetAmbientMap().CreateUnorderedAccessView(&uavDesc, &ambientMapUAV);

    RebuildXeGTAOInternalDescriptors();
}

std::shared_ptr<GRootSignature> XeGTAOResources::CreateXeGTAORootSignature(int srvCount,
                                                                             int uavCount,
                                                                             int extraCbvCount)
{
    auto rs = std::make_shared<GRootSignature>();

    rs->AddConstantBufferParameter(0);

    for (int i = 0; i < extraCbvCount; ++i)
    {
        rs->AddConstantBufferParameter(i + 1);
    }

    CD3DX12_DESCRIPTOR_RANGE srvTable;
    CD3DX12_DESCRIPTOR_RANGE uavTable;

    if (srvCount > 0)
    {
        srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, srvCount, 0, 0);
        rs->AddDescriptorParameter(&srvTable, 1);
    }

    if (uavCount > 0)
    {
        uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, uavCount, 0, 0);
        rs->AddDescriptorParameter(&uavTable, 1);
    }

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        0, D3D12_FILTER_MIN_MAG_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        1, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

    rs->AddStaticSampler(pointClamp);
    rs->AddStaticSampler(linearClamp);
    rs->Initialize(device);

    return rs;
}

void XeGTAOResources::Initialize(const std::shared_ptr<GDevice>& Device, const D3D12_INPUT_LAYOUT_DESC& layout)
{
    this->device = Device;
    SSAOResources::Initialize(Device, layout);

    xeDescriptorBase = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, XeDescriptorCount);

    ambientMapUAV = ambientMapSRV.Offset(1);

    BuildPSO();
}

void XeGTAOResources::BuildPSO()
{
    BuildPass(Prefilter, L"Shaders\\XeGTAO_PrefilterDepths16x16.hlsl", "CSPrefilterDepths16x16",
              1, 5, 0);
    BuildPass(Main, L"Shaders\\XeGTAO_MainPass.hlsl", "CSGTAOHigh",
              2, 2, 0);
    BuildPass(Denoise, L"Shaders\\XeGTAO_Denoise.hlsl", "CSDenoisePass",
              2, 1, 0);
    BuildPass(DenoiseLast, L"Shaders\\XeGTAO_Denoise.hlsl", "CSDenoiseLastPass",
              2, 1, 0);
    BuildPass(Composite, L"Shaders\\XeGTAO_Composite.hlsl", "Composite",
              1, 1, 1);
}

void XeGTAOResources::BuildPass(Pass& pass,
                                const std::wstring& fileName,
                                const std::string& entryPoint,
                                int srvCount,
                                int uavCount,
                                int cbvCount)
{
    pass.Shader = std::make_shared<GShader>(
        fileName,
        ComputeShader, nullptr,
        entryPoint,
        "cs_5_1");

    pass.Shader->LoadAndCompile();

    pass.srvCount = srvCount;
    pass.uavCount = uavCount;
    pass.extraCbvCount = cbvCount;

    pass.RootSignature = CreateXeGTAORootSignature(srvCount, uavCount, cbvCount);

    pass.PSO = std::make_shared<ComputePSO>();
    pass.PSO->SetShader(pass.Shader.get());
    pass.PSO->SetRootSignature(*pass.RootSignature);
    pass.PSO->Initialize(device);
}

void XeGTAOResources::ApplyPass(GCommandList& cmdList, Pass& pass) const
{
    cmdList.SetComputeRootSignature(*pass.RootSignature);
    cmdList.SetPipelineState(*pass.PSO);
}

void SharedXeGTAO::Initialize(const std::shared_ptr<GDevice>& PrimeDevice, const std::shared_ptr<GDevice>& SecondDevice,
                                const D3D12_INPUT_LAYOUT_DESC& layout, UINT width, UINT height)
{
    primeCompositeCB = std::make_shared<ConstantUploadBuffer<XeGTAOCompositeConstants>>(
        PrimeDevice, 1, PrimeDevice->GetName() + L" XeGTAO Composite CB");
    secondCompositeCB = std::make_shared<ConstantUploadBuffer<XeGTAOCompositeConstants>>(
        SecondDevice, 1, SecondDevice->GetName() + L" XeGTAO Composite CB");

    primeResources.Initialize(PrimeDevice, layout);
    primeResources.OnResize(width, height);

    secondResources.Initialize(SecondDevice, layout);
    secondResources.OnResize(width, height);

    crossResources.Initialize(primeResources, PrimeDevice, SecondDevice);
    crossResources.OnResize(width, height);

    OnResize(width, height);
}

void SharedXeGTAO::OnResize(UINT newWidth, UINT newHeight)
{
    if (RenderTargetWidth == newWidth && RenderTargetHeight == newHeight)
        return;

    RenderTargetWidth = newWidth;
    RenderTargetHeight = newHeight;

    primeResources.OnResize(newWidth, newHeight);
    secondResources.OnResize(newWidth, newHeight);
    crossResources.OnResize(newWidth, newHeight);
}

void SharedXeGTAO::UpdateCompositeConstants()
{
    XeGTAOCompositeConstants compositeCpu{};
    compositeCpu.GTAOResolutionScale = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    compositeCpu.Intensity = gtaoSettings.FinalValuePower;
    primeCompositeCB->CopyData(0, compositeCpu);
    secondCompositeCB->CopyData(0, compositeCpu);
}

void SharedXeGTAO::Compute(const std::shared_ptr<GCommandList>& cmdList,
                           const std::shared_ptr<ConstantUploadBuffer<GTAOConstants>>& Constants,
                           const XeGTAOResources& Resources)
{
    cmdList->StartMark(L"XeGTAO");

    const std::shared_ptr<ConstantUploadBuffer<XeGTAOCompositeConstants>> compositeGpu =
        (&Resources == &primeResources) ? primeCompositeCB : secondCompositeCB;

    const GDescriptor* const xeBase = Resources.GetXeDescriptorBase();

    // --- Prefilter depth ---
    cmdList->TransitionBarrier(Resources.GetDepthMap(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(Resources.GetXeWorkingDepth(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    Resources.ApplyPass(*cmdList, const_cast<XeGTAOResources&>(Resources).Prefilter);
    cmdList->SetDescriptorsHeap(Resources.GetAmbientMapSRV());
    cmdList->SetComputeRootConstantBufferView(0, *Constants.get());
    cmdList->SetComputeRootDescriptorTable(1, Resources.GetDepthMapSRV());
    cmdList->SetComputeRootDescriptorTable(2, xeBase, 0);

    {
        const UINT tgx = IntDivRoundUp(RenderTargetWidth, XE_GTAO_NUMTHREADS_X);
        const UINT tgy = IntDivRoundUp(RenderTargetHeight, XE_GTAO_NUMTHREADS_Y);
        cmdList->Dispatch(tgx, tgy, 1);
    }

    cmdList->UAVBarrier(Resources.GetXeWorkingDepth());
    cmdList->TransitionBarrier(Resources.GetXeWorkingDepth(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(Resources.GetNormalMap(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(Resources.GetXeAoPing(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->TransitionBarrier(Resources.GetXeEdges(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    // --- Main GTAO ---
    Resources.ApplyPass(*cmdList, const_cast<XeGTAOResources&>(Resources).Main);
    cmdList->SetDescriptorsHeap(Resources.GetAmbientMapSRV());
    cmdList->SetComputeRootConstantBufferView(0, *Constants.get());
    cmdList->SetComputeRootDescriptorTable(1, xeBase, kMainSrvBase);
    cmdList->SetComputeRootDescriptorTable(2, xeBase, kAoPingUav);

    {
        const UINT tgx = IntDivRoundUp(RenderTargetWidth, XE_GTAO_NUMTHREADS_X);
        const UINT tgy = IntDivRoundUp(RenderTargetHeight, XE_GTAO_NUMTHREADS_Y);
        cmdList->Dispatch(tgx, tgy, 1);
    }

    cmdList->UAVBarrier(Resources.GetXeAoPing());
    cmdList->UAVBarrier(Resources.GetXeEdges());
    cmdList->TransitionBarrier(Resources.GetXeAoPing(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(Resources.GetXeEdges(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(Resources.GetXeAoPong(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    const bool runDenoise = gtaoSettings.DenoisePasses > 0;

    if (runDenoise)
    {
        // Denoise pass -> pong
        Resources.ApplyPass(*cmdList, const_cast<XeGTAOResources&>(Resources).Denoise);
        cmdList->SetDescriptorsHeap(Resources.GetAmbientMapSRV());
        cmdList->SetComputeRootConstantBufferView(0, *Constants.get());
        cmdList->SetComputeRootDescriptorTable(1, xeBase, kAoPingSrv);
        cmdList->SetComputeRootDescriptorTable(2, xeBase, kAoPongUav);

        const UINT threadsX = IntDivRoundUp(RenderTargetWidth, 2);
        const UINT tgxD = IntDivRoundUp(threadsX, XE_GTAO_NUMTHREADS_X);
        const UINT tgyD = IntDivRoundUp(RenderTargetHeight, XE_GTAO_NUMTHREADS_Y);
        cmdList->Dispatch(tgxD, tgyD, 1);

        cmdList->UAVBarrier(Resources.GetXeAoPong());
        cmdList->TransitionBarrier(Resources.GetXeAoPong(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(Resources.GetXeAoPing(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->FlushResourceBarriers();

        // Last denoise pass writes ping (final uint AO for Composite)
        Resources.ApplyPass(*cmdList, const_cast<XeGTAOResources&>(Resources).DenoiseLast);
        cmdList->SetDescriptorsHeap(Resources.GetAmbientMapSRV());
        cmdList->SetComputeRootConstantBufferView(0, *Constants.get());
        cmdList->SetComputeRootDescriptorTable(1, xeBase, kAoPongSrv);
        cmdList->SetComputeRootDescriptorTable(2, xeBase, kAoPingUav);
        cmdList->Dispatch(tgxD, tgyD, 1);

        cmdList->UAVBarrier(Resources.GetXeAoPing());
        cmdList->TransitionBarrier(Resources.GetXeAoPing(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->FlushResourceBarriers();
    }

    // --- Composite uint -> R16 ambient ---
    cmdList->TransitionBarrier(Resources.GetAmbientMap(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->FlushResourceBarriers();

    Resources.ApplyPass(*cmdList, const_cast<XeGTAOResources&>(Resources).Composite);
    cmdList->SetDescriptorsHeap(Resources.GetAmbientMapSRV());
    cmdList->SetComputeRootConstantBufferView(0, *Constants.get());
    cmdList->SetComputeRootConstantBufferView(1, *compositeGpu);
    cmdList->SetComputeRootDescriptorTable(2, xeBase, kAoPingSrv);
    cmdList->SetComputeRootDescriptorTable(3, Resources.GetAmbientMapUAV());

    {
        const UINT tgx = IntDivRoundUp(RenderTargetWidth, XE_GTAO_NUMTHREADS_X);
        const UINT tgy = IntDivRoundUp(RenderTargetHeight, XE_GTAO_NUMTHREADS_Y);
        cmdList->Dispatch(tgx, tgy, 1);
    }

    cmdList->TransitionBarrier(Resources.GetAmbientMap(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(Resources.GetNormalMap(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();
    cmdList->EndMark();
}
