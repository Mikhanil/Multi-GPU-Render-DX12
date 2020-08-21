#pragma once

#include "d3dUtil.h"
using namespace Microsoft::WRL;
class ShaderBuffer
{
public:
	ShaderBuffer(UINT elementCount, UINT elementByteSize);

	ShaderBuffer(const ShaderBuffer& rhs) = delete;
	ShaderBuffer& operator=(const ShaderBuffer& rhs) = delete;

	~ShaderBuffer()
	{
		if (isDispose)
		{
			return;
		}

		if (buffer != nullptr)
			buffer->Unmap(0, nullptr);

		mappedData = nullptr;
		isDispose = true;
	}

	ID3D12Resource* Resource() const
	{
		return buffer.Get();
	}

	void CopyData(int elementIndex, const void* data, size_t size) const
	{
		memcpy(&mappedData[elementIndex * elementByteSize], data, size);
	}

	UINT GetElementByteSize() const
	{
		return elementByteSize;
	}

	UINT GetElementCount()
	{
		return elementCount;
	}
	
protected:
	UINT elementCount;
	bool isDispose = false;
	ComPtr<ID3D12Resource> buffer;
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
	ConstantBuffer(UINT elementCount) : ShaderBuffer(elementCount, d3dUtil::CalcConstantBufferByteSize(sizeof(T)))
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
	UploadBuffer(UINT elementCount) : ShaderBuffer(elementCount, (sizeof(T)))
	{
	}

	void CopyData(int elementIndex, const T& data)
	{
		ShaderBuffer::CopyData(elementIndex, &data, sizeof(T));
	}
};
