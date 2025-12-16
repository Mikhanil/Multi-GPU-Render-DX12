#include "pch.h"
#include "SharedFluidParticleEmitter.h"

#include "GameObject.h"
#include "GameTimer.h"
#include "Transform.h"
#include "../MGPU-Particles/HybridParticleApp.h"

void FluidSimulationResources::Initialize(const std::shared_ptr<GDevice>& device, const FluidSimulationData& simData, const ParticleSpawner::SpawnData& spawnData)
{
    Device = device;

    SimData = simData;
    SpawnData = spawnData;
    SimData.numParticles = SpawnData.Points.size();
    
    PSOInitialize();
    
    SpatialHash.Initialize(device, SimData.numParticles);

    InitializeBuffers(SimData, spawnData);
    InitializeDescriptors();
}

void FluidSimulationResources::PSOInitialize()
{

    if (ComputePSOs.empty())
    {
        ComputeSignature.AddConstantBufferParameter(0);
        ComputeSignature.AddConstantParameter(4, 1);
        
        CD3DX12_DESCRIPTOR_RANGE ranges[10];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2);
        ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3);
        ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 4);
        ranges[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 5);
        ranges[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 6);
        ranges[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 7);
        ranges[8].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 8);
        
        ranges[9].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        
        ComputeSignature.AddDescriptorParameter(&ranges[0], 1);
        ComputeSignature.AddDescriptorParameter(&ranges[1], 1);
        ComputeSignature.AddDescriptorParameter(&ranges[2], 1);
        ComputeSignature.AddDescriptorParameter(&ranges[3], 1);
        ComputeSignature.AddDescriptorParameter(&ranges[4], 1);
        ComputeSignature.AddDescriptorParameter(&ranges[5], 1);
        ComputeSignature.AddDescriptorParameter(&ranges[6], 1);
        ComputeSignature.AddDescriptorParameter(&ranges[7], 1);
        ComputeSignature.AddDescriptorParameter(&ranges[8], 1);
        ComputeSignature.AddDescriptorParameter(&ranges[9], 1);

        ComputeSignature.Initialize(Device);

        CompileComputeShaders();

        for (auto kernel : EKernels::All)
        {
            ComputePSOs[kernel] = std::make_shared<ComputePSO>();
            ComputePSOs[kernel]->SetRootSignature(ComputeSignature);
            ComputePSOs[kernel]->SetShader(ComputeKernels[kernel].get());
            ComputePSOs[kernel]->Initialize(Device);
            ComputePSOs[kernel]->GetPSO()->SetName(EKernels::ToWide(kernel).c_str());
        }
    }
}

void FluidSimulationResources::CompileComputeShaders()
{
    static std::string threadCountStr = std::to_string(FLUID_SIM_GROUP_COUNT);
    D3D_SHADER_MACRO macros[] = {"GROUP_SIZE", threadCountStr.c_str(), NULL, NULL};
    
    ComputeKernels[EKernels::ExternalForces] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, macros, "ExternalForces", "cs_5_1"));
    ComputeKernels[EKernels::ExternalForces]->LoadAndCompile();
    
    ComputeKernels[EKernels::UpdateSpatialHash] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, macros, "UpdateSpatialHash", "cs_5_1"));
    ComputeKernels[EKernels::UpdateSpatialHash]->LoadAndCompile();
    
    ComputeKernels[EKernels::Reorder] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, macros, "Reorder", "cs_5_1"));
    ComputeKernels[EKernels::Reorder]->LoadAndCompile();
    
    ComputeKernels[EKernels::ReorderCopyBack] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, macros, "ReorderCopyBack", "cs_5_1"));
    ComputeKernels[EKernels::ReorderCopyBack]->LoadAndCompile();
    
    ComputeKernels[EKernels::CalculateDensities] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, macros, "CalculateDensities", "cs_5_1"));
    ComputeKernels[EKernels::CalculateDensities]->LoadAndCompile();
    
    ComputeKernels[EKernels::CalculatePressureForce] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, macros, "CalculatePressureForce", "cs_5_1"));
    ComputeKernels[EKernels::CalculatePressureForce]->LoadAndCompile();
    
    ComputeKernels[EKernels::CalculateViscosity] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, macros, "CalculateViscosity", "cs_5_1"));
    ComputeKernels[EKernels::CalculateViscosity]->LoadAndCompile();
    
    ComputeKernels[EKernels::UpdatePositions] = std::move(
        std::make_shared<GShader>(L"Shaders\\FluidSimulation.hlsl", ComputeShader, macros, "UpdatePositions", "cs_5_1"));
    ComputeKernels[EKernels::UpdatePositions]->LoadAndCompile();
}

void FluidSimulationResources::InitializeBuffers(const FluidSimulationData& simData, const ParticleSpawner::SpawnData& spawnData)
{
    uint32_t numParticles = simData.numParticles;

    if (!SimulationSettingsBuffer)
        SimulationSettingsBuffer = std::make_shared<ConstantUploadBuffer<FluidSimulationData>>(Device, 1, L"Simulation Settings");

    if (PositionsBuffer)
    {
        PositionsBuffer->Reset();
        PositionsBuffer.reset();
    }
    if (VelocityBuffer)
    {
        VelocityBuffer->Reset();
        VelocityBuffer.reset();
    }
    if (DensityBuffer)
    {
        DensityBuffer->Reset();
        DensityBuffer.reset();
    }
    if (PredictedPositionsBuffer)
    {
        PredictedPositionsBuffer->Reset();
        PredictedPositionsBuffer.reset();
    }
    if (SortTargetPositionsBuffer)
    {
        SortTargetPositionsBuffer->Reset();
        SortTargetPositionsBuffer.reset();
    }
    if (SortTargetVelocityBuffer)
    {
        SortTargetVelocityBuffer->Reset();
        SortTargetVelocityBuffer.reset();
    }
    if (SortTargetPredictedPositionsBuffer)
    {
        SortTargetPredictedPositionsBuffer->Reset();
        SortTargetPredictedPositionsBuffer.reset();
    }
    
    using namespace DirectX::SimpleMath;
    PositionsBuffer           = std::make_shared<GBuffer>(Device, sizeof(Vector3), numParticles, L"PositionsBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    VelocityBuffer            = std::make_shared<GBuffer>(Device, sizeof(Vector3), numParticles, L"VelocityBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    DensityBuffer             = std::make_shared<GBuffer>(Device, sizeof(Vector2), numParticles, L"DensityBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    PredictedPositionsBuffer  = std::make_shared<GBuffer>(Device, sizeof(Vector3), numParticles, L"PredictedPositionsBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    SortTargetPositionsBuffer             = std::make_shared<GBuffer>(Device, sizeof(Vector3), numParticles, L"SortTargetPositionsBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    SortTargetVelocityBuffer              = std::make_shared<GBuffer>(Device, sizeof(Vector3), numParticles, L"SortTargetVelocityBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    SortTargetPredictedPositionsBuffer    = std::make_shared<GBuffer>(Device, sizeof(Vector3), numParticles, L"SortTargetPredictedPositionsBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    {
        auto queue = Device->GetCommandQueue();
        auto cmdList = queue->GetCommandList();

        PositionsBuffer->LoadData(spawnData.Points.data(), cmdList);
        PredictedPositionsBuffer->LoadData(spawnData.Points.data(), cmdList);
        VelocityBuffer->LoadData(spawnData.Velocities.data(), cmdList);

        cmdList->TransitionBarrier(PositionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(VelocityBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(PredictedPositionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();

        queue->ExecuteCommandList(cmdList);
        queue->Flush();
    }
}

void FluidSimulationResources::InitializeDescriptors()
{
    {
        ComputeDescriptors = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                                                             EComputeBufferOffsets::BufferCount);

        using namespace DirectX::SimpleMath;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;

        uavDesc.Buffer.NumElements = PositionsBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = PositionsBuffer->GetStride();
        PositionsBuffer->CreateUnorderedAccessView(&uavDesc, &ComputeDescriptors, EComputeBufferOffsets::PositionsBufferOffset);

        uavDesc.Buffer.NumElements = PredictedPositionsBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = PredictedPositionsBuffer->GetStride();
        PredictedPositionsBuffer->CreateUnorderedAccessView(&uavDesc, &ComputeDescriptors, EComputeBufferOffsets::PredictedPositionsBufferOffset);

        uavDesc.Buffer.NumElements = VelocityBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = VelocityBuffer->GetStride();
        VelocityBuffer->CreateUnorderedAccessView(&uavDesc, &ComputeDescriptors, EComputeBufferOffsets::VelocityBufferOffset);

        uavDesc.Buffer.NumElements = DensityBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = DensityBuffer->GetStride();
        DensityBuffer->CreateUnorderedAccessView(&uavDesc, &ComputeDescriptors, EComputeBufferOffsets::DensityBufferOffset);

        uavDesc.Buffer.NumElements = SortTargetPositionsBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = SortTargetPositionsBuffer->GetStride();
        SortTargetPositionsBuffer->CreateUnorderedAccessView(&uavDesc, &ComputeDescriptors, EComputeBufferOffsets::SortTarget_PositionsBufferOffset);

        uavDesc.Buffer.NumElements = SortTargetVelocityBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = SortTargetVelocityBuffer->GetStride();
        SortTargetVelocityBuffer->CreateUnorderedAccessView(&uavDesc, &ComputeDescriptors, EComputeBufferOffsets::SortTarget_VelocityBufferOffset);

        uavDesc.Buffer.NumElements = SortTargetPredictedPositionsBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = SortTargetPredictedPositionsBuffer->GetStride();
        SortTargetPredictedPositionsBuffer->CreateUnorderedAccessView(&uavDesc, &ComputeDescriptors, EComputeBufferOffsets::SortTarget_PredictedPositionsBufferOffset);

        const auto& spatialKeys = SpatialHash.GetSpatialKeys();
        uavDesc.Buffer.NumElements = spatialKeys->GetElementCount();
        uavDesc.Buffer.StructureByteStride = spatialKeys->GetStride();
        spatialKeys->CreateUnorderedAccessView(&uavDesc, &ComputeDescriptors, EComputeBufferOffsets::SpatialHash_SpatialKeysOffset);

        const auto& spatialOffsets = SpatialHash.GetSpatialOffsets();
        uavDesc.Buffer.NumElements = spatialOffsets->GetElementCount();
        uavDesc.Buffer.StructureByteStride = spatialOffsets->GetStride();
        spatialOffsets->CreateUnorderedAccessView(&uavDesc, &ComputeDescriptors, EComputeBufferOffsets::SpatialHash_SpatialOffsetsOffset);

        const auto& spatialIndices = SpatialHash.GetSpatialIndices();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Buffer.FirstElement = 0;

        srvDesc.Buffer.NumElements = spatialIndices->GetElementCount();
        srvDesc.Buffer.StructureByteStride = spatialIndices->GetStride();
        spatialIndices->CreateShaderResourceView(&srvDesc, &ComputeDescriptors, EComputeBufferOffsets::SpatialHash_SpatialIndicesOffset);
    }

    /*
    {
        GraphicsDescriptors = Device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, EDrawBufferOffsets::BufferCount);
    
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Buffer.FirstElement = 0;
    
        srvDesc.Buffer.NumElements = SimData->numParticles;
        srvDesc.Buffer.StructureByteStride = PositionsBuffer->GetStride();
        PositionsBuffer->CreateShaderResourceView(&srvDesc, &GraphicsDescriptors, EDrawBufferOffsets::PositionsBufferOffset);
        srvDesc.Buffer.NumElements = SimData->numParticles;
        srvDesc.Buffer.StructureByteStride = PositionsBuffer->GetStride();
        PositionsBuffer->CreateShaderResourceView(&srvDesc, &GraphicsDescriptors, EDrawBufferOffsets::PositionsBufferOffset);
    
        srvDesc.Buffer.NumElements = SimData->numParticles;
        srvDesc.Buffer.StructureByteStride = VelocityBuffer->GetStride();
        VelocityBuffer->CreateShaderResourceView(&srvDesc, &GraphicsDescriptors, EDrawBufferOffsets::VelocityBufferOffset);
    }
    */
    
}

void FluidSimulationCrossResources::Initialize(const FluidSimulationResources& Resources,
    const ptr<GDevice>& primaryDevice, const ptr<GDevice>& secondaryDevice)
{
    sharedPositions = std::make_shared<GCrossAdapterResource>(Resources.PositionsBuffer->GetD3D12ResourceDesc(), primaryDevice, secondaryDevice, L"Shared Positions Buffer");
    sharedVelocities = std::make_shared<GCrossAdapterResource>(Resources.VelocityBuffer->GetD3D12ResourceDesc(), primaryDevice, secondaryDevice, L"Shared Velocity Buffer");
}

SharedFluidParticleEmitter::SharedFluidParticleEmitter(
    const std::shared_ptr<GDevice>& primeDevice,
    const std::shared_ptr<GDevice>& secondaryDevice,
    const FluidSimulationData& simData,
    const ParticleSpawner& particleSpawner)
{
    m_primaryDevice = primeDevice;

    const auto& spawnData = particleSpawner.GetSpawnData();
    
    PrimaryResources.Initialize(primeDevice, simData, spawnData);
    UpdateSmoothingConstants(PrimaryResources);
    SecondaryResources.Initialize(secondaryDevice, simData, spawnData);
    UpdateSmoothingConstants(SecondaryResources);

    CrossResources.Initialize(PrimaryResources, primeDevice, secondaryDevice);

    InitializePSO();
    InitializeDescriptors();
}

void SharedFluidParticleEmitter::Update(const PEPEngine::Utils::GameTimer* gt)
{
    if (!hasScale)
    {
        m_baseScale = gameObject->GetComponent<Transform>()->GetScale();
        hasScale = true;
    }
    Vector3 tempScale = m_baseScale;
    tempScale.x = m_baseScale.x * (cos(gt->TotalTime()) + 2.f) * .8f;
    
    PrimaryResources.SimData.localToWorld = gameObject->GetTransform()->GetWorldMatrix();
    PrimaryResources.SimData.worldToLocal = PrimaryResources.SimData.localToWorld.Invert();
    SecondaryResources.SimData.localToWorld = gameObject->GetTransform()->GetWorldMatrix();
    SecondaryResources.SimData.worldToLocal = SecondaryResources.SimData.localToWorld.Invert();
    
    gameObject->GetComponent<Transform>()->SetScale(tempScale);
    
    const auto* cam = HybridParticleApp::GetApp().GetMainCamera();
    auto& viewMat = cam->GetViewMatrix();
    auto& projMat = cam->GetProjectionMatrix();

    auto invView = viewMat.Invert();

    Vector3 right(invView._11, invView._12, invView._13);
    Vector3 up(invView._21, invView._22, invView._23);

    m_drawData.ViewProj = viewMat * projMat;
    up.Normalize(m_drawData.BillboardUp);
    right.Normalize(m_drawData.BillboardRight);
    m_drawData.size = 0.1f;
}

void SharedFluidParticleEmitter::Dispatch(const std::shared_ptr<GCommandList>& cmdList, FluidSimulationResources& resources, const GameTimer& gt)
{
    float deltaTime = gt.DeltaTime();
    float maxDeltaTime = MaxTimeStepFps > 0 ? 1 / MaxTimeStepFps : FLT_MAX;
    float dt = std::min(deltaTime, maxDeltaTime);
    float subStepDeltaTime = deltaTime / IterationsPerFrame;
    UpdateSettings(resources, subStepDeltaTime, dt);

    for (int i = 0; i < IterationsPerFrame; i++)
    {
        resources.SimData.simTime += subStepDeltaTime;
        RunSimulationStep(cmdList, resources);
    }
}

void SharedFluidParticleEmitter::InitializePSO()
{
    if (m_renderPSO == nullptr)
    {
        auto vertexShader = std::move(
            std::make_shared<GShader>(L"Shaders\\FluidParticleDraw.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
        vertexShader->LoadAndCompile();

        auto pixelShader = std::move(
            std::make_shared<GShader>(L"Shaders\\FluidParticleDraw.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));
        pixelShader->LoadAndCompile();

        CD3DX12_DESCRIPTOR_RANGE range[2];
        range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        range[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

        m_renderSignature.AddConstantParameter(sizeof(FluidParticleDrawData) / sizeof(float), 0);
        m_renderSignature.AddDescriptorParameter(&range[0], 1); //Particles positions 
        m_renderSignature.AddDescriptorParameter(&range[1], 1); //Particles velocities 
        m_renderSignature.Initialize(m_primaryDevice);

        D3D12_GRAPHICS_PIPELINE_STATE_DESC renderPSODesc;

        ZeroMemory(&renderPSODesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
        renderPSODesc.InputLayout = {nullptr, 0};
        renderPSODesc.pRootSignature = m_renderSignature.GetNativeSignature().Get();
        renderPSODesc.VS = vertexShader->GetShaderResource();
        renderPSODesc.PS = pixelShader->GetShaderResource();
        renderPSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        renderPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        renderPSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        renderPSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        renderPSODesc.SampleMask = UINT_MAX;
        renderPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        renderPSODesc.NumRenderTargets = 1;
        renderPSODesc.RTVFormats[0] = GetSRGBFormat(BackBufferFormat);
        renderPSODesc.SampleDesc.Count = 1;
        renderPSODesc.SampleDesc.Quality = 0;
        renderPSODesc.DSVFormat = DepthStencilFormat;

        m_renderPSO = std::make_shared<GraphicPSO>(RenderMode::Particle);
        m_renderPSO->SetPsoDesc(renderPSODesc);
        {
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
            m_renderPSO->SetRenderTargetBlendState(0, blendDesc);
        }
        m_renderPSO->Initialize(m_primaryDevice);
        m_renderPSO->GetPSO()->SetName(L"FluidParticleDraw");
    }
}

void SharedFluidParticleEmitter::Draw(const std::shared_ptr<GCommandList>& cmdList)
{
    cmdList->TransitionBarrier(PrimaryResources.PositionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(PrimaryResources.VelocityBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();

    cmdList->SetRootSignature(m_renderSignature);
    cmdList->SetPipelineState(*m_renderPSO);
    cmdList->SetDescriptorsHeap(&m_graphicsDescriptors);
    
    cmdList->SetRoot32BitConstants (0, sizeof(FluidParticleDrawData) / sizeof(float), &m_drawData, 0);
    cmdList->SetRootDescriptorTable(1, &m_graphicsDescriptors, EDrawBufferOffsets::PositionsBufferOffset);
    cmdList->SetRootDescriptorTable(2, &m_graphicsDescriptors, EDrawBufferOffsets::VelocityBufferOffset);

    cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->Draw(4, PrimaryResources.SimData.numParticles);
    
    cmdList->TransitionBarrier(PrimaryResources.PositionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(PrimaryResources.VelocityBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();
}

void SharedFluidParticleEmitter::InitializeDescriptors()
{
    {
        m_graphicsDescriptors = m_primaryDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, EDrawBufferOffsets::BufferCount);
    
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Buffer.FirstElement = 0;
    
        srvDesc.Buffer.NumElements = PrimaryResources.SimData.numParticles;
        srvDesc.Buffer.StructureByteStride = PrimaryResources.PositionsBuffer->GetStride();
        PrimaryResources.PositionsBuffer->CreateShaderResourceView(&srvDesc, &m_graphicsDescriptors, EDrawBufferOffsets::PositionsBufferOffset);
    
        srvDesc.Buffer.NumElements = PrimaryResources.SimData.numParticles;
        srvDesc.Buffer.StructureByteStride = PrimaryResources.VelocityBuffer->GetStride();
        PrimaryResources.VelocityBuffer->CreateShaderResourceView(&srvDesc, &m_graphicsDescriptors, EDrawBufferOffsets::VelocityBufferOffset);
    }
}

void SharedFluidParticleEmitter::UpdateSettings(FluidSimulationResources& resources, float stepDeltaTime, float frameDeltaTime)
{
    if (resources.OldSmoothingRadius != resources.SimData.smoothingRadius)
    {
        resources.OldSmoothingRadius = resources.SimData.smoothingRadius;
        UpdateSmoothingConstants(resources);
    }

    resources.SimData.deltaTime = stepDeltaTime;
    resources.SimulationSettingsBuffer->CopyData(0, resources.SimData);
}

void SharedFluidParticleEmitter::UpdateSmoothingConstants(FluidSimulationResources& resources)
{
    float r = resources.SimData.smoothingRadius;
    resources.SmoothingConstants.SpikyPow2 = 15 / (2 * DirectX::XM_PI * pow(r, 5));
    resources.SmoothingConstants.SpikyPow3 = 15 / (DirectX::XM_PI * pow(r, 6));
    resources.SmoothingConstants.SpikyPow2Grad = 15 / (DirectX::XM_PI * pow(r, 5));
    resources.SmoothingConstants.SpikyPow3Grad = 45 / (DirectX::XM_PI * pow(r, 6));
}

void SharedFluidParticleEmitter::RunSimulationStep(const std::shared_ptr<GCommandList>& cmdList, FluidSimulationResources& resources)
{
    const size_t GroupSize = ceil(static_cast<float>(resources.SimData.numParticles) / static_cast<float>(FLUID_SIM_GROUP_COUNT));

    {
        cmdList->TransitionBarrier(resources.PositionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(resources.VelocityBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(resources.DensityBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(resources.PredictedPositionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(resources.SortTargetPositionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(resources.SortTargetVelocityBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(resources.SortTargetPredictedPositionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(resources.SpatialHash.GetSpatialKeys()->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(resources.SpatialHash.GetSpatialOffsets()->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        
        cmdList->TransitionBarrier(resources.SpatialHash.GetSpatialIndices()->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmdList->FlushResourceBarriers();
    }

    cmdList->SetDescriptorsHeap(&resources.ComputeDescriptors);
    cmdList->SetComputeRootSignature(resources.ComputeSignature);
    
    cmdList->SetComputeRootConstantBufferView(FluidSimulationResources::ERootSignatureSlots::SimulationSettingsSlot, *resources.SimulationSettingsBuffer.get());
    cmdList->SetComputeRoot32BitConstants(FluidSimulationResources::ERootSignatureSlots::SmoothingConstantsSlot, 4, &resources.SmoothingConstants, 0);

    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::PositionsBufferSlot,                       &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::PositionsBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::VelocityBufferSlot,                        &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::VelocityBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::PredictedPositionsBufferSlot,              &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::PredictedPositionsBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::DensityBufferSlot,                         &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::DensityBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::SortTarget_PositionsBufferSlot,            &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::SortTarget_PositionsBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::SortTarget_VelocityBufferSlot,             &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::SortTarget_VelocityBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::SortTarget_PredictedPositionsBufferSlot,   &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::SortTarget_PredictedPositionsBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::SpatialHash_SpatialIndicesSlot,            &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::SpatialHash_SpatialIndicesOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::SpatialHash_SpatialOffsetsSlot,            &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::SpatialHash_SpatialOffsetsOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::SpatialHash_SpatialKeysSlot,               &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::SpatialHash_SpatialKeysOffset);

    {
        cmdList->SetPipelineState(*resources.ComputePSOs[FluidSimulationResources::EKernels::ExternalForces].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(resources.VelocityBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(resources.PredictedPositionsBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    {
        cmdList->SetPipelineState(*resources.ComputePSOs[FluidSimulationResources::EKernels::UpdateSpatialHash].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(resources.SpatialHash.GetSpatialKeys()->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    resources.SpatialHash.Run(cmdList);
    
    cmdList->TransitionBarrier(resources.SpatialHash.GetSpatialIndices()->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    
    cmdList->SetDescriptorsHeap(&resources.ComputeDescriptors);
    cmdList->SetComputeRootSignature(resources.ComputeSignature);
    
    cmdList->SetComputeRootConstantBufferView(FluidSimulationResources::ERootSignatureSlots::SimulationSettingsSlot, *resources.SimulationSettingsBuffer.get());
    cmdList->SetComputeRoot32BitConstants(FluidSimulationResources::ERootSignatureSlots::SmoothingConstantsSlot, 4, &resources.SmoothingConstants, 0);

    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::PositionsBufferSlot,                       &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::PositionsBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::PredictedPositionsBufferSlot,              &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::PredictedPositionsBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::VelocityBufferSlot,                        &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::VelocityBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::DensityBufferSlot,                         &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::DensityBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::SortTarget_PositionsBufferSlot,            &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::SortTarget_PositionsBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::SortTarget_PredictedPositionsBufferSlot,   &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::SortTarget_PredictedPositionsBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::SortTarget_VelocityBufferSlot,             &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::SortTarget_VelocityBufferOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::SpatialHash_SpatialKeysSlot,               &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::SpatialHash_SpatialKeysOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::SpatialHash_SpatialOffsetsSlot,            &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::SpatialHash_SpatialOffsetsOffset);
    cmdList->SetComputeRootDescriptorTable(FluidSimulationResources::ERootSignatureSlots::SpatialHash_SpatialIndicesSlot,            &resources.ComputeDescriptors, FluidSimulationResources::EComputeBufferOffsets::SpatialHash_SpatialIndicesOffset);
    
    {
        cmdList->SetPipelineState(*resources.ComputePSOs[FluidSimulationResources::EKernels::Reorder].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(resources.SortTargetPositionsBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(resources.SortTargetVelocityBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(resources.SortTargetPredictedPositionsBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    {
        cmdList->SetPipelineState(*resources.ComputePSOs[FluidSimulationResources::EKernels::ReorderCopyBack].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(resources.PositionsBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(resources.VelocityBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(resources.PredictedPositionsBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    {
        cmdList->SetPipelineState(*resources.ComputePSOs[FluidSimulationResources::EKernels::CalculateDensities].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(resources.DensityBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    {
        cmdList->SetPipelineState(*resources.ComputePSOs[FluidSimulationResources::EKernels::CalculatePressureForce].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(resources.VelocityBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    if (resources.SimData.viscosityStrength > 0.0f)
    {
        cmdList->SetPipelineState(*resources.ComputePSOs[FluidSimulationResources::EKernels::CalculateViscosity].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(resources.VelocityBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    {
        cmdList->SetPipelineState(*resources.ComputePSOs[FluidSimulationResources::EKernels::UpdatePositions].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(resources.PositionsBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(resources.VelocityBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }
}

