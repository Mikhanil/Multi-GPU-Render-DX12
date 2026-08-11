#include "pch.h"
#include "CrossAdapterGrassEmitter.h"
#include "GCommandList.h"
#include "GResource.h"
#include "GTexture.h"
#include "MathHelper.h"
#include "d3dUtil.h"
#include <algorithm>
#include <vector>
#include "GameObject.h"
#include "Transform.h"

namespace
{
const D3D_SHADER_MACRO kComputeGrassExpandDefines[] =
{
    {"COMPUTE_GRASS_EXPAND_PASS", "1"},
    {nullptr, nullptr}
};

void UpdateWindFieldWorldParams(GrassEmitterData& emitterData, const GrassCullData& cullData,
                                const float fluidGridResolution)
{
    const float cx = cullData.World._41;
    const float cz = cullData.World._43;
    const float sx = sqrtf(cullData.World._11 * cullData.World._11 +
                           cullData.World._21 * cullData.World._21 +
                           cullData.World._31 * cullData.World._31);
    const float sz = sqrtf(cullData.World._13 * cullData.World._13 +
                           cullData.World._23 * cullData.World._23 +
                           cullData.World._33 * cullData.World._33);
    const float halfU = std::max(1.0f, emitterData.WorldSize * 0.5f * std::max(sx, 1e-4f));
    const float halfV = std::max(1.0f, emitterData.WorldSize * 0.5f * std::max(sz, 1e-4f));
    const float half = std::max(halfU, halfV);
    const float gridRes = std::max(fluidGridResolution, 1.0f);
    const float cellWorld = std::max(1e-4f, (half * 2.0f) / gridRes);
    emitterData.WindFieldWorldParams = Vector4(cx, cz, half, cellWorld);
}

template <typename T>
void LoadAlignedConstantBuffer(const std::shared_ptr<GBuffer>& buffer, const T& data,
                               const std::shared_ptr<GCommandList>& cmdList)
{
    const UINT alignedBytes = d3dUtil::CalcConstantBufferByteSize(static_cast<UINT>(sizeof(T)));
    std::vector<uint8_t> padded(alignedBytes, 0);
    memcpy(padded.data(), &data, sizeof(T));
    buffer->LoadData(padded.data(), cmdList);
}
}

CrossAdapterGrassEmitter::CrossAdapterGrassEmitter(std::shared_ptr<GDevice> primeDev,
    const std::shared_ptr<GDevice>& secondDev,
    uint32_t grassCount, float worldSize,
    uint32_t lod0BladeCount, uint32_t lod1BladeCount)
    : primeDevice(primeDev), secondDevice(secondDev)
{
    emitterData.GrassCount = grassCount;
    emitterData.WorldSize = worldSize;
    emitterData.QuadSize = 10.0f;
    emitterData.WindStrength = 0.5f;
    emitterData.WindIntensity = 1.0f;
    emitterData.WindAmplitude = 1.0f;
    emitterData.Lod0BladeWidthScale = 1.0f;
    emitterData.Lod0BladeHeightScale = 1.0f;
    emitterData.Lod1BladeWidthScale = 1.0f;
    emitterData.Lod1BladeHeightScale = 1.0f;
    emitterData.Lod0SdofNaturalFreq = 2.5f;
    emitterData.Lod0SdofDampingRatio = 0.35f;
    emitterData.Time = 0.0f;
    emitterData.GridSize = static_cast<uint32_t>(std::sqrt(static_cast<float>(grassCount)));
    emitterData.Lod0BladeCount = std::max(1u, std::min(lod0BladeCount, kMaxBladeCount));
    emitterData.Lod1BladeCount = std::max(1u, std::min(lod1BladeCount, kMaxBladeCount));
    emitterData.WindDirection = Vector2(1.0f, 0.0f);
    emitterData.AtlasTextureCount = 1;
    emitterData.Lod0LeanGain = 3.0f;
    emitterData.WindFluidObstacleA =
        Vector4(windFluidWallA_.x, windFluidWallA_.y, windFluidWallA_.z, 0.0f);
    emitterData.WindFluidObstacleB =
        Vector4(windFluidWallB_.x, grassObstacleWakeLean_, windFluidWallB_.z, 0.0f);

    // ������� �������� ������� � 3 �����������
    primeGrassEmitter = std::make_shared<GrassEmitter>(
        primeDevice, grassCount, worldSize, emitterData.Lod0BladeCount, emitterData.Lod1BladeCount);

    cullData.MaxDistance = 1800.0f;
    cullData.Lod0Distance = 900.0f;
    cullData.Lod1Distance = 1200.0f;
    cullData.Lod0BaseSegments = 4u;
    cullData.WindTessellationScale = 4.0f;
}

CrossAdapterGrassEmitter::~CrossAdapterGrassEmitter()
{
    if (secondDevice)
    {
        try
        {
            if (auto qg = secondDevice->GetCommandQueue(GQueueType::Graphics))
                qg->Flush();
            if (auto qc = secondDevice->GetCommandQueue(GQueueType::Compute))
                qc->Flush();
        }
        catch (...)
        {
        }
    }
}

void CrossAdapterGrassEmitter::InitPSO(const std::shared_ptr<GDevice>& otherDevice)
{
    CD3DX12_DESCRIPTOR_RANGE uavRange;
    uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

    computeRS = std::make_shared<GRootSignature>();
    computeRS->AddConstantBufferParameter(0);
    computeRS->AddDescriptorParameter(&uavRange, 1);
    computeRS->Initialize(otherDevice, false, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    auto generateShader = std::make_shared<GShader>(L"Shaders\\ComputeGrass.hlsl", ComputeShader, nullptr, "CS", "cs_5_1");
    generateShader->LoadAndCompile();

    generatePSO = std::make_shared<ComputePSO>();
    generatePSO->SetRootSignature(*computeRS.get());
    generatePSO->SetShader(generateShader.get());
    generatePSO->Initialize(secondDevice);

    CD3DX12_DESCRIPTOR_RANGE expandInputRange;
    expandInputRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0);
    CD3DX12_DESCRIPTOR_RANGE expandOutputRanges[2];
    expandOutputRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    expandOutputRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);

    expandRS = std::make_shared<GRootSignature>();
    expandRS->AddConstantBufferParameter(0);
    expandRS->AddConstantBufferParameter(1);
    expandRS->AddDescriptorParameter(&expandInputRange, 1);
    expandRS->AddDescriptorParameter(&expandOutputRanges[0], 1);
    expandRS->AddDescriptorParameter(&expandOutputRanges[1], 1);
    CD3DX12_STATIC_SAMPLER_DESC expandSampler(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    expandRS->AddStaticSampler(expandSampler);
    expandRS->Initialize(otherDevice, false, D3D12_ROOT_SIGNATURE_FLAG_NONE);

    auto expandShader = std::make_shared<GShader>(
        L"Shaders\\ComputeGrass.hlsl", ComputeShader, kComputeGrassExpandDefines, "CS_ExpandGrassToVertices", "cs_5_1");
    expandShader->LoadAndCompile();

    expandPSO = std::make_shared<ComputePSO>();
    expandPSO->SetRootSignature(*expandRS.get());
    expandPSO->SetShader(expandShader.get());
    expandPSO->Initialize(secondDevice);

    CD3DX12_DESCRIPTOR_RANGE expandedBufferRange;
    expandedBufferRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 8);
    CD3DX12_DESCRIPTOR_RANGE textureRange;
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    drawRS = std::make_shared<GRootSignature>();
    drawRS->AddConstantBufferParameter(0);
    drawRS->AddConstantBufferParameter(1);
    drawRS->AddDescriptorParameter(&expandedBufferRange, 1);
    drawRS->AddDescriptorParameter(&textureRange, 1);
    CD3DX12_STATIC_SAMPLER_DESC sampler(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    drawRS->AddStaticSampler(sampler);
    drawRS->Initialize(primeDevice);

    auto expandedVS = std::make_shared<GShader>(L"Shaders\\GrassDraw.hlsl", VertexShader, nullptr, "VS_Expanded", "vs_5_1");
    expandedVS->LoadAndCompile();
    auto expandedPS = std::make_shared<GShader>(L"Shaders\\GrassDraw.hlsl", PixelShader, nullptr, "PS_Expanded", "ps_5_1");
    expandedPS->LoadAndCompile();

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { nullptr, 0 };
    psoDesc.pRootSignature = drawRS->GetNativeSignature().Get();
    psoDesc.VS = expandedVS->GetShaderResource();
    psoDesc.PS = expandedPS->GetShaderResource();
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    D3D12_RENDER_TARGET_BLEND_DESC blendDesc = {};
    blendDesc.BlendEnable = true;
    blendDesc.LogicOpEnable = false;
    blendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.SrcBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.DestBlendAlpha = D3D12_BLEND_ONE;
    blendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
    blendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.BlendState.RenderTarget[0] = blendDesc;
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = GetSRGBFormat(BackBufferFormat);
    psoDesc.DSVFormat = DepthStencilFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    expandedDrawPSO = std::make_shared<GraphicPSO>(RenderMode::Transparent);
    expandedDrawPSO->SetPsoDesc(psoDesc);
    expandedDrawPSO->Initialize(primeDevice);
}

void CrossAdapterGrassEmitter::CreateBuffers()
{
    if (grassBuffer)
    {
        grassBuffer->Reset();
        grassBuffer.reset();
    }

    if (crossAdapterGrassBuffer)
    {
        crossAdapterGrassBuffer->Reset();
        crossAdapterGrassBuffer.reset();
    }
    if (expandedVertexBuffer)
    {
        expandedVertexBuffer->Reset();
        expandedVertexBuffer.reset();
    }
    if (crossAdapterExpandedVertexBuffer)
    {
        crossAdapterExpandedVertexBuffer->Reset();
        crossAdapterExpandedVertexBuffer.reset();
    }
    if (primeExpandedVertexBuffer)
    {
        primeExpandedVertexBuffer->Reset();
        primeExpandedVertexBuffer.reset();
    }
    if (visibleVertexCountBuffer)
    {
        visibleVertexCountBuffer->Reset();
        visibleVertexCountBuffer.reset();
    }
    if (crossAdapterVisibleVertexCountBuffer)
    {
        crossAdapterVisibleVertexCountBuffer->Reset();
        crossAdapterVisibleVertexCountBuffer.reset();
    }
    if (primeVisibleVertexCountBuffer)
    {
        primeVisibleVertexCountBuffer->Reset();
        primeVisibleVertexCountBuffer.reset();
    }

    grassBuffer = std::make_shared<GBuffer>(
        secondDevice,
        static_cast<UINT>(sizeof(GrassData)),
        emitterData.GrassCount,
        L"Second Grass Buffer",
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    );

    auto desc = grassBuffer->GetD3D12ResourceDesc();
    crossAdapterGrassBuffer = std::make_shared<GCrossAdapterResource>(
        desc, primeDevice, secondDevice, L"Cross Adapter Grass Buffer");

    expandedVertexBuffer = std::make_shared<GBuffer>(
        secondDevice,
        sizeof(GrassRenderVertex),
        emitterData.GrassCount * kMaxVerticesPerBlade,
        L"Second Expanded Grass Vertex Buffer",
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    desc = expandedVertexBuffer->GetD3D12ResourceDesc();
    crossAdapterExpandedVertexBuffer = std::make_shared<GCrossAdapterResource>(
        desc, primeDevice, secondDevice, L"Cross Adapter Expanded Grass Vertex Buffer");
    primeExpandedVertexBuffer = std::make_shared<GBuffer>(
        primeDevice,
        sizeof(GrassRenderVertex),
        emitterData.GrassCount * kMaxVerticesPerBlade,
        L"Prime Expanded Grass Vertex Buffer",
        D3D12_RESOURCE_FLAG_NONE);
    visibleVertexCountBuffer = std::make_shared<GBuffer>(
        secondDevice,
        sizeof(uint32_t),
        1,
        L"Second Visible Grass Vertex Count",
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    desc = visibleVertexCountBuffer->GetD3D12ResourceDesc();
    crossAdapterVisibleVertexCountBuffer = std::make_shared<GCrossAdapterResource>(
        desc, primeDevice, secondDevice, L"Cross Adapter Visible Grass Vertex Count");
    primeVisibleVertexCountBuffer = std::make_shared<GBuffer>(
        primeDevice,
        sizeof(uint32_t),
        1,
        L"Prime Visible Grass Vertex Count",
        D3D12_RESOURCE_FLAG_NONE);

    grassDataCPU.resize(emitterData.GrassCount);
}

void CrossAdapterGrassEmitter::DescriptorInitialize()
{
    computeDescriptors = secondDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
    expandDescriptors = secondDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = emitterData.GrassCount;
    uavDesc.Buffer.StructureByteStride = sizeof(GrassData);
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    grassBuffer->CreateUnorderedAccessView(&uavDesc, &computeDescriptors, 0);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = emitterData.GrassCount;
    srvDesc.Buffer.StructureByteStride = sizeof(GrassData);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    grassBuffer->CreateShaderResourceView(&srvDesc, &expandDescriptors, 0);

    EnsureExpandWindVelocitySnapshot();

    D3D12_UNORDERED_ACCESS_VIEW_DESC expandedUavDesc = {};
    expandedUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    expandedUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    expandedUavDesc.Buffer.FirstElement = 0;
    expandedUavDesc.Buffer.NumElements = emitterData.GrassCount * kMaxVerticesPerBlade;
    expandedUavDesc.Buffer.StructureByteStride = sizeof(GrassRenderVertex);
    expandedUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    expandedVertexBuffer->CreateUnorderedAccessView(&expandedUavDesc, &expandDescriptors, 2);

    D3D12_UNORDERED_ACCESS_VIEW_DESC counterUavDesc = {};
    counterUavDesc.Format = DXGI_FORMAT_UNKNOWN;
    counterUavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    counterUavDesc.Buffer.FirstElement = 0;
    counterUavDesc.Buffer.NumElements = 1;
    counterUavDesc.Buffer.StructureByteStride = sizeof(uint32_t);
    counterUavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    visibleVertexCountBuffer->CreateUnorderedAccessView(&counterUavDesc, &expandDescriptors, 3);
}

void CrossAdapterGrassEmitter::DescriptorInitializeExpandedDraw()
{
    if (!crossAdapterExpandedVertexBuffer || !crossAdapterVisibleVertexCountBuffer)
    {
        return;
    }

    expandedDrawDescriptors = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 3);

    if (GTexture* grassTex = primeGrassEmitter->GetGrassAtlasTexture())
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC texSrvDesc = {};
        texSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        texSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        texSrvDesc.Texture2D.MostDetailedMip = 0;
        texSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        const auto texDesc = grassTex->GetD3D12ResourceDesc();
        texSrvDesc.Format = texDesc.Format;
        texSrvDesc.Texture2D.MipLevels = texDesc.MipLevels;
        texSrvDesc.Texture2D.PlaneSlice = 0;
        grassTex->CreateShaderResourceView(&texSrvDesc, &expandedDrawDescriptors, 0);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC vertSrv = {};
    vertSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    vertSrv.Format = DXGI_FORMAT_UNKNOWN;
    vertSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    vertSrv.Buffer.FirstElement = 0;
    vertSrv.Buffer.NumElements = emitterData.GrassCount * kMaxVerticesPerBlade;
    vertSrv.Buffer.StructureByteStride = sizeof(GrassRenderVertex);
    vertSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    crossAdapterExpandedVertexBuffer->GetPrimeResource().CreateShaderResourceView(
        &vertSrv, &expandedDrawDescriptors, 1);

    D3D12_SHADER_RESOURCE_VIEW_DESC counterSrv = {};
    counterSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    counterSrv.Format = DXGI_FORMAT_UNKNOWN;
    counterSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    counterSrv.Buffer.FirstElement = 0;
    counterSrv.Buffer.NumElements = 1;
    counterSrv.Buffer.StructureByteStride = sizeof(uint32_t);
    counterSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    crossAdapterVisibleVertexCountBuffer->GetPrimeResource().CreateShaderResourceView(
        &counterSrv, &expandedDrawDescriptors, 2);
}

void CrossAdapterGrassEmitter::EnsureSharedComputeResourcesInitialized()
{
    if (sharedComputeResourcesInitialized_)
        return;

    InitPSO(secondDevice);
    CreateBuffers();
    DescriptorInitialize();
    DescriptorInitializeExpandedDraw();
    GenerateGrassDataCPU();
    sharedComputeResourcesInitialized_ = true;
    needRegenerate = true;
}

void CrossAdapterGrassEmitter::EnsureWindFluidGpuInitialized()
{
    if (windFluid.IsInitialized())
        return;
    if (windFluidInitGiveUp_)
        return;

    windFluid.Initialize(secondDevice, windFluidGridResolution_);
    EnsureExpandWindVelocitySnapshot();
    if (!windFluid.IsInitialized())
        windFluidInitGiveUp_ = true;
}

void CrossAdapterGrassEmitter::EnsureExpandWindVelocitySnapshot()
{
    auto bindVelocitySrv = [this](GResource& resource)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC velSrv{};
        velSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        velSrv.Format = DXGI_FORMAT_R16G16_FLOAT;
        velSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        velSrv.Texture2D.MostDetailedMip = 0;
        velSrv.Texture2D.MipLevels = 1;
        resource.CreateShaderResourceView(&velSrv, &expandDescriptors, 1);
    };

    if (windFluid.IsInitialized())
    {
        const uint32_t grid = windFluid.GetGridResolution();
        if (expandWindVelSnapshot_ && expandWindVelGrid_ == grid)
            return;

        expandWindVelSnapshot_.reset();
        expandWindVelGrid_ = grid;

        const CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
            DXGI_FORMAT_R16G16_FLOAT,
            static_cast<UINT64>(grid),
            grid,
            1u,
            1u,
            1u,
            0u,
            D3D12_RESOURCE_FLAG_NONE);

        expandWindVelSnapshot_ = std::make_unique<GResource>(
            secondDevice,
            desc,
            L"Expand Wind Velocity Snapshot",
            nullptr,
            D3D12_RESOURCE_STATE_COMMON);

        bindVelocitySrv(*expandWindVelSnapshot_);
        return;
    }

    if (expandWindVelFallback_)
        return;

    const CD3DX12_RESOURCE_DESC fallbackDesc = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R16G16_FLOAT,
        1u,
        1u,
        1u,
        1u,
        1u,
        0u,
        D3D12_RESOURCE_FLAG_NONE);

    expandWindVelFallback_ = std::make_unique<GResource>(
        secondDevice,
        fallbackDesc,
        L"Expand Wind Velocity Fallback",
        nullptr,
        D3D12_RESOURCE_STATE_COMMON);

    bindVelocitySrv(*expandWindVelFallback_);
}

void CrossAdapterGrassEmitter::GenerateGrassDataCPU()
{
    float cellSize = emitterData.WorldSize / static_cast<float>(emitterData.GridSize);
    float halfWorld = emitterData.WorldSize * 0.5f;

    for (uint32_t i = 0; i < emitterData.GrassCount; ++i)
    {
        uint32_t x = i % emitterData.GridSize;
        uint32_t z = i / emitterData.GridSize;

        if (z >= emitterData.GridSize) break;

        float posX = (static_cast<float>(x) + 0.5f) * cellSize - halfWorld;
        float posZ = (static_cast<float>(z) + 0.5f) * cellSize - halfWorld;

        posX += MathHelper::RandF(-cellSize * 0.4f, cellSize * 0.4f);
        posZ += MathHelper::RandF(-cellSize * 0.4f, cellSize * 0.4f);

        GrassData& grass = grassDataCPU[i];
        grass.Position = Vector3(posX, 0.0f, posZ);
        grass.Scale = MathHelper::RandF(0.8f, 1.5f);
        grass.Rotation = MathHelper::RandF(0.0f, DirectX::XM_2PI);
        grass.WindOffset = MathHelper::RandF(0.0f, DirectX::XM_2PI);
        grass.TextureIndex = 0;
        grass.Padding[0] = grass.Padding[1] = grass.Padding[2] = 0;
    }
}

void CrossAdapterGrassEmitter::Update()
{
    // Time is advanced via AdvanceTime() from the app (uses real delta).

    // ������������� gameObject ��� ��������� ��������
    if (primeGrassEmitter && gameObject)
    {
        primeGrassEmitter->gameObject = gameObject;
        cullData.World = gameObject->GetTransform()->GetWorldMatrix().Transpose();
        UpdateWindFieldWorldParams(
            emitterData,
            cullData,
            static_cast<float>(std::max(windFluid.GetGridResolution(), 1u)));
    }

    // ��������� ��������� ��������� ��������
    GrassEmitterData primeEmitterDataCopy = emitterData;
    if (!useSharedCompute)
    {
        primeEmitterDataCopy.WindFluidEnable = 0.f;
    }
    primeGrassEmitter->UpdateConstants(primeEmitterDataCopy);
    primeGrassEmitter->Update();
    
    if (needRegenerate && !useSharedCompute)
    {
        GenerateGrassDataCPU();

        auto queue = primeDevice->GetCommandQueue(GQueueType::Compute);
        auto cmdList = queue->GetCommandList();

        primeGrassEmitter->GetGrassBuffer()->LoadData(grassDataCPU.data(), cmdList);

        cmdList->TransitionBarrier(primeGrassEmitter->GetGrassBuffer()->GetD3D12Resource(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->FlushResourceBarriers();

        queue->ExecuteCommandList(cmdList);
        queue->Flush();

        needRegenerate = false;
    }
}

void CrossAdapterGrassEmitter::Draw(const std::shared_ptr<GCommandList>& cmdList)
{
    if (useSharedCompute && sharedComputeResourcesInitialized_)
    {
        auto& expandedOnPrime = crossAdapterExpandedVertexBuffer->GetPrimeResource();
        auto& counterOnPrime = crossAdapterVisibleVertexCountBuffer->GetPrimeResource();

        cmdList->TransitionBarrier(
            expandedOnPrime.GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(
            counterOnPrime.GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->FlushResourceBarriers();

        const auto worldCB = primeGrassEmitter->GetWorldConstantsBuffer();
        const auto objectCB = primeGrassEmitter->GetObjectPositionBuffer();
        if (worldCB && objectCB && !expandedDrawDescriptors.IsNull())
        {
            cmdList->SetPipelineState(*expandedDrawPSO.get());
            cmdList->SetRootSignature(*drawRS);
            cmdList->SetDescriptorsHeap(&expandedDrawDescriptors);
            cmdList->SetRootConstantBufferView(0, *objectCB);
            cmdList->SetRootConstantBufferView(1, *worldCB);
            cmdList->SetRootDescriptorTable(2, &expandedDrawDescriptors, 1);
            cmdList->SetRootDescriptorTable(3, &expandedDrawDescriptors, 0);
            cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            cmdList->Draw(emitterData.GrassCount * kMaxVerticesPerBlade, 1, 0, 0);
        }
    }
    else
    {
        primeGrassEmitter->Draw(cmdList);
    }
}

void CrossAdapterGrassEmitter::Dispatch(const std::shared_ptr<GCommandList>& cmdList)
{
    if (useSharedCompute)
        EnsureSharedComputeResourcesInitialized();

    if (dirtyActivated == Enable)
    {
        if (needRegenerate && sharedComputeResourcesInitialized_ && crossAdapterGrassBuffer && grassBuffer)
        {
            cmdList->CopyResource(grassBuffer->GetD3D12Resource(),
                crossAdapterGrassBuffer->GetSharedResource().GetD3D12Resource());
        }
        dirtyActivated = None;
    }

    if (dirtyActivated == Disable)
    {
        // Single-GPU path regenerates/uploads grass on prime in Update().
        // Do not overwrite it with cross-adapter shadow resources here.
        dirtyActivated = None;
    }

    if (useSharedCompute)
    {
        using PEPEngine::Graphics::WindFluidGpuCB;

        EnsureWindFluidGpuInitialized();

        const UINT grassEmitterCbBytes =
            d3dUtil::CalcConstantBufferByteSize(static_cast<UINT>(sizeof(GrassEmitterData)));
        const UINT grassCullCbBytes =
            d3dUtil::CalcConstantBufferByteSize(static_cast<UINT>(sizeof(GrassCullData)));
        if (!sharedGrassEmitterCb_)
        {
            sharedGrassEmitterCb_ =
                std::make_shared<GBuffer>(secondDevice, grassEmitterCbBytes, 1, L"Grass Emitter Constant");
            sharedGrassCullCb_ =
                std::make_shared<GBuffer>(secondDevice, grassCullCbBytes, 1, L"Grass Cull Constant");
        }
        const auto& constantBuffer = sharedGrassEmitterCb_;
        const auto& cullBuffer = sharedGrassCullCb_;

        const bool gpuFluidLive =
            windFluid.IsInitialized() && (emitterData.WindFluidEnable >= 0.5f);

        if (gameObject)
        {
            cullData.World = gameObject->GetTransform()->GetWorldMatrix().Transpose();
        }
        UpdateWindFieldWorldParams(
            emitterData,
            cullData,
            static_cast<float>(std::max(windFluid.GetGridResolution(), 1u)));

        if (!lod0DebugGradientEnable_)
        {
            emitterData.WindFluidObstacleA =
                Vector4(windFluidWallA_.x, windFluidWallA_.y, windFluidWallA_.z, 0.0f);
            emitterData.WindFluidObstacleB =
                Vector4(windFluidWallB_.x, grassObstacleWakeLean_, windFluidWallB_.z, 0.0f);
        }
        else
        {
            ApplyLod0DebugGradientToEmitterData();
        }

        WindFluidGpuCB wf{};
        wf.WindOriginCount = emitterData.WindOriginCount;
        wf.InjectStrength = gpuFluidLive ? windFluidInjectStrength_ : 0.0f;
        wf.VorticityEps = gpuFluidLive ? windFluidVorticityEps_ : 0.0f;
        wf.Dissipation = gpuFluidLive ? windFluidDissipation_ : 0.985f;
        wf.Dt = windFluidDt_;
        wf.WindMapFalloff = emitterData.WindMapFalloff;
        wf.JacobiIterations = windFluidJacobiIterations_;
        wf.FieldCenterHalf =
            Vector4(emitterData.WindFieldWorldParams.x, emitterData.WindFieldWorldParams.y,
                    emitterData.WindFieldWorldParams.z, emitterData.WindFieldWorldParams.z);
        wf.CellWorldSize =
            (emitterData.WindFieldWorldParams.z > 1e-5f ? (emitterData.WindFieldWorldParams.z * 2.f /
                                                            static_cast<float>(windFluid.GetGridResolution()))
                                                         : emitterData.WorldSize / static_cast<float>(windFluid.GetGridResolution()));
        wf.ObstacleWallA = windFluidWallA_;
        wf.ObstacleWallB = windFluidWallB_;
        for (uint32_t i = 0; i < GrassEmitterData::MaxWindOrigins; ++i)
        {
            wf.WindOriginData[i] = emitterData.WindOriginData[i];
            wf.WindDirectionData[i] = emitterData.WindDirectionData[i];
        }
        wf.ClickImpulseU = windFluidClickU_;
        wf.ClickImpulseV = windFluidClickV_;
        wf.ClickImpulseStrength = windFluidClickStrength_;
        wf.ClickImpulseRadiusSq = windFluidClickRadiusSq_;

        windFluid.Simulate(cmdList, wf);
        EnsureExpandWindVelocitySnapshot();

        if (gpuFluidLive)
        {
            // Bind post-sim readable velocity directly for expand (avoids stale snapshot SRV).
            windFluid.PublishReadableSrvTo(&expandDescriptors, 1);

            if (expandWindVelSnapshot_)
            {
                if (const auto readableVel = windFluid.GetVelocityResource())
                {
                    cmdList->TransitionBarrier(readableVel.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
                    cmdList->TransitionBarrier(expandWindVelSnapshot_->GetD3D12Resource().Get(),
                                               D3D12_RESOURCE_STATE_COPY_DEST);
                    cmdList->FlushResourceBarriers();
                    cmdList->CopyResource(expandWindVelSnapshot_->GetD3D12Resource().Get(),
                                          readableVel.Get());
                    cmdList->TransitionBarrier(expandWindVelSnapshot_->GetD3D12Resource().Get(),
                                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                    cmdList->TransitionBarrier(readableVel.Get(),
                                               D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                }
            }
        }

        GrassEmitterData grassCb = emitterData;
        if (!gpuFluidLive)
        {
            grassCb.WindFluidEnable = 0.f;
        }

        LoadAlignedConstantBuffer(constantBuffer, grassCb, cmdList);
        LoadAlignedConstantBuffer(cullBuffer, cullData, cmdList);

        uint32_t threadGroups = (emitterData.GrassCount + 63) / 64;

        if (needRegenerate)
        {
            cmdList->TransitionBarrier(grassBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            cmdList->FlushResourceBarriers();

            cmdList->SetPipelineState(*generatePSO.get());
            cmdList->SetRootSignature(*computeRS);

            cmdList->SetDescriptorsHeap(&computeDescriptors);

            cmdList->SetRootConstantBufferView(0, *constantBuffer);
            cmdList->SetRootDescriptorTable(1, &computeDescriptors, 0);
            cmdList->Dispatch(threadGroups, 1, 1);

            cmdList->UAVBarrier(grassBuffer->GetD3D12Resource());
            cmdList->FlushResourceBarriers();
        }

        uint32_t zeroCounter = 0;
        visibleVertexCountBuffer->LoadData(&zeroCounter, cmdList);

        // Expand reads grass + velocity as SRV and writes expanded vertices as UAV.
        cmdList->TransitionBarrier(grassBuffer->GetD3D12Resource(),
                                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        cmdList->TransitionBarrier(expandedVertexBuffer->GetD3D12Resource(),
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(visibleVertexCountBuffer->GetD3D12Resource(),
                                   D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->FlushResourceBarriers();

        cmdList->SetPipelineState(*expandPSO.get());
        cmdList->SetRootSignature(*expandRS);
        cmdList->SetDescriptorsHeap(&expandDescriptors);
        cmdList->SetRootConstantBufferView(0, *constantBuffer);
        cmdList->SetRootConstantBufferView(1, *cullBuffer);
        cmdList->SetRootDescriptorTable(2, &expandDescriptors, 0);
        cmdList->SetRootDescriptorTable(3, &expandDescriptors, 2);
        cmdList->SetRootDescriptorTable(4, &expandDescriptors, 3);
        cmdList->Dispatch(threadGroups, 1, 1);

        cmdList->UAVBarrier(expandedVertexBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();

        cmdList->CopyResource(crossAdapterExpandedVertexBuffer->GetSharedResource().GetD3D12Resource(),
            expandedVertexBuffer->GetD3D12Resource());
        cmdList->CopyResource(crossAdapterVisibleVertexCountBuffer->GetSharedResource().GetD3D12Resource(),
            visibleVertexCountBuffer->GetD3D12Resource());
        cmdList->TransitionBarrier(crossAdapterExpandedVertexBuffer->GetSharedResource().GetD3D12Resource(),
                                   D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(crossAdapterVisibleVertexCountBuffer->GetSharedResource().GetD3D12Resource(),
                                   D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();

        needRegenerate = false;
    }
}

void CrossAdapterGrassEmitter::SetWindStrength(float strength)
{
    emitterData.WindStrength = strength;
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetWindStrength(strength);
    }
}

void CrossAdapterGrassEmitter::SetWindIntensity(float intensity)
{
    emitterData.WindIntensity = std::max(0.0f, intensity);
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetWindIntensity(emitterData.WindIntensity);
    }
}

void CrossAdapterGrassEmitter::SetWindAmplitude(float amplitude)
{
    emitterData.WindAmplitude = std::max(0.0f, amplitude);
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetWindAmplitude(emitterData.WindAmplitude);
    }
}

void CrossAdapterGrassEmitter::SetGpuWindFluid(float enable, float blend, uint32_t jacobiIterations)
{
    const float prevEnable = emitterData.WindFluidEnable;
    emitterData.WindFluidEnable = enable;
    emitterData.WindFluidBlend = blend;
    windFluidJacobiIterations_ = std::clamp(jacobiIterations, 2u, 40u);
    // Let the user re-trigger EnsureWindFluidGpuInitialized once when turning fluid on again.
    if (prevEnable < 0.5f && enable >= 0.5f)
        windFluidInitGiveUp_ = false;
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetGpuWindFluid(enable, blend);
    }
}

void CrossAdapterGrassEmitter::SetWindFluidSimulationTuning(float injectStrength, float dissipation,
                                                            float dt, float vorticityEps,
                                                            uint32_t gridResolution)
{
    windFluidInjectStrength_ = std::clamp(injectStrength, 0.0f, 2.0f);
    windFluidDissipation_ = std::clamp(dissipation, 0.80f, 0.9999f);
    windFluidDt_ = std::clamp(dt, 0.001f, 0.05f);
    windFluidVorticityEps_ = std::clamp(vorticityEps, 0.0f, 2.0f);

    const uint32_t newGrid = std::clamp(gridResolution, 32u, 512u);
    if (newGrid != windFluidGridResolution_)
    {
        windFluidGridResolution_ = newGrid;
        windFluidInitGiveUp_ = false;
        expandWindVelGrid_ = 0;
        expandWindVelSnapshot_.reset();
        if (secondDevice)
            secondDevice->Flush();
        windFluid.Initialize(secondDevice, windFluidGridResolution_);
        if (windFluid.IsInitialized())
            EnsureExpandWindVelocitySnapshot();
    }
}

void CrossAdapterGrassEmitter::SetWindFluidWall(bool enabled, float posU, float posV,
                                                float /*angleRad*/, float radiusNorm,
                                                float /*halfWidthNorm*/, float drag,
                                                float wakeStrength)
{
    windFluidWallA_.x = enabled ? 1.0f : 0.0f;
    windFluidWallA_.y = std::clamp(posU, 0.0f, 1.0f);
    windFluidWallA_.z = std::clamp(posV, 0.0f, 1.0f);
    windFluidWallA_.w = 0.0f;

    // Shadertoy circle: B.x stores radius in UV space (BarrierRadius ~ 0.1).
    windFluidWallB_.x = std::clamp(radiusNorm, 0.01f, 0.45f);
    windFluidWallB_.y = 0.0f;
    windFluidWallB_.z = std::clamp(drag, 0.0f, 2.0f);
    grassObstacleWakeLean_ = std::max(0.0f, wakeStrength);
    windFluidWallB_.w = std::clamp(wakeStrength * (2.0f / 200.0f), 0.0f, 2.0f);

    emitterData.WindFluidObstacleA =
        Vector4(windFluidWallA_.x, windFluidWallA_.y, windFluidWallA_.z, 0.0f);
    emitterData.WindFluidObstacleB =
        Vector4(windFluidWallB_.x, grassObstacleWakeLean_, windFluidWallB_.z, 0.0f);

    if (lod0DebugGradientEnable_)
        ApplyLod0DebugGradientToEmitterData();
}

void CrossAdapterGrassEmitter::ApplyLod0DebugGradientToEmitterData()
{
    emitterData.WindFluidObstacleA = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    emitterData.WindFluidObstacleB =
        Vector4(lod0DebugGradientMin_, lod0DebugGradientMax_, lod0DebugGradientAxis_, 1.0f);
}

void CrossAdapterGrassEmitter::SetLod0DebugGradientWind(
    const bool enabled, const float minWorldSpeed, const float maxWorldSpeed, const float axis01)
{
    lod0DebugGradientEnable_ = enabled;
    lod0DebugGradientMin_ = std::max(0.0f, minWorldSpeed);
    lod0DebugGradientMax_ = std::max(lod0DebugGradientMin_, maxWorldSpeed);
    lod0DebugGradientAxis_ = (axis01 >= 0.5f) ? 1.0f : 0.0f;

    if (lod0DebugGradientEnable_)
    {
        ApplyLod0DebugGradientToEmitterData();
    }
    else
    {
        emitterData.WindFluidObstacleA =
            Vector4(windFluidWallA_.x, windFluidWallA_.y, windFluidWallA_.z, 0.0f);
        emitterData.WindFluidObstacleB =
            Vector4(windFluidWallB_.x, grassObstacleWakeLean_, windFluidWallB_.z, 0.0f);
    }
}

void CrossAdapterGrassEmitter::SetLodBladeSize(float lod0WidthScale, float lod0HeightScale,
                                               float lod1WidthScale, float lod1HeightScale)
{
    emitterData.Lod0BladeWidthScale = std::max(0.05f, lod0WidthScale);
    emitterData.Lod0BladeHeightScale = std::max(0.05f, lod0HeightScale);
    emitterData.Lod1BladeWidthScale = std::max(0.05f, lod1WidthScale);
    emitterData.Lod1BladeHeightScale = std::max(0.05f, lod1HeightScale);
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetLodBladeSize(
            emitterData.Lod0BladeWidthScale, emitterData.Lod0BladeHeightScale,
            emitterData.Lod1BladeWidthScale, emitterData.Lod1BladeHeightScale);
    }
}

void CrossAdapterGrassEmitter::SetLod0Sdof(float naturalFreq, float dampingRatio)
{
    emitterData.Lod0SdofNaturalFreq = std::max(0.05f, naturalFreq);
    emitterData.Lod0SdofDampingRatio = std::max(0.01f, dampingRatio);
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetLod0Sdof(emitterData.Lod0SdofNaturalFreq, emitterData.Lod0SdofDampingRatio);
    }
}

void CrossAdapterGrassEmitter::SetWindGradient(uint32_t originCount, float falloff,
                                               const Vector4* originData, const Vector4* directionData)
{
    emitterData.WindOriginCount = std::min(originCount, GrassEmitterData::MaxWindOrigins);
    emitterData.WindMapFalloff = std::max(0.1f, falloff);
    for (uint32_t i = 0; i < GrassEmitterData::MaxWindOrigins; ++i)
    {
        if (originData)
        {
            emitterData.WindOriginData[i] = originData[i];
            emitterData.WindOriginData[i].w = std::max(1.0f, emitterData.WindOriginData[i].w);
        }
        if (directionData)
        {
            Vector2 dir(directionData[i].x, directionData[i].y);
            const float strengthPacked = std::max(0.0f, directionData[i].w);
            const bool radialOnly = dir.LengthSquared() < 1e-12f;
            if (radialOnly)
            {
                dir = Vector2::Zero;
            }
            else if (dir.LengthSquared() < 1e-6f)
            {
                dir = Vector2(1.0f, 0.0f);
            }
            else
            {
                dir.Normalize();
            }
            emitterData.WindDirectionData[i] = Vector4(
                dir.x, dir.y, directionData[i].z, strengthPacked);
        }
    }
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetWindGradient(emitterData.WindOriginCount, emitterData.WindMapFalloff,
                                           emitterData.WindOriginData, emitterData.WindDirectionData);
    }
}

void CrossAdapterGrassEmitter::SetFieldInfluenceScale(float scale)
{
    emitterData.FieldInfluenceScale = std::max(0.0f, scale);
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetFieldInfluenceScale(emitterData.FieldInfluenceScale);
    }
}

void CrossAdapterGrassEmitter::SetLod0LeanGain(float gain)
{
    emitterData.Lod0LeanGain = std::max(0.1f, gain);
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetLod0LeanGain(emitterData.Lod0LeanGain);
    }
}

void CrossAdapterGrassEmitter::SetDebugNearestOriginTint(bool enabled)
{
    emitterData.DebugNearestOriginTint = enabled ? 1.0f : 0.0f;
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetDebugNearestOriginTint(enabled);
    }
}

Microsoft::WRL::ComPtr<ID3D12Resource> CrossAdapterGrassEmitter::GetExpandWindVelocityResource() const
{
    if (!expandWindVelSnapshot_ || !expandWindVelSnapshot_->IsValid())
        return nullptr;
    return expandWindVelSnapshot_->GetD3D12Resource();
}

void CrossAdapterGrassEmitter::SetWindDirection(const Vector2& direction)
{
    Vector2 d = direction;
    if (d.LengthSquared() < 1e-6f)
    {
        d = Vector2(1.0f, 0.0f);
    }
    else
    {
        d.Normalize();
    }
    emitterData.WindDirection = d;
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetWindDirection(d);
    }
}

void CrossAdapterGrassEmitter::AdvanceTime(float deltaSeconds)
{
    emitterData.Time += std::max(0.0f, deltaSeconds);
}

void CrossAdapterGrassEmitter::SetClickWindBoost(float boost)
{
    emitterData.WindFluidPad0 = std::max(0.0f, boost);
}

void CrossAdapterGrassEmitter::SetWindFluidClickImpulse(float u, float v, float strength, float radiusSq)
{
    windFluidClickU_ = std::clamp(u, 0.0f, 1.0f);
    windFluidClickV_ = std::clamp(v, 0.0f, 1.0f);
    windFluidClickStrength_ = std::max(0.0f, strength);
    windFluidClickRadiusSq_ = std::max(0.0f, radiusSq);
}

void CrossAdapterGrassEmitter::SetWorldSize(float size)
{
    emitterData.WorldSize = size;
    needRegenerate = true;
}

void CrossAdapterGrassEmitter::SetGrassCount(uint32_t count)
{
    if (count == 0)
    {
        count = 1;
    }
    if (emitterData.GrassCount == count)
    {
        return;
    }

    emitterData.GrassCount = count;
    emitterData.GridSize = static_cast<uint32_t>(std::sqrt(static_cast<float>(count)));
    primeGrassEmitter->SetGrassCount(count);
    windFluidInitGiveUp_ = false;
    if (useSharedCompute)
    {
        EnsureSharedComputeResourcesInitialized();
        CreateBuffers();
        DescriptorInitialize();
        DescriptorInitializeExpandedDraw();
        GenerateGrassDataCPU();
    }
    needRegenerate = true;
}

void CrossAdapterGrassEmitter::SetLodBladeCounts(uint32_t lod0BladeCount, uint32_t lod1BladeCount)
{
    emitterData.Lod0BladeCount = std::max(1u, std::min(lod0BladeCount, kMaxBladeCount));
    emitterData.Lod1BladeCount = std::max(1u, std::min(lod1BladeCount, kMaxBladeCount));
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetLodBladeCounts(emitterData.Lod0BladeCount, emitterData.Lod1BladeCount);
    }
}

void CrossAdapterGrassEmitter::Regenerate()
{
    needRegenerate = true;
}

void CrossAdapterGrassEmitter::EnableShared()
{
    useSharedCompute = true;
    dirtyActivated = Enable;
    needRegenerate = true;
    windFluidInitGiveUp_ = false;
}

void CrossAdapterGrassEmitter::DisableShared()
{
    useSharedCompute = false;
    dirtyActivated = Disable;
    // Prime grass buffer was only filled on GPU2 when shared; force CPU upload next Update.
    needRegenerate = true;
}

void CrossAdapterGrassEmitter::SetWorldConstantsBuffer(const GBuffer* worldConstants)
{
    if (primeGrassEmitter)
    {
        primeGrassEmitter->SetWorldConstantsBuffer(worldConstants);
    }
}

void CrossAdapterGrassEmitter::SetFrustumCullingData(const Matrix& viewProj, const Vector3& eyePos, const float maxDistance,
                                                     const float lod0Distance, const float lod1Distance,
                                                     const uint32_t lod0BaseSegments,
                                                     const float windTessellationScale)
{
    cullData.ViewProj = viewProj;
    cullData.EyePos = eyePos;
    cullData.MaxDistance = maxDistance;
    cullData.Lod0Distance = lod0Distance;
    cullData.Lod1Distance = std::max(lod0Distance, lod1Distance);
    cullData.Lod0BaseSegments = std::min(lod0BaseSegments, kMaxLod0Segments);
    cullData.WindTessellationScale = windTessellationScale;
    emitterData.Lod0DistanceThreshold = lod0Distance;
    emitterData.Lod1DistanceThreshold = cullData.Lod1Distance;
}
