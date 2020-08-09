#include "GBuffer.h"


GVertexBuffer::GVertexBuffer(const std::wstring& name)
    : GBuffer(name)
    , VertexCount(0)
    , VertexSize(0)
    , vertexBufferView({})
{}

GVertexBuffer::~GVertexBuffer()
= default;

void GVertexBuffer::CreateViews(size_t elementCount, size_t elementSize)
{
    VertexCount = elementCount;
    VertexSize = elementSize;

    vertexBufferView.BufferLocation = dxResource->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = static_cast<UINT>(VertexCount * VertexSize);
    vertexBufferView.StrideInBytes = static_cast<UINT>(VertexSize);
}

D3D12_VERTEX_BUFFER_VIEW GVertexBuffer::GetVertexBufferView() const
{
	return vertexBufferView;
}

size_t GVertexBuffer::GetVertexCount() const
{
	return VertexCount;
}

size_t GVertexBuffer::GetVertexSize() const
{
	return VertexSize;
}

DescriptorHandle GVertexBuffer::GetShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc) const
{
    throw std::exception("GVertexBuffer::GetShaderResourceView should not be called.");
}

DescriptorHandle GVertexBuffer::GetUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc) const
{
    throw std::exception("GVertexBuffer::GetUnorderedAccessView should not be called.");
}
