#pragma once

#include "d3dUtil.h"

//template<typename T>
//class UploadBuffer
//{
//public:
//    UploadBuffer(ID3D12Device* device, UINT elementCount, bool isConstantBuffer) :
//        mIsConstantBuffer(isConstantBuffer)
//    {
//        mElementByteSize = sizeof(T);
//
//        // Constant buffer elements need to be multiples of 256 bytes.
//        // This is because the hardware can only view constant data 
//        // at m*256 byte offsets and of n*256 byte lengths. 
//        // typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC {
//        // UINT64 OffsetInBytes; // multiple of 256
//        // UINT   SizeInBytes;   // multiple of 256
//        // } D3D12_CONSTANT_BUFFER_VIEW_DESC;
//        if (isConstantBuffer)
//            mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));
//
//        ThrowIfFailed(device->CreateCommittedResource(
//            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
//            D3D12_HEAP_FLAG_NONE,
//            &CD3DX12_RESOURCE_DESC::Buffer(mElementByteSize * elementCount),
//            D3D12_RESOURCE_STATE_GENERIC_READ,
//            nullptr,
//            IID_PPV_ARGS(&mUploadBuffer)));
//
//        ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)));
//
//        // We do not need to unmap until we are done with the resource.  However, we must not write to
//        // the resource while it is in use by the GPU (so we must use synchronization techniques).
//    }
//
//    UINT GetElementByteSize() const
//	{
//		return mElementByteSize;
//	}
//	
//    UploadBuffer(const UploadBuffer& rhs) = delete;
//    UploadBuffer& operator=(const UploadBuffer& rhs) = delete;
//    ~UploadBuffer()
//    {
//        if (mUploadBuffer != nullptr)
//            mUploadBuffer->Unmap(0, nullptr);
//
//        mMappedData = nullptr;
//    }
//
//    ID3D12Resource* Resource()const
//    {
//        return mUploadBuffer.Get();
//    }
//
//    void CopyData(int elementIndex, const T& data)
//    {
//        memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
//    }
//
//private:
//    Microsoft::WRL::ComPtr<ID3D12Resource> mUploadBuffer;
//    BYTE* mMappedData = nullptr;
//
//    UINT mElementByteSize = 0;
//    bool mIsConstantBuffer = false;
//};


class DirectXBuffer
{
public:
	DirectXBuffer(ID3D12Device* device, UINT elementCount, UINT elementByteSize) : elementByteSize(elementByteSize)
	{
		ThrowIfFailed(device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(elementByteSize * elementCount),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&buffer)));

		ThrowIfFailed(buffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData)));
	}

	DirectXBuffer(const DirectXBuffer& rhs) = delete;
	DirectXBuffer& operator=(const DirectXBuffer& rhs) = delete;
	~DirectXBuffer()
	{
		if (buffer != nullptr)
			buffer->Unmap(0, nullptr);

		mappedData = nullptr;
	}

	ID3D12Resource* Resource()const
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
	
protected:

	Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
	BYTE* mappedData = nullptr;
	UINT elementByteSize = 0;
};

template<typename T>
class ConstantBuffer : public virtual DirectXBuffer
{
public:
	// Constant buffer elements need to be multiples of 256 bytes.
	// This is because the hardware can only view constant data
	// at m*256 byte offsets and of n*256 byte lengths.
	// typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC {
	//  UINT64 OffsetInBytes; // multiple of 256
	//  UINT  SizeInBytes;  // multiple of 256
	// } D3D12_CONSTANT_BUFFER_VIEW_DESC;
	ConstantBuffer(ID3D12Device* device, UINT elementCount) : DirectXBuffer(device, elementCount, d3dUtil::CalcConstantBufferByteSize(sizeof(T)))
	{
	}

	void CopyData(int elementIndex, const T& data)
	{
		DirectXBuffer::CopyData(elementIndex, &data, sizeof(T));
	}
};

template<typename T>
class UploadBuffer : public virtual DirectXBuffer
{
public:
	UploadBuffer(ID3D12Device* device, UINT elementCount) : DirectXBuffer(device, elementCount, (sizeof(T)))
	{
	}

	void CopyData(int elementIndex, const T& data)
	{
		DirectXBuffer::CopyData(elementIndex, &data, sizeof(T));
	}
};