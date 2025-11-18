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

    struct EDrawBufferOffsets
    {
        enum Enum : uint8_t
        {
            PositionsBufferOffset,
            VelocityBufferOffset,
            
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
    
    GRootSignature m_renderSignature{};
    // std::shared_ptr<GRootSignature> computeSignature;
    std::shared_ptr<GraphicPSO> m_renderPSO;
    std::shared_ptr<ComputePSO> injectedPSO;
    std::shared_ptr<ComputePSO> simulatedPSO;
    std::shared_ptr<GShader> injectedShader;
    std::shared_ptr<GShader> simulatedShader;
    
    // Stuff that I actually need for my compute pipeline
    // The signature will be the same for all the PSOs
    GRootSignature computeSignature{};
    std::unordered_map<EKernels::Enum, std::shared_ptr<GShader>> computeKernels;
    std::unordered_map<EKernels::Enum, std::shared_ptr<ComputePSO>> computePSOs;

    // static ParticleData GenerateParticle();
    void CompileComputeShaders();

    void PSOInitialize();

    std::vector<std::shared_ptr<GTexture>> Atlas;

    std::shared_ptr<GDevice> m_device;

public:
    virtual void Dispatch(const std::shared_ptr<GCommandList>& cmdList, const GameTimer& gt) = 0;
    virtual ~FluidEmitter() = default;
};
