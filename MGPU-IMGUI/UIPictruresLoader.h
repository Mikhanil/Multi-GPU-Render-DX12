#pragma once
#include "GTexture.h"
#include "imgui.h"

using namespace DirectX;

class UIPictruresLoader
{


public:
	UIPictruresLoader();
	~UIPictruresLoader();

	bool LoadTextureFromFile(const char* file_name, ID3D12Device* d3d_device, D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_handle, ID3D12Resource** out_tex_resource, int* out_width, int* out_height);
	void DestroyTexture(ID3D12Resource** tex_resources);
	bool LoadTextureFromMemory(const void* data, size_t data_size, ID3D12Device* d3d_device, D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_handle, ID3D12Resource** out_tex_resource, int* out_width, int* out_height);
};

struct ExampleDescriptorHeapAllocator
{
public:
    ExampleDescriptorHeapAllocator() : m_pHeap(nullptr), m_descriptorSize(0), m_nextFreeIndex(0) {}

    ~ExampleDescriptorHeapAllocator()
    {
        if (m_pHeap)
            m_pHeap->Release();
    }

    bool Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = type;
        heapDesc.NumDescriptors = numDescriptors;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_pHeap));
        if (FAILED(hr))
            return false;

        m_descriptorSize = device->GetDescriptorHandleIncrementSize(type);
        m_nextFreeIndex = 0;
        m_maxCount = numDescriptors;

        return true;
    }

    void Alloc(D3D12_CPU_DESCRIPTOR_HANDLE* outCpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* outGpuHandle)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        assert(m_pHeap != nullptr && "Heap allocator not initialized or failed.");
        assert(m_nextFreeIndex < m_maxCount && "Descriptor heap is full!");

        D3D12_CPU_DESCRIPTOR_HANDLE baseCpuHandle = m_pHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_GPU_DESCRIPTOR_HANDLE baseGpuHandle = m_pHeap->GetGPUDescriptorHandleForHeapStart();

        baseCpuHandle.ptr += m_nextFreeIndex * m_descriptorSize;
        baseGpuHandle.ptr += m_nextFreeIndex * m_descriptorSize;

        *outCpuHandle = baseCpuHandle;
        *outGpuHandle = baseGpuHandle;

        ++m_nextFreeIndex;
    }

private:
    ID3D12DescriptorHeap* m_pHeap;
    UINT m_descriptorSize;
    UINT m_nextFreeIndex;
    UINT m_maxCount;

    std::mutex m_mutex; // For thread safety
};


