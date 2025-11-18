#pragma once
#include "GPUCountingSort.h"
#include "SpacialOffsetsCalculator.h"

class SpatialHash
{
    using BufferPointer = std::shared_ptr<PEPEngine::Graphics::GBuffer>;
public:
    SpatialHash() = default;
    
    void Initialize(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device, size_t count);

    void Resize(size_t newSize);

    void Run(const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList);

    BufferPointer GetSpatialKeys() const { return m_SpatialKeys; }
    BufferPointer GetSpatialIndices() const { return m_SpatialIndices; }
    BufferPointer GetSpatialOffsets() const { return m_SpatialOffsets; }
private:
    void CreateBuffers(size_t count);
    bool TryCreateBuffer(BufferPointer& buffer, UINT count, UINT stride, const std::wstring& name);

private:
    std::shared_ptr<PEPEngine::Graphics::GDevice> m_Device;

    BufferPointer m_SpatialKeys;
    BufferPointer m_SpatialIndices;
    BufferPointer m_SpatialOffsets;

    GPUCountingSort m_CountingSort;
    SpacialOffsetsCalculator m_OffsetsCalculator;
};
