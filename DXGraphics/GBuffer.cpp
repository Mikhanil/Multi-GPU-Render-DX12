#include "GBuffer.h"
#include "GCommandList.h"
#include "d3dcompiler.h"
#include "d3dUtil.h"

UINT GBuffer::GetElementsCount() const
{
	return count;
}

DWORD GBuffer::GetBufferSize() const
{
	return bufferSize;
}

UINT GBuffer::GetStride() const
{
	return stride;
}

ComPtr<ID3DBlob> GBuffer::GetCPUResource() const
{
	return bufferCPU;
}

D3D12_INDEX_BUFFER_VIEW GBuffer::IndexBufferView() const
{
	D3D12_INDEX_BUFFER_VIEW ibv;
	ibv.BufferLocation = dxResource->GetGPUVirtualAddress();
	ibv.Format = IndexFormat;
	ibv.SizeInBytes = bufferSize;
	return ibv;
}

D3D12_VERTEX_BUFFER_VIEW GBuffer::VertexBufferView() const
{
	D3D12_VERTEX_BUFFER_VIEW vbv;
	vbv.BufferLocation = dxResource->GetGPUVirtualAddress();
	vbv.StrideInBytes = stride;
	vbv.SizeInBytes = bufferSize;
	return vbv;
}


GBuffer GBuffer::CreateBuffer(std::shared_ptr<GCommandList> cmdList, void* data, UINT elementSize, UINT count,
                              const std::wstring& name)
{
	const auto desc = CD3DX12_RESOURCE_DESC::Buffer(elementSize * count);
	GBuffer buffer(cmdList->GetDevice(), name, desc, elementSize, count, data);

	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = data;
	subResourceData.RowPitch = buffer.bufferSize;
	subResourceData.SlicePitch = subResourceData.RowPitch;

	cmdList->UpdateSubresource(buffer, &subResourceData, 1);
	cmdList->TransitionBarrier(buffer, D3D12_RESOURCE_STATE_COMMON);
	cmdList->FlushResourceBarriers();

	return buffer;
}

GBuffer::GBuffer(const std::shared_ptr<GDevice> device, const std::wstring& name,
                 const D3D12_RESOURCE_DESC& resourceDesc, UINT elementSize, UINT elementCount,
                 void* data): GResource(device, resourceDesc, name), stride(elementSize), count(elementCount)
{
	bufferSize = stride * count;
	ThrowIfFailed(D3DCreateBlob(bufferSize, bufferCPU.GetAddressOf()));
	CopyMemory(bufferCPU->GetBufferPointer(), data, bufferSize);
}
