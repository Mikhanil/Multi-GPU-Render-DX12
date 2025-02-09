#pragma once
#include <array>
#include <d3d12.h>
#include <wrl/client.h>
#include "dxgi1_6.h"
#include "Lazy.h"
#include "MemoryAllocator.h"

using namespace Microsoft::WRL;

namespace PEPEngine::Graphics
{
    using namespace Allocator;
    using namespace Utils;

    class GCommandQueue;
    class GResource;
    class GAllocator;
    class GDeviceFactory;
    class GDescriptor;

    enum class GQueueType
    {
        Graphics,
        Compute,
        Copy,
        Count
    };

    class GDevice : std::enable_shared_from_this<GDevice>
    {
        ComPtr<ID3D12Device> device;
        ComPtr<IDXGIAdapter3> adapter;


        std::array<std::shared_ptr<GAllocator>, D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES> graphicAllocators;
        std::array<std::shared_ptr<GCommandQueue>, static_cast<int>(GQueueType::Count)> queues;

        bool crossAdapterTextureSupport;

        // Get the timestamp values from the result buffers.
        D3D12_RANGE readRange = {};
        const D3D12_RANGE emptyRange = {};

        void* mappedData = nullptr;
        Lazy<GResource> timestampResultBuffer;
        Lazy<ComPtr<ID3D12QueryHeap>> timestampQueryHeap;

        std::wstring name;

        friend GDeviceFactory;


        void InitialDescriptorAllocator();
        void InitialCommandQueue();
        void InitialQueryTimeStamp();
        void InitialDevice();

        bool isInitialized = false;
    public:
        GDevice(const ComPtr<IDXGIAdapter3>& adapter);

        ~GDevice();

        bool IsInitialized() const {return isInitialized;}
        void Initialize();
        
        HANDLE SharedHandle(const ComPtr<ID3D12DeviceChild>& deviceObject, const SECURITY_ATTRIBUTES* attributes,
                            DWORD access,
                            LPCWSTR name) const;

        void ShareResource(const GResource& resource, const std::shared_ptr<GDevice>& destDevice, GResource& destResource,
                           const SECURITY_ATTRIBUTES* attributes = nullptr,
                           DWORD access = GENERIC_ALL, LPCWSTR name = L"") const;


        void ResetAllocators(uint64_t frameCount);

        GDescriptor AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t descriptorCount = 1);

        UINT GetNodeMask() const;


        bool IsCrossAdapterTextureSupported() const;

        void SharedFence(ComPtr<ID3D12Fence>& primaryFence, const std::shared_ptr<GDevice>& sharedDevice,
                         ComPtr<ID3D12Fence>& sharedFence, UINT64 fenceValue = 0,
                         const SECURITY_ATTRIBUTES* attributes = nullptr,
                         DWORD access = GENERIC_ALL, LPCWSTR name = L"") const;


        std::shared_ptr<GCommandQueue> GetCommandQueue(
            GQueueType type = GQueueType::Graphics) const;

        UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

        void Flush();

        void TerminatedQueuesWorker();

        ComPtr<ID3D12Device> GetDXDevice() const;

        std::wstring GetName() const
        {
            return name;
        }
    };
}
