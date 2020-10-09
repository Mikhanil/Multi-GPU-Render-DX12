#pragma once


#include "d3dUtil.h"
#include "GResource.h"
using namespace Microsoft::WRL;

class ShaderBuffer : public GResource
{
public:
	ShaderBuffer(std::shared_ptr<GDevice> device, UINT elementCount, UINT elementByteSize, std::wstring name = L"");

	ShaderBuffer(const ShaderBuffer& rhs) = delete;
	ShaderBuffer& operator=(const ShaderBuffer& rhs) = delete;

	~ShaderBuffer();

	void CopyData(int elementIndex, const void* data, size_t size) const;

	D3D12_GPU_VIRTUAL_ADDRESS GetElementResourceAddress(UINT index = 0) const;

	UINT GetElementByteSize() const;

	UINT GetElementCount() const;

	void Reset() override;;

protected:
	D3D12_GPU_VIRTUAL_ADDRESS address;
	UINT elementCount = 0;
	BYTE* mappedData = nullptr;
	UINT elementByteSize = 0;
};

template <typename T>
class ConstantBuffer : public virtual ShaderBuffer
{
public:
	// Constant buffer elements need to be multiples of 256 bytes.
	// This is because the hardware can only view constant data
	// at m*256 byte offsets and of n*256 byte lengths.
	// typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC {
	//  UINT64 OffsetInBytes; // multiple of 256
	//  UINT  SizeInBytes;  // multiple of 256
	// } D3D12_CONSTANT_BUFFER_VIEW_DESC;
	ConstantBuffer(const std::shared_ptr<GDevice> device, UINT elementCount, std::wstring name = L"") : ShaderBuffer(
		device, elementCount, d3dUtil::CalcConstantBufferByteSize(sizeof(T)), name = L"")
	{
	}

	void CopyData(int elementIndex, const T& data)
	{
		ShaderBuffer::CopyData(elementIndex, &data, sizeof(T));
	}
};

template <typename T>
class UploadBuffer : public virtual ShaderBuffer
{
public:
	UploadBuffer(const std::shared_ptr<GDevice> device, UINT elementCount, std::wstring name = L"") : ShaderBuffer(
		device, elementCount, (sizeof(T)), name)
	{
	}

	void CopyData(int elementIndex, const T& data)
	{
		ShaderBuffer::CopyData(elementIndex, &data, sizeof(T));
	}
};
