#pragma once

#include "GDescriptor.h"
#include "FluidEmitter.h"
#include "FluidSimulationData.h"
#include "SpacialHash.h"
#include "../MGPU-Particles/ParticleSpawner.h"

class FluidParticleEmitter : public FluidEmitter
{
    friend class CrossAdapterFluidParticleEmitter;
public:
    //void ChangeParticleCount(const DWORD newParticleCount);

    FluidParticleEmitter(const std::shared_ptr<GDevice>& primeDevice, const FluidSimulationData& simData, const ParticleSpawner& spawner);

    void Update(const PEPEngine::Utils::GameTimer* gt) override;
    void Dispatch(const std::shared_ptr<GCommandList>& cmdList, const GameTimer& gt) override;
    void Draw(const std::shared_ptr<GCommandList>& cmdList) override;

protected:
    void InitializeBuffers();
    void InitializeDescriptors();

    void UpdateSettings(float stepDeltaTime, float frameDeltaTime);
    void UpdateSmoothingConstants();

    void RunSimulationStep(const std::shared_ptr<GCommandList>& cmdList);
public:
    int m_iterationsPerFrame = 3;
    float m_maxTimeStepFps = 60;
private:
    FluidParticleDrawData m_drawData;

    ParticleSpawner::SpawnData m_spawnData;
    FluidSimulationData m_simData={};

    float m_oldSmoothingRadius;
    SpikyKernels m_smoothingConstants;

    SpatialHash m_spatialHash;

    std::shared_ptr<ConstantUploadBuffer<FluidSimulationData>> m_simulationSettingsBuffer = nullptr;
    
    std::shared_ptr<GBuffer> m_positionsBuffer;
    std::shared_ptr<GBuffer> m_velocityBuffer;
    std::shared_ptr<GBuffer> m_densityBuffer;
    std::shared_ptr<GBuffer> m_predictedPositionsBuffer;
    
    std::shared_ptr<GBuffer> m_sortTargetPositionsBuffer;
    std::shared_ptr<GBuffer> m_sortTargetVelocityBuffer;
    std::shared_ptr<GBuffer> m_sortTargetPredictedPositionsBuffer;

    GDescriptor m_computeDescriptors;
    GDescriptor m_graphicsDescriptors;
};
