#include "DirectXBuffers.h"
#include "d3dApp.h"
#include "GResourceStateTracker.h"


ShaderBuffer::ShaderBuffer(UINT elementCount, UINT elementByteSize, std::wstring name):
GResource(CD3DX12_RESOURCE_DESC::Buffer(elementCount * elementByteSize), name, nullptr, D3D12_RESOURCE_STATE_GENERIC_READ, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD)), elementByteSize(elementByteSize), elementCount(elementCount)
{
	ThrowIfFailed(dxResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedData)));

	address = dxResource->GetGPUVirtualAddress();
}

ShaderBuffer::~ShaderBuffer()
{
	if (dxResource != nullptr)
		dxResource->Unmap(0, nullptr);
	mappedData = nullptr;
}

void ShaderBuffer::CopyData(int elementIndex, const void* data, size_t size) const
{
	memcpy(&mappedData[elementIndex * elementByteSize], data, size);
}

D3D12_GPU_VIRTUAL_ADDRESS ShaderBuffer::GetElementResourceAddress(UINT index) const
{
	return address + (elementByteSize * index);
}

UINT ShaderBuffer::GetElementByteSize() const
{
	return elementByteSize;
}

UINT ShaderBuffer::GetElementCount() const
{
	return elementCount;
}

void ShaderBuffer::Reset()
{
	GResource::Reset();
	address = 0;
}
