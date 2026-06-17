#include "pch.h"
#include "GrassEmitter.h"
#include "GameObject.h"
#include "MathHelper.h"
#include "Transform.h"
#include "GCommandList.h"
#include <algorithm>

GrassEmitter::GrassEmitter(std::shared_ptr<GDevice> dev, uint32_t grassCount, float worldSize,
                           uint32_t lod0BladeCount, uint32_t lod1BladeCount)
    : device(dev)
{
    // ������ ������ ����
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
    emitterData.Lod0BladeCount = std::max(1u, lod0BladeCount);
    emitterData.Lod1BladeCount = std::max(1u, lod1BladeCount);
    emitterData.WindDirection = Vector2(1.0f, 0.0f);
    emitterData.AtlasTextureCount = Atlas.size();
    emitterData.Lod0LeanGain = 3.0f;

    Initialize();
}

GrassEmitter::~GrassEmitter()
{
}

void GrassEmitter::Initialize()
{
    // ��������� �������� ����� (��� � ParticleEmitter)
    {
        auto queue = device->GetCommandQueue(GQueueType::Compute);
        auto cmdList = queue->GetCommandList();

        // ��������� ��������� ������� �����
        Atlas.push_back(GTexture::LoadTextureFromFile(L"Data\\Textures\\grassBlades.dds", cmdList, TextureUsage::Albedo));


        queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));

        emitterData.AtlasTextureCount = Atlas.size();
    }

    // ���������� mipmaps ��� �������
    {
        auto queue = device->GetCommandQueue(GQueueType::Compute);
        auto cmdList = queue->GetCommandList();

        std::vector<GTexture*> textures;
        for (auto texture : Atlas)
        {
            textures.push_back(texture.get());
        }

        GTexture::GenerateMipMaps(cmdList, textures.data(), textures.size());

        for (auto texture : Atlas)
        {
            cmdList->TransitionBarrier(texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
        }
        cmdList->FlushResourceBarriers();

        queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
    }


    // ������� �������� ���������
    CreateRootSignatures();

    // ������� PSO
    CreatePipelineState();

    // ������� compute ������ (�����������)
    CreateComputeShaders();

    // ������� ������
    CreateBuffers();

    // �������������� �����������
    DescriptorInitialize();

    // ���������� ������
    Regenerate();
}

void GrassEmitter::CreateRootSignatures()
{
    // ��� ������� - ������ ���� SRV ��� ��������
    CD3DX12_DESCRIPTOR_RANGE range[2];
    range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // Grass buffer (t0)
    range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // Grass texture (t1) - ���� ��������
    
    renderSignature = std::make_shared<GRootSignature>();
    renderSignature->AddConstantBufferParameter(0); // b0 - ObjectConstants (������� �������)
    renderSignature->AddConstantBufferParameter(1); // b1 - WorldConstants (View, Proj) - �� ��������� �������
    renderSignature->AddConstantBufferParameter(2); // b2 - GrassEmitterData
    renderSignature->AddDescriptorParameter(&range[0], 1); // Grass buffer SRV
    renderSignature->AddDescriptorParameter(&range[1], 1); // ���� �������� SRV
    
    // ���� �������
    CD3DX12_STATIC_SAMPLER_DESC sampler(
        0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    renderSignature->AddStaticSampler(sampler);
    
    renderSignature->Initialize(device);
    
    // ��� compute (���� ������������)
    if (useGPUGeneration)
    {
        CD3DX12_DESCRIPTOR_RANGE uavRange;
        uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // Grass buffer UAV
        
        computeSignature = std::make_shared<GRootSignature>();
        computeSignature->AddConstantBufferParameter(0); // b0 - GrassEmitterData
        computeSignature->AddDescriptorParameter(&uavRange, 1); // u0 - Grass buffer
        computeSignature->Initialize(device, false, D3D12_ROOT_SIGNATURE_FLAG_NONE);
    }
}

void GrassEmitter::CreatePipelineState()
{
    auto vertexShader = std::make_shared<GShader>(L"Shaders\\GrassDraw.hlsl", VertexShader, nullptr, "VS", "vs_5_1");
    vertexShader->LoadAndCompile();

    auto pixelShader = std::make_shared<GShader>(L"Shaders\\GrassDraw.hlsl", PixelShader, nullptr, "PS", "ps_5_1");
    pixelShader->LoadAndCompile();

    auto geometryShader = std::make_shared<GShader>(L"Shaders\\GrassDraw.hlsl", GeometryShader, nullptr, "GS", "gs_5_1");
    geometryShader->LoadAndCompile();

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { nullptr, 0 };
    psoDesc.pRootSignature = renderSignature->GetNativeSignature().Get();
    psoDesc.VS = vertexShader->GetShaderResource();
    psoDesc.PS = pixelShader->GetShaderResource();
    psoDesc.GS = geometryShader->GetShaderResource();
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // ����� �������������
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

    // �����-��������
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
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = GetSRGBFormat(BackBufferFormat);
    psoDesc.DSVFormat = DepthStencilFormat;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    renderPSO = std::make_shared<GraphicPSO>(RenderMode::Transparent);
    renderPSO->SetPsoDesc(psoDesc);
    renderPSO->Initialize(device);
}

void GrassEmitter::CreateComputeShaders()
{
    if (useGPUGeneration)
    {
        D3D_SHADER_MACRO defines[] =
        {
            "GENERATE_GRASS", "1",
            nullptr, nullptr
        };

        generateShader = std::make_shared<GShader>(L"Shaders\\ComputeGrass.hlsl", ComputeShader, defines, "CS", "cs_5_1");
        generateShader->LoadAndCompile();

        generatePSO = std::make_shared<ComputePSO>();
        generatePSO->SetRootSignature(*computeSignature.get());
        generatePSO->SetShader(generateShader.get());
        generatePSO->Initialize(device);
    }
}

void GrassEmitter::CreateBuffers()
{
    // ����� ��� ObjectConstants (��� � ParticleEmitter)
    objectPositionBuffer = std::make_shared<ConstantUploadBuffer<ObjectConstants>>(device, 1, L"Grass Object Position");

    // ����� ��� ������ �����
    grassBuffer = std::make_shared<GBuffer>(
        device,
        sizeof(GrassData),
        emitterData.GrassCount,
        L"Grass Buffer",
        useGPUGeneration ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE
    );

    // ����������� ����� ��� GrassEmitterData
    constantBuffer = std::make_shared<GBuffer>(
        device,
        sizeof(GrassEmitterData),
        1,
        L"Grass Emitter Constant Buffer"
    );

    // CPU ����� ��� ���������
    grassDataCPU.resize(emitterData.GrassCount);
}

void GrassEmitter::DescriptorInitialize()
{
    // �������� ����������� ��� �������: grass buffer SRV + ��������
    grassDescriptors = device->AllocateDescriptors(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        1 + Atlas.size() // grass buffer + ��� ��������
    );

    // SRV ��� ������ �����
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = emitterData.GrassCount;
    srvDesc.Buffer.StructureByteStride = sizeof(GrassData);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    grassBuffer->CreateShaderResourceView(&srvDesc, &grassDescriptors, 0);

    // SRV ��� ������� (��� � ParticleEmitter)
    D3D12_SHADER_RESOURCE_VIEW_DESC texSrvDesc = {};
    texSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    texSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    texSrvDesc.Texture2D.MostDetailedMip = 0;
    texSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    for (size_t i = 0; i < Atlas.size(); ++i)
    {
        auto texture = Atlas[i];
        auto desc = texture->GetD3D12ResourceDesc();

        texSrvDesc.Format = desc.Format;
        texSrvDesc.Texture2D.MipLevels = desc.MipLevels;
        texSrvDesc.Texture2D.PlaneSlice = 0;

        texture->CreateShaderResourceView(&texSrvDesc, &grassDescriptors, 1 + i);
    }

    // ���� ���������� GPU ���������, �������� ����������� ��� compute
    if (useGPUGeneration)
    {
        computeDescriptors = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = emitterData.GrassCount;
        uavDesc.Buffer.StructureByteStride = sizeof(GrassData);
        uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        uavDesc.Buffer.CounterOffsetInBytes = 0;

        grassBuffer->CreateUnorderedAccessView(&uavDesc, &computeDescriptors, 0);
    }
}

void GrassEmitter::GenerateGrassDataCPU()
{
    // ����������� ������������� �� �����
    float cellSize = emitterData.WorldSize / static_cast<float>(emitterData.GridSize);
    float halfWorld = emitterData.WorldSize * 0.5f;

    for (uint32_t i = 0; i < emitterData.GrassCount; ++i)
    {
        uint32_t x = i % emitterData.GridSize;
        uint32_t z = i / emitterData.GridSize;

        if (z >= emitterData.GridSize) break;

        // ������� ������� � �����
        float posX = (static_cast<float>(x) + 0.5f) * cellSize - halfWorld;
        float posZ = (static_cast<float>(z) + 0.5f) * cellSize - halfWorld;

        // ��������� ��������
        posX += MathHelper::RandF(-cellSize * 0.4f, cellSize * 0.4f);
        posZ += MathHelper::RandF(-cellSize * 0.4f, cellSize * 0.4f);

        GrassData& grass = grassDataCPU[i];
        grass.Position = Vector3(posX, 0.0f, posZ);
        grass.Scale = MathHelper::RandF(0.8f, 1.5f);
        grass.Rotation = MathHelper::RandF(0.0f, DirectX::XM_2PI);
        grass.WindOffset = MathHelper::RandF(0.0f, DirectX::XM_2PI);
        grass.TextureIndex = MathHelper::RandF(0, static_cast<float>(Atlas.size() - 1));
        grass.Padding[0] = grass.Padding[1] = grass.Padding[2] = 0;
    }
}

void GrassEmitter::Regenerate()
{
    needRegenerate = true;
}

void GrassEmitter::SetGrassCount(uint32_t count)
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

    // Recreate size-dependent buffers/views for dynamic blade count changes.
    CreateBuffers();
    DescriptorInitialize();
    needRegenerate = true;
}

void GrassEmitter::SetLodBladeCounts(uint32_t lod0BladeCount, uint32_t lod1BladeCount)
{
    emitterData.Lod0BladeCount = std::max(1u, lod0BladeCount);
    emitterData.Lod1BladeCount = std::max(1u, lod1BladeCount);
}

void GrassEmitter::SetWindDirection(const Vector2& direction)
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
}

void GrassEmitter::SetWindGradient(uint32_t originCount, float falloff,
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
}

void GrassEmitter::Update()
{
    if (!gameObject) return; // ������ �� nullptr
    // ��������� �����
    emitterData.Time += 0.016f; // �������� 60 FPS

    // Always refresh object constants. After dynamic grass-count changes, the
    // upload buffer is recreated and must be repopulated even if transform
    // itself wasn't marked dirty this frame.
    const auto transform = gameObject->GetTransform();
    objectWorldData.TextureTransform = transform->TextureTransform.Transpose();
    objectWorldData.World = (transform->GetWorldMatrix()).Transpose();
    objectPositionBuffer->CopyData(0, objectWorldData);

    // ���� ����� �������������� ������
    if (needRegenerate)
    {
        if (!useGPUGeneration)
        {
            // ��������� �� CPU
            GenerateGrassDataCPU();

            auto queue = device->GetCommandQueue(GQueueType::Compute);
            auto cmdList = queue->GetCommandList();

            grassBuffer->LoadData(grassDataCPU.data(), cmdList);

            cmdList->TransitionBarrier(grassBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            cmdList->FlushResourceBarriers();

            queue->ExecuteCommandList(cmdList);
            queue->Flush();
        }

        needRegenerate = false;
    }

    // ��������� ����������� �����
    auto queue = device->GetCommandQueue(GQueueType::Copy);
    auto cmdList = queue->GetCommandList();
    constantBuffer->LoadData(&emitterData, cmdList);
    queue->ExecuteCommandList(cmdList);
}

void GrassEmitter::Draw(const std::shared_ptr<GCommandList>& cmdList)
{
    cmdList->TransitionBarrier(grassBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();

    cmdList->SetPipelineState(*renderPSO.get());
    cmdList->SetRootSignature(*renderSignature);

    cmdList->SetDescriptorsHeap(&grassDescriptors);

    // b0 - ObjectConstants (������� �������)
    cmdList->SetRootConstantBufferView(0, *objectPositionBuffer.get());

    // b1 - WorldConstants must be explicitly rebound for this root signature.
    if (worldConstantsBuffer)
    {
        cmdList->SetRootConstantBufferView(1, *worldConstantsBuffer);
    }

    // b2 - GrassEmitterData
    cmdList->SetRootConstantBufferView(2, *constantBuffer);

    // t0 - Grass buffer
    cmdList->SetRootDescriptorTable(3, &grassDescriptors, 0);

    // t1+ - Textures
    cmdList->SetRootDescriptorTable(4, &grassDescriptors, 1);

    cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    cmdList->Draw(6, emitterData.GrassCount, 0, 0);

    cmdList->TransitionBarrier(grassBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();
}

void GrassEmitter::Dispatch(const std::shared_ptr<GCommandList>& cmdList)
{
    if (useGPUGeneration && needRegenerate)
    {
        cmdList->TransitionBarrier(grassBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->FlushResourceBarriers();

        cmdList->SetPipelineState(*generatePSO.get());
        cmdList->SetRootSignature(*computeSignature);

        cmdList->SetDescriptorsHeap(&computeDescriptors);
        cmdList->SetRootConstantBufferView(0, *constantBuffer);
        cmdList->SetRootDescriptorTable(1, &computeDescriptors, 0);

        // ��������� compute ������
        uint32_t threadGroups = (emitterData.GrassCount + 1023) / 1024;
        cmdList->Dispatch(threadGroups, 1, 1);

        cmdList->UAVBarrier(grassBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();

        needRegenerate = false;
    }
}