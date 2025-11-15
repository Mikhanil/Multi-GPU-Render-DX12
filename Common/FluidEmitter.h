#pragma once
#include "Renderer.h"
#include "GRootSignature.h"

class FluidEmitter :
    public Renderer
{
protected:
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
    

    struct EKernels
    {
        enum Enum : uint8_t
        {
            ExternalForces      = 0,
            UpdateSpatialHash,
            Reorder,
            ReorderCopyBack,
            CalculateDensities,
            CalculatePressureForce,
            CalculateViscosity,
            UpdatePositions,
            Count
        };
    };
    
    GRootSignature renderSignature{};
    // std::shared_ptr<GRootSignature> computeSignature;
    std::shared_ptr<GraphicPSO> renderPSO;
    std::shared_ptr<ComputePSO> injectedPSO;
    std::shared_ptr<ComputePSO> simulatedPSO;
    std::shared_ptr<GShader> injectedShader;
    std::shared_ptr<GShader> simulatedShader;
    
    // Stuff that I actually need for my compute pipeline
    // The signature will be the same for all the PSOs
    GRootSignature computeSignature{};
    std::unordered_map<uint8_t, std::shared_ptr<GShader>> computeKernels;
    std::unordered_map<uint8_t, std::shared_ptr<ComputePSO>> computePSOs;

    // static ParticleData GenerateParticle();
    void CompileComputeShaders();

    void PSOInitialize();

    std::vector<std::shared_ptr<GTexture>> Atlas;

    std::shared_ptr<GDevice> m_device;

public:
    virtual void Dispatch(const std::shared_ptr<GCommandList>& cmdList, const GameTimer& gt) = 0;
    virtual ~FluidEmitter() = default;
};
