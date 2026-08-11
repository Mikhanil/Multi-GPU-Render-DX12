#include "pch.h"
#include "WindFluidSimulator.h"
#include "ComputePSO.h"
#include "GCommandList.h"
#include "GDescriptor.h"
#include "GDevice.h"
#include "GShader.h"
#include "GTexture.h"
#include "d3dUtil.h"

#include <algorithm>
#include <vector>

#include <windows.h>

using namespace PEPEngine::Graphics;

static_assert(sizeof(WindFluidGpuCB) == 256u, "WindFluidGpuCB must be 256 bytes (D3D12 CB alignment)");

namespace
{
    constexpr UINT KThr = 8;

    constexpr UINT SrvVelA = 0;
    constexpr UINT SrvVelB = 1;
    constexpr UINT UavVelA = 2;
    constexpr UINT UavVelB = 3;
    constexpr UINT SrvDiv = 4;
    constexpr UINT UavDiv = 5;
    constexpr UINT SrvPA = 6;
    constexpr UINT UavPA = 7;
    constexpr UINT SrvPB = 8;
    constexpr UINT UavPB = 9;

    UINT DivUp(UINT n, UINT g)
    {
        return (n + g - 1u) / g;
    }

    CD3DX12_RESOURCE_DESC Rt(UINT res, DXGI_FORMAT fmt)
    {
        return CD3DX12_RESOURCE_DESC::Tex2D(fmt, UINT64(res), UINT(res),
                                            1u, 1u, 1u, 0u,
                                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    }

    void MkSrv(GResource& r, DXGI_FORMAT fmt, GDescriptor& heap, UINT o)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC d{};
        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d.Format = fmt;
        d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        d.Texture2D.MostDetailedMip = 0;
        d.Texture2D.MipLevels = 1;
        r.CreateShaderResourceView(&d, &heap, o);
    }

    void MkUav(GResource& r, DXGI_FORMAT fmt, GDescriptor& heap, UINT o)
    {
        DXGI_FORMAT uf = GTexture::GetUAVCompatableFormat(fmt);
        D3D12_UNORDERED_ACCESS_VIEW_DESC u{};
        u.Format = uf;
        u.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        u.Texture2D.MipSlice = 0;
        r.CreateUnorderedAccessView(&u, &heap, o);
    }

    void SampClamp0(GRootSignature& rs)
    {
        CD3DX12_STATIC_SAMPLER_DESC s(
            0,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        rs.AddStaticSampler(s);
    }
}

Microsoft::WRL::ComPtr<ID3D12Resource> WindFluidSimulator::GetVelocityResource() const
{
    Microsoft::WRL::ComPtr<ID3D12Resource> out;
    if (readableSrvHeapIdx_ == SrvVelA && velA_)
    {
        out = velA_->GetD3D12Resource();
    }
    else if (readableSrvHeapIdx_ == SrvVelB && velB_)
    {
        out = velB_->GetD3D12Resource();
    }

    return out;
}

void WindFluidSimulator::Initialize(std::weak_ptr<GDevice> du, UINT gridRes)
{
    device_ = du;
    initialized_ = false;
    needsReset_ = true;

    ReleaseWindFluidGpuState();

    grid_ = std::clamp(gridRes, 32u, 512u);
    velHistoryReadsA_ = true;

    auto dev = device_.lock();
    if (!dev)
        return;

    CreateTexturesAndViews(dev);
    if (!CreateRootsAndPSOs(dev))
        return;

    gpuCB_ = std::make_unique<GBuffer>(dev, static_cast<UINT>(sizeof(WindFluidGpuCB)), 1u, L"WindFluid CB");

    readableSrvHeapIdx_ = SrvVelB;
    readableDyeSrvHeapIdx_ = kInvalidSrvIndex;

    initialized_ = true;
}

void WindFluidSimulator::ReleaseWindFluidGpuState()
{
    // Ensure no queue still references PSOs/resources before releasing them.
    if (auto dev = device_.lock())
    {
        try
        {
            if (auto qg = dev->GetCommandQueue(GQueueType::Graphics))
                qg->Flush();
            if (auto qc = dev->GetCommandQueue(GQueueType::Compute))
                qc->Flush();
        }
        catch (...)
        {
            // Ignore here; best-effort drain to avoid debug-layer lifetime crashes.
        }
    }

    readableSrvHeapIdx_ = kInvalidSrvIndex;
    readableDyeSrvHeapIdx_ = kInvalidSrvIndex;
    needsReset_ = true;

    rsInject_.reset();
    rsAdvect_.reset();
    rsDiv_.reset();
    rsJacobi_.reset();
    rsProject_.reset();
    psoInject_.reset();
    psoAdvect_.reset();
    psoDiv_.reset();
    psoJacobi_.reset();
    psoProject_.reset();

    velA_.reset();
    velB_.reset();
    div_.reset();
    pressA_.reset();
    pressB_.reset();
    gpuCB_.reset();
    descriptors_ = GDescriptor{};
}

bool WindFluidSimulator::CreateTexturesAndViews(const std::shared_ptr<GDevice>& dev)
{
    descriptors_ = GDescriptor{};
    descriptors_ = dev->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 10u);

    constexpr DXGI_FORMAT kVF = DXGI_FORMAT_R16G16_FLOAT;
    constexpr DXGI_FORMAT kSF = DXGI_FORMAT_R16_FLOAT;

    velA_ = std::make_unique<GResource>(dev, Rt(grid_, kVF), L"WindFluid VelA",
                                      nullptr, D3D12_RESOURCE_STATE_COMMON);
    velB_ = std::make_unique<GResource>(dev, Rt(grid_, kVF), L"WindFluid VelB",
                                      nullptr, D3D12_RESOURCE_STATE_COMMON);
    div_ = std::make_unique<GResource>(dev, Rt(grid_, kSF), L"WindFluid Div",
                                      nullptr, D3D12_RESOURCE_STATE_COMMON);
    pressA_ = std::make_unique<GResource>(dev, Rt(grid_, kSF), L"WindFluid Pa",
                                         nullptr, D3D12_RESOURCE_STATE_COMMON);
    pressB_ = std::make_unique<GResource>(dev, Rt(grid_, kSF), L"WindFluid Pb",
                                         nullptr, D3D12_RESOURCE_STATE_COMMON);

    MkSrv(*velA_, kVF, descriptors_, SrvVelA);
    MkSrv(*velB_, kVF, descriptors_, SrvVelB);
    MkUav(*velA_, kVF, descriptors_, UavVelA);
    MkUav(*velB_, kVF, descriptors_, UavVelB);
    MkSrv(*div_, kSF, descriptors_, SrvDiv);
    MkUav(*div_, kSF, descriptors_, UavDiv);
    MkSrv(*pressA_, kSF, descriptors_, SrvPA);
    MkUav(*pressA_, kSF, descriptors_, UavPA);
    MkSrv(*pressB_, kSF, descriptors_, SrvPB);
    MkUav(*pressB_, kSF, descriptors_, UavPB);

    return true;
}

bool WindFluidSimulator::CreateRootsAndPSOs(const std::shared_ptr<GDevice>& dev)
{
    try
    {
    auto cmp = [&](const wchar_t* f, const char* ep)
    {
        auto sh = std::make_shared<GShader>(f, ComputeShader, nullptr, ep, "cs_5_1");
        sh->LoadAndCompile();
        return sh;
    };

    CD3DX12_DESCRIPTOR_RANGE rs{}, ua{};
    rs.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u);
    ua.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1u, 0u);

    rsInject_ = std::make_shared<GRootSignature>();
    rsInject_->AddConstantBufferParameter(0);
    rsInject_->AddDescriptorParameter(&rs, 1);
    rsInject_->AddDescriptorParameter(&ua, 1);
    SampClamp0(*rsInject_);
    rsInject_->Initialize(dev, false, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    rsAdvect_ = std::make_shared<GRootSignature>();
    rsAdvect_->AddConstantBufferParameter(0);
    rsAdvect_->AddDescriptorParameter(&rs, 1);
    rsAdvect_->AddDescriptorParameter(&ua, 1);
    SampClamp0(*rsAdvect_);
    rsAdvect_->Initialize(dev, false, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    rsDiv_ = std::make_shared<GRootSignature>();
    rsDiv_->AddConstantBufferParameter(0);
    rsDiv_->AddDescriptorParameter(&rs, 1);
    rsDiv_->AddDescriptorParameter(&ua, 1);
    rsDiv_->Initialize(dev, false, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    CD3DX12_DESCRIPTOR_RANGE rD{}, rP{}, rU{};
    rD.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u);
    rP.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 1u);
    rU.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1u, 0u);

    rsJacobi_ = std::make_shared<GRootSignature>();
    rsJacobi_->AddConstantBufferParameter(0);
    rsJacobi_->AddDescriptorParameter(&rD, 1);
    rsJacobi_->AddDescriptorParameter(&rP, 1);
    rsJacobi_->AddDescriptorParameter(&rU, 1);
    rsJacobi_->Initialize(dev, false, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    CD3DX12_DESCRIPTOR_RANGE v{}, p{}, vu{};
    v.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 0u);
    p.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1u, 1u);
    vu.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1u, 0u);

    rsProject_ = std::make_shared<GRootSignature>();
    rsProject_->AddConstantBufferParameter(0);
    rsProject_->AddDescriptorParameter(&v, 1);
    rsProject_->AddDescriptorParameter(&p, 1);
    rsProject_->AddDescriptorParameter(&vu, 1);
    rsProject_->Initialize(dev, false, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    // Keep shared_ptr<GShader> alive across SetShader + TryInitialize: ComputePSO only stores bytecode
    // pointers; temps like cmp(...).get() would destroy the blob before CreateComputePipelineState runs.
    auto shWindInject = cmp(L"Shaders\\WindFluidInject.hlsl", "CS_Main");
    psoInject_ = std::make_shared<ComputePSO>();
    psoInject_->SetRootSignature(*rsInject_);
    psoInject_->SetShader(shWindInject.get());
    if (FAILED(psoInject_->TryInitialize(dev)))
    {
        OutputDebugStringW(L"[WindFluid] Inject PSO: CreateComputePipelineState failed\n");
        ReleaseWindFluidGpuState();
        return false;
    }

    auto shWindAdvect = cmp(L"Shaders\\WindFluidAdvect.hlsl", "CS_Main");
    psoAdvect_ = std::make_shared<ComputePSO>();
    psoAdvect_->SetRootSignature(*rsAdvect_);
    psoAdvect_->SetShader(shWindAdvect.get());
    if (FAILED(psoAdvect_->TryInitialize(dev)))
    {
        OutputDebugStringW(L"[WindFluid] Advect PSO: CreateComputePipelineState failed\n");
        ReleaseWindFluidGpuState();
        return false;
    }

    auto shWindDiv = cmp(L"Shaders\\WindFluidDivergence.hlsl", "CS_Main");
    psoDiv_ = std::make_shared<ComputePSO>();
    psoDiv_->SetRootSignature(*rsDiv_);
    psoDiv_->SetShader(shWindDiv.get());
    if (FAILED(psoDiv_->TryInitialize(dev)))
    {
        OutputDebugStringW(L"[WindFluid] Divergence PSO: CreateComputePipelineState failed\n");
        ReleaseWindFluidGpuState();
        return false;
    }

    auto shWindJacobi = cmp(L"Shaders\\WindFluidPressure.hlsl", "CS_Main");
    psoJacobi_ = std::make_shared<ComputePSO>();
    psoJacobi_->SetRootSignature(*rsJacobi_);
    psoJacobi_->SetShader(shWindJacobi.get());
    if (FAILED(psoJacobi_->TryInitialize(dev)))
    {
        OutputDebugStringW(L"[WindFluid] Pressure/Jacobi PSO: CreateComputePipelineState failed\n");
        ReleaseWindFluidGpuState();
        return false;
    }

    auto shWindProject = cmp(L"Shaders\\WindFluidProject.hlsl", "CS_Main");
    psoProject_ = std::make_shared<ComputePSO>();
    psoProject_->SetRootSignature(*rsProject_);
    psoProject_->SetShader(shWindProject.get());
    if (FAILED(psoProject_->TryInitialize(dev)))
    {
        OutputDebugStringW(L"[WindFluid] Project PSO: CreateComputePipelineState failed\n");
        ReleaseWindFluidGpuState();
        return false;
    }

    return true;
    }
    catch (const PEPEngine::Utils::DxException& e)
    {
        OutputDebugStringW((L"[WindFluid] DxException: " + e.ToString() + L"\n").c_str());
        ReleaseWindFluidGpuState();
        return false;
    }
    catch (const std::exception& e)
    {
        OutputDebugStringA("[WindFluid] std::exception during CreateRootsAndPSOs: ");
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");
        ReleaseWindFluidGpuState();
        return false;
    }
    catch (...)
    {
        OutputDebugStringW(L"[WindFluid] Unknown exception during CreateRootsAndPSOs\n");
        ReleaseWindFluidGpuState();
        return false;
    }
}

void WindFluidSimulator::Simulate(const std::shared_ptr<GCommandList>& cmd, const WindFluidGpuCB& in)
{
    if (!initialized_ || !gpuCB_ || !velA_ || !velA_->IsValid())
        return;

    const D3D12_RESOURCE_DESC velDesc = velA_->GetD3D12ResourceDesc();
    const UINT texGrid = static_cast<UINT>(velDesc.Width);
    if (texGrid == 0u || texGrid != static_cast<UINT>(velDesc.Height))
        return;

    if (texGrid != grid_)
    {
        grid_ = texGrid;
        needsReset_ = true;
    }

    WindFluidGpuCB p{};
    p = in;
    p.GridW = grid_;
    p.GridH = grid_;
    p.InvGridW = 1.0f / static_cast<float>(grid_);
    p.InvGridH = 1.0f / static_cast<float>(grid_);
    p.JacobiIterations = std::clamp(p.JacobiIterations, 2u, 40u);

    gpuCB_->LoadData(&p, cmd);

    const UINT gx = DivUp(grid_, KThr);
    const UINT gy = DivUp(grid_, KThr);
    const bool hA = velHistoryReadsA_;

    GResource* injReadRes = hA ? velA_.get() : velB_.get();
    GResource* injWriteRes = hA ? velB_.get() : velA_.get();
    const UINT injSrvOff = hA ? SrvVelA : SrvVelB;
    const UINT injUavOff = hA ? UavVelB : UavVelA;
    const UINT advSrv = hA ? SrvVelB : SrvVelA;
    const UINT projUav = hA ? UavVelA : UavVelB;

    cmd->SetDescriptorsHeap(&descriptors_);

    if (needsReset_)
    {
        auto uploadZeroTex = [&](GResource& rr, UINT bytesPerPixel)
        {
            const UINT rowBytes = grid_ * bytesPerPixel;
            const UINT rowPitch =
                (rowBytes + (D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u)) &
                ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
            std::vector<uint8_t> zeros(static_cast<size_t>(rowPitch) * static_cast<size_t>(grid_), 0u);
            D3D12_SUBRESOURCE_DATA sub{};
            sub.pData = zeros.data();
            sub.RowPitch = static_cast<LONG_PTR>(rowPitch);
            sub.SlicePitch = static_cast<LONG_PTR>(zeros.size());
            cmd->UpdateSubresource(rr, &sub, 1);
        };

        // Explicitly zero initialize all solver textures once after (re)initialization.
        uploadZeroTex(*velA_, sizeof(uint16_t) * 2u);  // R16G16_FLOAT
        uploadZeroTex(*velB_, sizeof(uint16_t) * 2u);  // R16G16_FLOAT
        uploadZeroTex(*div_, sizeof(uint16_t));        // R16_FLOAT
        uploadZeroTex(*pressA_, sizeof(uint16_t));     // R16_FLOAT
        uploadZeroTex(*pressB_, sizeof(uint16_t));     // R16_FLOAT
        needsReset_ = false;
    }

    auto toSrv = [&](GResource& rr)
    {
        cmd->TransitionBarrier(rr, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmd->FlushResourceBarriers();
    };
    auto toUav = [&](GResource& rr)
    {
        cmd->TransitionBarrier(rr, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmd->FlushResourceBarriers();
    };

    // Inject: SRV read + UAV write on separate ping-pong targets (invalid to bind SRV on UAV state).
    toSrv(*injReadRes);
    toUav(*injWriteRes);

    cmd->SetPipelineState(*psoInject_);
    cmd->SetRootSignature(*rsInject_);
    cmd->SetRootConstantBufferView(0, *gpuCB_);
    cmd->SetRootDescriptorTable(1, &descriptors_, injSrvOff);
    cmd->SetRootDescriptorTable(2, &descriptors_, injUavOff);
    cmd->Dispatch(gx, gy, 1);

    cmd->UAVBarrier(*injWriteRes, true);

    GResource* vAfterAdv = injWriteRes;
    toSrv(*vAfterAdv);

    toUav(*div_);
    cmd->SetPipelineState(*psoDiv_);
    cmd->SetRootSignature(*rsDiv_);
    cmd->SetRootConstantBufferView(0, *gpuCB_);
    cmd->SetRootDescriptorTable(1, &descriptors_, advSrv);
    cmd->SetRootDescriptorTable(2, &descriptors_, UavDiv);
    cmd->Dispatch(gx, gy, 1);
    toSrv(*div_);

    // Jacobi ping-pong pressure
    const UINT jacCount = std::clamp(p.JacobiIterations, 2u, 40u);

    UINT pressureFinalSrv = SrvPB;

    cmd->SetPipelineState(*psoJacobi_);
    cmd->SetRootSignature(*rsJacobi_);

    bool pinA = true;
    for (UINT it = 0u; it < jacCount; ++it)
    {
        GResource& pinTex = pinA ? *pressA_ : *pressB_;
        GResource& pouTex = pinA ? *pressB_ : *pressA_;

        toSrv(pinTex);
        toUav(pouTex);

        cmd->TransitionBarrier(*div_, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmd->FlushResourceBarriers();

        const UINT srvPinHeap = pinA ? SrvPA : SrvPB;
        const UINT uavPoHeap = pinA ? UavPB : UavPA;

        cmd->SetRootConstantBufferView(0, *gpuCB_);
        cmd->SetRootDescriptorTable(1, &descriptors_, SrvDiv);
        cmd->SetRootDescriptorTable(2, &descriptors_, srvPinHeap);
        cmd->SetRootDescriptorTable(3, &descriptors_, uavPoHeap);

        cmd->Dispatch(gx, gy, 1);

        pressureFinalSrv = pinA ? SrvPB : SrvPA;
        pinA = !pinA;
    }

    GResource* pFinalTex = pressureFinalSrv == SrvPA ? pressA_.get() : pressB_.get();
    toSrv(*pFinalTex);
    toSrv(*vAfterAdv);

    cmd->SetPipelineState(*psoProject_);
    cmd->SetRootSignature(*rsProject_);
    cmd->SetRootConstantBufferView(0, *gpuCB_);
    cmd->SetRootDescriptorTable(1, &descriptors_, advSrv);
    cmd->SetRootDescriptorTable(2, &descriptors_, pressureFinalSrv);

    GResource* vOutVel = projUav == UavVelA ? velA_.get() : velB_.get();
    toUav(*vOutVel);
    cmd->SetRootDescriptorTable(3, &descriptors_, projUav);

    cmd->Dispatch(gx, gy, 1);

    readableSrvHeapIdx_ = (projUav == UavVelB) ? SrvVelB : SrvVelA;
    velHistoryReadsA_ = (readableSrvHeapIdx_ == SrvVelA);

    GResource* readableVel = readableSrvHeapIdx_ == SrvVelA ? velA_.get() : velB_.get();
    if (readableVel)
    {
        toSrv(*readableVel);
    }
}

void WindFluidSimulator::PublishReadableSrvTo(GDescriptor* destHeap, UINT destSrvOffset) const
{
    if (!initialized_ || destHeap == nullptr || destHeap->IsNull())
        return;

    const GResource* src = readableSrvHeapIdx_ == SrvVelA ? velA_.get() : velB_.get();
    if (!src || !src->IsValid())
        return;

    D3D12_SHADER_RESOURCE_VIEW_DESC d{};
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Format = DXGI_FORMAT_R16G16_FLOAT;
    d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Texture2D.MostDetailedMip = 0;
    d.Texture2D.MipLevels = 1;
    src->CreateShaderResourceView(&d, destHeap, destSrvOffset);
}
