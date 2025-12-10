#pragma once
#include "ComputePSO.h"
#include "GBuffer.h"
#include "GDescriptor.h"

class SpacialOffsetsCalculator
{
    using BufferPointer = std::shared_ptr<PEPEngine::Graphics::GBuffer>;
    using ShaderPointer = std::shared_ptr<PEPEngine::Graphics::GShader>;
public:
    SpacialOffsetsCalculator() = default;
    void Initialize(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device);

    void Run(const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList, const BufferPointer& sortedKeys, const BufferPointer& offsets);
private:
    void CompileShaders();
    void CreateDescriptors(const BufferPointer& sortedKeys, const BufferPointer& offsets);
private:
    bool m_bDescriptorsInitialized = false;
    
    std::shared_ptr<PEPEngine::Graphics::GDevice> m_device;
    
    PEPEngine::Graphics::GRootSignature m_RootSignature;

    PEPEngine::Graphics::ComputePSO m_InitPSO;
    PEPEngine::Graphics::ComputePSO m_CalcPSO;

    ShaderPointer m_InitShader;
    ShaderPointer m_CalcShader;

    PEPEngine::Graphics::GDescriptor m_Descriptors;
};
