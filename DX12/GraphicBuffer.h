#pragma once
#include "d3dUtil.h"
#include "FrameResource.h"

template<class T>
class GraphicBuffer
{
protected:
	Microsoft::WRL::ComPtr<ID3DBlob> bufferCPU;
	Microsoft::WRL::ComPtr<ID3D12Resource> bufferGPU;
	Microsoft::WRL::ComPtr<ID3D12Resource> bufferUploader;
	UINT stride;
	UINT count;
	DWORD bufferSize = 0;

public:
	GraphicBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, T* data, UINT count) : count(count)
	{
		stride = sizeof(T);
		bufferSize = sizeof(T) * count;
		ThrowIfFailed(D3DCreateBlob(bufferSize, bufferCPU.GetAddressOf()));
		CopyMemory(bufferCPU->GetBufferPointer(), data, bufferSize);
		bufferGPU = d3dUtil::CreateDefaultBuffer(device, cmdList, data, bufferSize, bufferUploader);
	}

	GraphicBuffer(const GraphicBuffer<T>& rhs)
	{
		this->bufferCPU = rhs.bufferCPU;
		this->bufferGPU = rhs.bufferGPU;
		this->bufferUploader = rhs.bufferUploader;
		this->bufferSize = rhs.bufferSize;
		this->stride = rhs.stride;
		this->count = rhs.count;
	}

	GraphicBuffer<T>& operator=(const GraphicBuffer<T>& a)
	{
		this->bufferCPU = a.bufferCPU;
		this->bufferGPU = a.bufferGPU;
		this->bufferUploader = a.bufferUploader;
		this->bufferSize = a.bufferSize;
		this->stride = a.stride;
		this->count = a.count;
		return *this;
	}

	UINT GetElementsCount()
	{
		return count;
	}
	
	DWORD GetBufferSize() const
	{
		return bufferSize;
	}

	UINT GetStride() const
	{
		return stride;
	}

	ID3DBlob* GetCPUResource() const
	{
		return  bufferCPU.Get();
	}

	ID3D12Resource* GetGPUResource() const
	{
		return bufferGPU.Get();
	}
};

class IndexBuffer : public GraphicBuffer<DWORD>
{
protected:
	DXGI_FORMAT IndexFormat = DXGI_FORMAT_R32_UINT;

public:

	IndexBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, DWORD* data, DWORD count)
		: GraphicBuffer<DWORD>(device, cmdList, data, count)
	{
	}
	
	IndexBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, DXGI_FORMAT indexFormat, DWORD* data, DWORD count)
		: GraphicBuffer<DWORD>(device, cmdList, data, count), IndexFormat(indexFormat)
	{
	}

	explicit IndexBuffer(const GraphicBuffer<DWORD>& rhs)
		: GraphicBuffer<DWORD>(rhs)
	{
	}



	D3D12_INDEX_BUFFER_VIEW IndexBufferView()const
	{
		D3D12_INDEX_BUFFER_VIEW ibv;
		ibv.BufferLocation = bufferGPU->GetGPUVirtualAddress();
		ibv.Format = IndexFormat;
		ibv.SizeInBytes = bufferSize;

		return ibv;
	}
};

class VertexBuffer : public GraphicBuffer<Vertex>
{
public:
	VertexBuffer(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, Vertex* data, UINT count)
		: GraphicBuffer<Vertex>(device, cmdList, data, count)
	{
	}

	explicit VertexBuffer(const GraphicBuffer<Vertex>& rhs)
		: GraphicBuffer<Vertex>(rhs)
	{
	}

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView()const
	{
		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = bufferGPU->GetGPUVirtualAddress();
		vbv.StrideInBytes = stride;
		vbv.SizeInBytes = bufferSize;

		return vbv;
	}
};
