#include "pch.h"
#include "SpacialHash.h"

void SpatialHash::Initialize(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device, size_t count)
{
    m_Device = device;

    m_CountingSort.Initialize(device, count);
    m_OffsetsCalculator.Initialize(device);
    
    CreateBuffers(count);
}

void SpatialHash::Resize(size_t newSize)
{
    CreateBuffers(newSize);
}

void SpatialHash::Run(const std::shared_ptr<PEPEngine::Graphics::GCommandList>& commandList)
{
    m_CountingSort.Run(commandList, m_SpatialIndices, m_SpatialKeys, (UINT)(m_SpatialKeys->GetElementCount() - 1));
    m_OffsetsCalculator.Run(commandList, m_SpatialKeys, m_SpatialOffsets);
}

void SpatialHash::CreateBuffers(size_t count)
{
    TryCreateBuffer(m_SpatialKeys, count, sizeof(UINT));
    TryCreateBuffer(m_SpatialIndices, count, sizeof(UINT));
    TryCreateBuffer(m_SpatialOffsets, count, sizeof(UINT));
}

bool SpatialHash::TryCreateBuffer(BufferPointer& buffer, UINT count, UINT stride)
{
    
    bool createNewBuffer = buffer == nullptr
                        || !buffer->IsValid()
                        || buffer->GetStride() != stride
                        || buffer->GetElementCount() != count;
    if (createNewBuffer)
    {
        if (buffer && buffer->IsValid())
            buffer->Reset();
        buffer = std::make_shared<PEPEngine::Graphics::GBuffer>(m_Device, stride, count, L"", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        return true;
    }
    return false;
}
