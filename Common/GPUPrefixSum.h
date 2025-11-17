#pragma once
#include "ComputePSO.h"
#include "GBuffer.h"
#include "GDescriptor.h"

class GPUPrefixSum
{
    enum RSSlots : UINT
    {
        ItemCountSlot,
        ElementsBufferSlot,
        GroupSumsBufferSlot,
        RSSlotsCount
    };
public:
    GPUPrefixSum() = default;
    
    void Initialize(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device, uint32_t count);
    
    void Run(
        const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList,
        const std::shared_ptr<PEPEngine::Graphics::GBuffer>& elements,
        bool isInitialRun = true
    );

private:
    void CompileShaders();

private:
    bool m_areDescriptorsAllocated = false;
    size_t m_particleCount = 0;
    
    int c_ThreadGroupSize{256};
    
    std::unordered_map<int, std::shared_ptr<PEPEngine::Graphics::GBuffer>> m_FreeBuffers;
    std::unordered_map<int, int> m_FreeBuffersOffsets;

    std::shared_ptr<PEPEngine::Graphics::GBuffer> m_InitialElementsBuffer;

    PEPEngine::Graphics::ComputePSO m_BlockScanPSO;
    PEPEngine::Graphics::ComputePSO m_BlockCombinePSO;

    std::shared_ptr<PEPEngine::Graphics::GShader> m_BlockScanShader;
    std::shared_ptr<PEPEngine::Graphics::GShader> m_BlockCombineShader;

    PEPEngine::Graphics::GRootSignature m_RootSignature;

    PEPEngine::Graphics::GDescriptor computeDescriptors;

    std::shared_ptr<PEPEngine::Graphics::GDevice> m_Device;
};
