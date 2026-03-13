#pragma once

#include "Renderer.h"

#include "GDescriptor.h"
//#include "FluidEmitter.h"
#include "FluidSimulationData.h"
#include "SpacialHash.h"
#include "../MGPU-Particles/ParticleSpawner.h"

class GCrossAdapterResource;

class FluidSimulationResources
{
public:
#pragma region Enums
    
    struct ERootSignatureSlots
    {
        enum Enum : uint8_t
        {
            SimulationSettingsSlot,                     // b0
            SmoothingConstantsSlot,                     // b1
            
            PositionsBufferSlot,                        // u0
            PredictedPositionsBufferSlot,               // u1
            VelocityBufferSlot,                         // u2
            DensityBufferSlot,                          // u3

            SortTarget_PositionsBufferSlot,             // u4
            SortTarget_PredictedPositionsBufferSlot,    // u5
            SortTarget_VelocityBufferSlot,              // u6
            
            SpatialHash_SpatialKeysSlot,                // u7
            SpatialHash_SpatialOffsetsSlot,             // u8
            SpatialHash_SpatialIndicesSlot,             // t0

            BufferCount
        };
    };
    
    struct EComputeBufferOffsets
    {
        enum Enum : uint8_t
        {
            PositionsBufferOffset,
            PredictedPositionsBufferOffset,
            VelocityBufferOffset,
            DensityBufferOffset,

            SortTarget_PositionsBufferOffset,
            SortTarget_PredictedPositionsBufferOffset,
            SortTarget_VelocityBufferOffset,
            
            SpatialHash_SpatialKeysOffset,
            SpatialHash_SpatialOffsetsOffset,
            SpatialHash_SpatialIndicesOffset,

            BufferCount
        };
    };

    struct EKernels
    {
        enum Enum : uint8_t
        {
#define EKERNELS_LIST                            \
X(ExternalForces)                        \
X(UpdateSpatialHash)                     \
X(Reorder)                               \
X(ReorderCopyBack)                       \
X(CalculateDensities)                    \
X(CalculatePressureForce)                \
X(CalculateViscosity)                    \
X(UpdatePositions)

#define X(name) name,
            EKERNELS_LIST
#undef X
            Count
        };

        static const char* ToString(Enum e)
        {
            switch (e)
            {
#define X(name) case name: return "FluidSim::" #name;
                EKERNELS_LIST
#undef X
            default: return "";
            }
        }

        static std::wstring ToWide(Enum e)
        {
            const char* s = ToString(e);
            int size_needed = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
            std::wstring ws(size_needed, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s, -1, ws.data(), size_needed);
            return ws;
        }

        static constexpr Enum All[] = {
#define X(name) name,
            EKERNELS_LIST
    #undef X
        };
    };
#pragma endregion
    
public:
    FluidSimulationResources() = default;
    ~FluidSimulationResources() = default;

    void Initialize(const std::shared_ptr<GDevice>& device, const FluidSimulationData& simData, const ParticleSpawner::SpawnData& spawnData);

    std::shared_ptr<GDevice> Device;

    GRootSignature ComputeSignature;
    
    std::unordered_map<EKernels::Enum, std::shared_ptr<GShader>> ComputeKernels;
    std::unordered_map<EKernels::Enum, std::shared_ptr<ComputePSO>> ComputePSOs;

    FluidSimulationData SimData;
    ParticleSpawner::SpawnData SpawnData;

    float OldSmoothingRadius;
    SpikyKernels SmoothingConstants;
    
    std::shared_ptr<ConstantUploadBuffer<FluidSimulationData>> SimulationSettingsBuffer = nullptr;
    
    std::shared_ptr<GBuffer> PositionsBuffer;
    std::shared_ptr<GBuffer> VelocityBuffer;
    std::shared_ptr<GBuffer> DensityBuffer;
    std::shared_ptr<GBuffer> PredictedPositionsBuffer;
    
    std::shared_ptr<GBuffer> SortTargetPositionsBuffer;
    std::shared_ptr<GBuffer> SortTargetVelocityBuffer;
    std::shared_ptr<GBuffer> SortTargetPredictedPositionsBuffer;

    GDescriptor ComputeDescriptors;
    
    SpatialHash SpatialHash;
    
private:
    void PSOInitialize();
    void CompileComputeShaders();

    void InitializeBuffers(const FluidSimulationData& simData, const ParticleSpawner::SpawnData& spawnData);
    void InitializeDescriptors();
};

struct FluidSimulationCrossResources
{
    template<class t>
    using ptr = std::shared_ptr<t>;

    ptr<GCrossAdapterResource> sharedPositions;
    ptr<GCrossAdapterResource> sharedVelocities;

    void Initialize(const FluidSimulationResources& Resources, const ptr<GDevice>& primaryDevice, const ptr<GDevice>& secondaryDevice);
};

class SharedFluidParticleEmitter : public Renderer//, public FluidEmitter
{
    struct EDrawBufferOffsets
    {
        enum Enum : uint8_t
        {
            PositionsBufferOffset,
            VelocityBufferOffset,
            
            BufferCount
        };
    };
    
public:
    SharedFluidParticleEmitter(const std::shared_ptr<GDevice>& primeDevice, const std::shared_ptr<GDevice>& secondaryDevice, const FluidSimulationData& simData, const ParticleSpawner& spawner);
    virtual ~SharedFluidParticleEmitter() = default;

    void Update(const PEPEngine::Utils::GameTimer* gt) override;
    void Draw(const std::shared_ptr<GCommandList>& cmdList) override;

    void Dispatch(const std::shared_ptr<GCommandList>& cmdList, FluidSimulationResources& resources, const GameTimer& gt);
    
protected:
    void InitializePSO();
    void InitializeDescriptors();

    void UpdateSettings(FluidSimulationResources& resources, float stepDeltaTime, float frameDeltaTime);
    void UpdateSmoothingConstants(FluidSimulationResources& resources);

    void RunSimulationStep(const std::shared_ptr<GCommandList>& cmdList, FluidSimulationResources& resources);
public:
    int IterationsPerFrame = 3;
    float MaxTimeStepFps = 60;
    
    FluidSimulationResources PrimaryResources;
    FluidSimulationResources SecondaryResources;
    FluidSimulationCrossResources CrossResources;
private:
    std::shared_ptr<GDevice> m_primaryDevice;

    
    GDescriptor m_graphicsDescriptors;
    
    FluidParticleDrawData m_drawData;
    
    GRootSignature m_renderSignature;
    std::shared_ptr<GraphicPSO> m_renderPSO;

    Vector3 m_baseScale;
    float m_localAnimationTime = 0.0f;
    bool hasScale = false;
};
