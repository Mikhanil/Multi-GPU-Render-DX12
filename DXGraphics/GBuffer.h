#pragma once

#include "GResource.h"

using namespace Microsoft::WRL;

class GCommandList;

class GBuffer : public GResource
{
protected:
	ComPtr<ID3DBlob> bufferCPU;
	UINT stride;
	UINT count;
	DWORD bufferSize = 0;
	const DXGI_FORMAT IndexFormat = DXGI_FORMAT_R32_UINT;

public:

	static GBuffer CreateBuffer(std::shared_ptr<GCommandList> cmdList, void* data, UINT elementSize, UINT count,
	                            const std::wstring& name = L"");

	GBuffer(const GBuffer& rhs) : GResource(rhs)
	{
		this->bufferCPU = rhs.bufferCPU;
		this->bufferSize = rhs.bufferSize;
		this->stride = rhs.stride;
		this->count = rhs.count;
	}

	GBuffer& operator=(const GBuffer& a)
	{
		GResource::operator=(a);
		this->bufferCPU = a.bufferCPU;
		this->bufferSize = a.bufferSize;
		this->stride = a.stride;
		this->count = a.count;
		return *this;
	}


	UINT GetElementsCount() const;

	DWORD GetBufferSize() const;

	UINT GetStride() const;

	ComPtr<ID3DBlob> GetCPUResource() const;

	D3D12_INDEX_BUFFER_VIEW IndexBufferView() const;

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const;

public:

	GBuffer(std::shared_ptr<GDevice> device, const std::wstring& name, const D3D12_RESOURCE_DESC& resourceDesc,
	        UINT elementSize, UINT elementCount,
	        void* data);

private:
};
