#include "pch.h"
#include "FluidParticleEmitter.h"

#include "GameTimer.h"
#include "../MGPU-Particles/HybridParticleApp.h"

FluidParticleEmitter::FluidParticleEmitter(
    const std::shared_ptr<GDevice>& primeDevice,
    const FluidSimulationData& simData,
    const ParticleSpawner& particleSpawner)
{
    m_device = primeDevice;

    PSOInitialize();

    m_simData = simData;

    m_spawnData = particleSpawner.GetSpawnData();
    m_simData.numParticles = m_spawnData.Points.size();

    m_spatialHash.Initialize(m_device, m_simData.numParticles);

    InitializeBuffers();
    InitializeDescriptors();

    UpdateSmoothingConstants();
}

void FluidParticleEmitter::Update(const PEPEngine::Utils::GameTimer* gt)
{
    const auto* cam = HybridParticleApp::GetApp().GetMainCamera();
    auto& viewMat = cam->GetViewMatrix();
    auto& projMat = cam->GetProjectionMatrix();

    auto invView = viewMat.Invert();
    
    m_drawData.ViewProj = viewMat * projMat;
    m_drawData.BillboardUp = Vector3::TransformNormal(Vector3::Up, invView);
    m_drawData.BillboardUp.Normalize();
    m_drawData.BillboardRight = Vector3::TransformNormal(Vector3::Right, invView);
    m_drawData.BillboardRight.Normalize();
    m_drawData.size = 1.f;
}

void FluidParticleEmitter::Dispatch(const std::shared_ptr<GCommandList>& cmdList, const GameTimer& gt)
{
    float deltaTime = gt.DeltaTime();

    float subStepDeltaTime = deltaTime / m_iterationsPerFrame;
    UpdateSettings(subStepDeltaTime, deltaTime);

    for (int i = 0; i < m_iterationsPerFrame; i++)
    {
        m_simData.simTime += subStepDeltaTime;
        RunSimulationStep(cmdList);
    }
}

void FluidParticleEmitter::Draw(const std::shared_ptr<GCommandList>& cmdList)
{
    cmdList->TransitionBarrier(m_positionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    cmdList->TransitionBarrier(m_velocityBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();

    cmdList->SetRootSignature(m_renderSignature);
    cmdList->SetPipelineState(*m_renderPSO);
    cmdList->SetDescriptorsHeap(&m_graphicsDescriptors);
    
    cmdList->SetRoot32BitConstants (0, sizeof(FluidParticleDrawData) / sizeof(float), &m_drawData, 0);
    cmdList->SetRootDescriptorTable(1, &m_graphicsDescriptors, EDrawBufferOffsets::PositionsBufferOffset);
    cmdList->SetRootDescriptorTable(2, &m_graphicsDescriptors, EDrawBufferOffsets::VelocityBufferOffset);

    cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->Draw(4, m_simData.numParticles);
    
    cmdList->TransitionBarrier(m_positionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->TransitionBarrier(m_velocityBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
    cmdList->FlushResourceBarriers();
}

void FluidParticleEmitter::InitializeBuffers()
{
    uint32_t numParticles = m_simData.numParticles;

    if (!m_simulationSettingsBuffer)
        m_simulationSettingsBuffer = std::make_shared<ConstantUploadBuffer<FluidSimulationData>>(m_device, 1, L"Simulation Settings");

    if (m_positionsBuffer)
    {
        m_positionsBuffer->Reset();
        m_positionsBuffer.reset();
    }
    if (m_velocityBuffer)
    {
        m_velocityBuffer->Reset();
        m_velocityBuffer.reset();
    }
    if (m_densityBuffer)
    {
        m_densityBuffer->Reset();
        m_densityBuffer.reset();
    }
    if (m_predictedPositionsBuffer)
    {
        m_predictedPositionsBuffer->Reset();
        m_predictedPositionsBuffer.reset();
    }
    if (m_sortTargetPositionsBuffer)
    {
        m_sortTargetPositionsBuffer->Reset();
        m_sortTargetPositionsBuffer.reset();
    }
    if (m_sortTargetVelocityBuffer)
    {
        m_sortTargetVelocityBuffer->Reset();
        m_sortTargetVelocityBuffer.reset();
    }
    if (m_sortTargetPredictedPositionsBuffer)
    {
        m_sortTargetPredictedPositionsBuffer->Reset();
        m_sortTargetPredictedPositionsBuffer.reset();
    }
    
    using namespace DirectX::SimpleMath;
    m_positionsBuffer           = std::make_shared<GBuffer>(m_device, sizeof(Vector3), numParticles, L"PositionsBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_velocityBuffer            = std::make_shared<GBuffer>(m_device, sizeof(Vector3), numParticles, L"VelocityBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_densityBuffer             = std::make_shared<GBuffer>(m_device, sizeof(Vector2), numParticles, L"DensityBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_predictedPositionsBuffer  = std::make_shared<GBuffer>(m_device, sizeof(Vector3), numParticles, L"PredictedPositionsBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    m_sortTargetPositionsBuffer             = std::make_shared<GBuffer>(m_device, sizeof(Vector3), numParticles, L"SortTargetPositionsBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_sortTargetVelocityBuffer              = std::make_shared<GBuffer>(m_device, sizeof(Vector3), numParticles, L"SortTargetVelocityBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    m_sortTargetPredictedPositionsBuffer    = std::make_shared<GBuffer>(m_device, sizeof(Vector3), numParticles, L"SortTargetPredictedPositionsBuffer", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    {
        auto queue = m_device->GetCommandQueue();
        auto cmdList = queue->GetCommandList();

        m_positionsBuffer->LoadData(m_spawnData.Points.data(), cmdList);
        m_velocityBuffer->LoadData(m_spawnData.Velocities.data(), cmdList);
        m_predictedPositionsBuffer->LoadData(m_spawnData.Points.data(), cmdList);

        cmdList->TransitionBarrier(m_positionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(m_velocityBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
        cmdList->TransitionBarrier(m_predictedPositionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
        cmdList->FlushResourceBarriers();

        queue->ExecuteCommandList(cmdList);
        queue->Flush();
    }
}

void FluidParticleEmitter::InitializeDescriptors()
{
    {
        m_computeDescriptors = m_device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, EComputeBufferOffsets::BufferCount);
        
        using namespace DirectX::SimpleMath;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        
        uavDesc.Buffer.NumElements = m_positionsBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = m_positionsBuffer->GetStride();
        m_positionsBuffer->CreateUnorderedAccessView(&uavDesc, &m_computeDescriptors, EComputeBufferOffsets::PositionsBufferOffset);
        
        uavDesc.Buffer.NumElements = m_predictedPositionsBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = m_predictedPositionsBuffer->GetStride();
        m_predictedPositionsBuffer->CreateUnorderedAccessView(&uavDesc, &m_computeDescriptors, EComputeBufferOffsets::PredictedPositionsBufferOffset);
        
        uavDesc.Buffer.NumElements = m_velocityBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = m_velocityBuffer->GetStride();
        m_velocityBuffer->CreateUnorderedAccessView(&uavDesc, &m_computeDescriptors, EComputeBufferOffsets::VelocityBufferOffset);
        
        uavDesc.Buffer.NumElements = m_densityBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = m_densityBuffer->GetStride();
        m_densityBuffer->CreateUnorderedAccessView(&uavDesc, &m_computeDescriptors, EComputeBufferOffsets::DensityBufferOffset);
        
        uavDesc.Buffer.NumElements = m_sortTargetPositionsBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = m_sortTargetPositionsBuffer->GetStride();
        m_sortTargetPositionsBuffer->CreateUnorderedAccessView(&uavDesc, &m_computeDescriptors, EComputeBufferOffsets::SortTarget_PositionsBufferOffset);

        uavDesc.Buffer.NumElements = m_sortTargetVelocityBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = m_sortTargetVelocityBuffer->GetStride();
        m_sortTargetVelocityBuffer->CreateUnorderedAccessView(&uavDesc, &m_computeDescriptors, EComputeBufferOffsets::SortTarget_VelocityBufferOffset);
        
        uavDesc.Buffer.NumElements = m_sortTargetPredictedPositionsBuffer->GetElementCount();
        uavDesc.Buffer.StructureByteStride = m_sortTargetPredictedPositionsBuffer->GetStride();
        m_sortTargetPredictedPositionsBuffer->CreateUnorderedAccessView(&uavDesc, &m_computeDescriptors, EComputeBufferOffsets::SortTarget_PredictedPositionsBufferOffset);

        const auto& spatialKeys = m_spatialHash.GetSpatialKeys();
        uavDesc.Buffer.NumElements = spatialKeys->GetElementCount();
        uavDesc.Buffer.StructureByteStride = spatialKeys->GetStride();
        spatialKeys->CreateUnorderedAccessView(&uavDesc, &m_computeDescriptors, EComputeBufferOffsets::SpatialHash_SpatialKeysOffset);

        const auto& spatialOffsets = m_spatialHash.GetSpatialOffsets();
        uavDesc.Buffer.NumElements = spatialOffsets->GetElementCount();
        uavDesc.Buffer.StructureByteStride = spatialOffsets->GetStride();
        spatialOffsets->CreateUnorderedAccessView(&uavDesc, &m_computeDescriptors, EComputeBufferOffsets::SpatialHash_SpatialOffsetsOffset);

        const auto& spatialIndices = m_spatialHash.GetSpatialIndices();
        
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Buffer.FirstElement = 0;
    
        srvDesc.Buffer.NumElements = spatialIndices->GetElementCount();
        srvDesc.Buffer.StructureByteStride = spatialIndices->GetStride();
        spatialIndices->CreateShaderResourceView(&srvDesc, &m_computeDescriptors, EComputeBufferOffsets::SpatialHash_SpatialIndicesOffset);
    }

    {
        m_graphicsDescriptors = m_device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, EDrawBufferOffsets::BufferCount);
    
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Buffer.FirstElement = 0;
    
        srvDesc.Buffer.NumElements = m_simData.numParticles;
        srvDesc.Buffer.StructureByteStride = m_positionsBuffer->GetStride();
        m_positionsBuffer->CreateShaderResourceView(&srvDesc, &m_graphicsDescriptors, EDrawBufferOffsets::PositionsBufferOffset);
    
        srvDesc.Buffer.NumElements = m_simData.numParticles;
        srvDesc.Buffer.StructureByteStride = m_velocityBuffer->GetStride();
        m_velocityBuffer->CreateShaderResourceView(&srvDesc, &m_graphicsDescriptors, EDrawBufferOffsets::VelocityBufferOffset);
    }
}

void FluidParticleEmitter::UpdateSettings(float stepDeltaTime, float frameDeltaTime)
{
    if (m_oldSmoothingRadius != m_simData.smoothingRadius)
    {
        m_oldSmoothingRadius = m_simData.smoothingRadius;
        UpdateSmoothingConstants();
    }

    m_simData.deltaTime = stepDeltaTime;
    m_simulationSettingsBuffer->CopyData(0, m_simData);
}

void FluidParticleEmitter::UpdateSmoothingConstants()
{
    float r = m_simData.smoothingRadius;
    m_smoothingConstants.SpikyPow2 = 15 / (2 * DirectX::XM_PI * pow(r, 5));
    m_smoothingConstants.SpikyPow3 = 15 / (DirectX::XM_PI * pow(r, 6));
    m_smoothingConstants.SpikyPow2Grad = 15 / (DirectX::XM_PI * pow(r, 5));
    m_smoothingConstants.SpikyPow3Grad = 45 / (DirectX::XM_PI * pow(r, 6));
}

void FluidParticleEmitter::RunSimulationStep(const std::shared_ptr<GCommandList>& cmdList)
{
    const size_t GroupSize = ceil(m_simData.numParticles / 256.f);

    {
        cmdList->TransitionBarrier(m_positionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(m_velocityBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(m_densityBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(m_predictedPositionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(m_sortTargetPositionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(m_sortTargetVelocityBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(m_sortTargetPredictedPositionsBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(m_spatialHash.GetSpatialKeys()->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cmdList->TransitionBarrier(m_spatialHash.GetSpatialOffsets()->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        
        cmdList->TransitionBarrier(m_spatialHash.GetSpatialIndices()->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

        cmdList->FlushResourceBarriers();
    }

    cmdList->SetDescriptorsHeap(&m_computeDescriptors);
    cmdList->SetRootSignature(computeSignature);
    
    cmdList->SetRootConstantBufferView(ERootSignatureSlots::SimulationSettingsSlot, *m_simulationSettingsBuffer.get());
    cmdList->SetRoot32BitConstants(ERootSignatureSlots::SmoothingConstantsSlot, 4, &m_smoothingConstants, 0);

    cmdList->SetRootDescriptorTable(ERootSignatureSlots::PositionsBufferSlot, &m_computeDescriptors, EComputeBufferOffsets::PositionsBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::VelocityBufferSlot, &m_computeDescriptors, EComputeBufferOffsets::VelocityBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::PredictedPositionsBufferSlot, &m_computeDescriptors, EComputeBufferOffsets::PredictedPositionsBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::DensityBufferSlot, &m_computeDescriptors, EComputeBufferOffsets::DensityBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::SortTarget_PositionsBufferSlot, &m_computeDescriptors, EComputeBufferOffsets::SortTarget_PositionsBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::SortTarget_VelocityBufferSlot, &m_computeDescriptors, EComputeBufferOffsets::SortTarget_VelocityBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::SortTarget_PredictedPositionsBufferSlot, &m_computeDescriptors, EComputeBufferOffsets::SortTarget_PredictedPositionsBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::SpatialHash_SpatialIndicesSlot, &m_computeDescriptors, EComputeBufferOffsets::SpatialHash_SpatialIndicesOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::SpatialHash_SpatialOffsetsSlot, &m_computeDescriptors, EComputeBufferOffsets::SpatialHash_SpatialOffsetsOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::SpatialHash_SpatialKeysSlot, &m_computeDescriptors, EComputeBufferOffsets::SpatialHash_SpatialKeysOffset);

    {
        cmdList->SetPipelineState(*computePSOs[EKernels::ExternalForces].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(m_velocityBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(m_predictedPositionsBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    {
        cmdList->SetPipelineState(*computePSOs[EKernels::UpdateSpatialHash].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(m_spatialHash.GetSpatialKeys()->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    m_spatialHash.Run(cmdList);
    
    cmdList->TransitionBarrier(m_spatialHash.GetSpatialIndices()->GetD3D12Resource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    
    cmdList->SetDescriptorsHeap(&m_computeDescriptors);
    cmdList->SetRootSignature(computeSignature);
    
    cmdList->SetRootConstantBufferView(ERootSignatureSlots::SimulationSettingsSlot, *m_simulationSettingsBuffer.get());
    cmdList->SetRoot32BitConstants(ERootSignatureSlots::SmoothingConstantsSlot, 4, &m_smoothingConstants, 0);

    cmdList->SetRootDescriptorTable(ERootSignatureSlots::PositionsBufferSlot,                       &m_computeDescriptors, EComputeBufferOffsets::PositionsBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::PredictedPositionsBufferSlot,              &m_computeDescriptors, EComputeBufferOffsets::PredictedPositionsBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::VelocityBufferSlot,                        &m_computeDescriptors, EComputeBufferOffsets::VelocityBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::DensityBufferSlot,                         &m_computeDescriptors, EComputeBufferOffsets::DensityBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::SortTarget_PositionsBufferSlot,            &m_computeDescriptors, EComputeBufferOffsets::SortTarget_PositionsBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::SortTarget_PredictedPositionsBufferSlot,   &m_computeDescriptors, EComputeBufferOffsets::SortTarget_PredictedPositionsBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::SortTarget_VelocityBufferSlot,             &m_computeDescriptors, EComputeBufferOffsets::SortTarget_VelocityBufferOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::SpatialHash_SpatialKeysSlot,               &m_computeDescriptors, EComputeBufferOffsets::SpatialHash_SpatialKeysOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::SpatialHash_SpatialOffsetsSlot,            &m_computeDescriptors, EComputeBufferOffsets::SpatialHash_SpatialOffsetsOffset);
    cmdList->SetRootDescriptorTable(ERootSignatureSlots::SpatialHash_SpatialIndicesSlot,            &m_computeDescriptors, EComputeBufferOffsets::SpatialHash_SpatialIndicesOffset);
    
    {
        cmdList->SetPipelineState(*computePSOs[EKernels::Reorder].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(m_sortTargetPositionsBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(m_sortTargetVelocityBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(m_sortTargetPredictedPositionsBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    {
        cmdList->SetPipelineState(*computePSOs[EKernels::ReorderCopyBack].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(m_positionsBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(m_velocityBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(m_predictedPositionsBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    {
        cmdList->SetPipelineState(*computePSOs[EKernels::ReorderCopyBack].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(m_positionsBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(m_velocityBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(m_predictedPositionsBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    {
        cmdList->SetPipelineState(*computePSOs[EKernels::CalculateDensities].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(m_densityBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    {
        cmdList->SetPipelineState(*computePSOs[EKernels::CalculatePressureForce].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(m_velocityBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    if (m_simData.viscosityStrength != 0.0f)
    {
        cmdList->SetPipelineState(*computePSOs[EKernels::CalculateViscosity].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(m_velocityBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }

    {
        cmdList->SetPipelineState(*computePSOs[EKernels::UpdatePositions].get());
        cmdList->Dispatch(GroupSize, 1, 1);

        cmdList->UAVBarrier(m_positionsBuffer->GetD3D12Resource());
        cmdList->UAVBarrier(m_velocityBuffer->GetD3D12Resource());
        cmdList->FlushResourceBarriers();
    }
}

