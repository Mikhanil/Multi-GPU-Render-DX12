#pragma once
#include "Renderer.h"

enum EKernels : uint8_t
{
    ExternalForces      = 0,
    UpdateSpatialHash,
    Reorder,
    ReorderCopyBack,
    CalculateDensities,
    CalculatePressureForce,
    CalculateViscosity,
    UpdatePositions,
    UpdateDensityTexture,
    Count
};

class FluidEmitter :
    public Renderer
{
protected:
    std::shared_ptr<GRootSignature> renderSignature;
    // std::shared_ptr<GRootSignature> computeSignature;
    std::shared_ptr<GraphicPSO> renderPSO;
    std::shared_ptr<ComputePSO> injectedPSO;
    std::shared_ptr<ComputePSO> simulatedPSO;
    std::shared_ptr<GShader> injectedShader;
    std::shared_ptr<GShader> simulatedShader;
    
    //Stuff that i actually need for my compute pipeline
    // The signature will be the same for all the PSOs
    std::shared_ptr<GRootSignature> computeSignature;
    std::unordered_map<EKernels, std::shared_ptr<GShader>> computeKernels;
    std::unordered_map<EKernels, std::shared_ptr<ComputePSO>> computePSOs;

    static ParticleData GenerateParticle();
    void CompileComputeShaders();

    void PSOInitialize();

    std::vector<std::shared_ptr<GTexture>> Atlas;

    std::shared_ptr<GDevice> device;

public:
    virtual void Dispatch(const std::shared_ptr<GCommandList>& cmdList) = 0;
};
