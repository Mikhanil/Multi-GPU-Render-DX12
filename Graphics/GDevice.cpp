#include "GDevice.h"


#include "d3dUtil.h"
#include "GAllocator.h"
#include "GCommandQueue.h"
#include "GResource.h"
#include "MemoryAllocator.h"

namespace PEPEngine::Graphics
{
    using namespace Utils;

    UINT GDevice::GetNodeMask() const
    {
        return 0;
    }

    bool GDevice::IsCrossAdapterTextureSupported() const
    {
        return crossAdapterTextureSupport;
    }

    void GDevice::SharedFence(ComPtr<ID3D12Fence>& primaryFence, const std::shared_ptr<GDevice>& sharedDevice,
                              ComPtr<ID3D12Fence>& sharedFence, const UINT64 fenceValue,
                              const SECURITY_ATTRIBUTES* attributes,
                              const DWORD access, const LPCWSTR name) const
    {
        // Create fence for cross adapter resources
        ThrowIfFailed(device->CreateFence(fenceValue,
            D3D12_FENCE_FLAG_SHARED | D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER,
            IID_PPV_ARGS(primaryFence.GetAddressOf())));

        const auto handle = SharedHandle(primaryFence, attributes, access, name);

        // Open shared handle to fence on secondaryDevice GPU
        ThrowIfFailed(sharedDevice->device->OpenSharedHandle(handle, IID_PPV_ARGS(sharedFence.GetAddressOf())));

        CloseHandle(handle);
    }

    static D3D12_COMMAND_LIST_TYPE GQueueTypeToCommandListType(GQueueType queueType)
    {
        switch (queueType)
        {
        case GQueueType::Graphics:
            return D3D12_COMMAND_LIST_TYPE_DIRECT;
        case GQueueType::Compute:
            return D3D12_COMMAND_LIST_TYPE_COMPUTE;
        case GQueueType::Copy:
            return D3D12_COMMAND_LIST_TYPE_COPY;
        case GQueueType::Count:
            break;
        default: break;
        }
        return D3D12_COMMAND_LIST_TYPE_DIRECT;
    }


    void GDevice::InitialCommandQueue()
    {
        auto Device = std::shared_ptr<GDevice>(this);
        for (UINT i = 0; i != static_cast<UINT>(GQueueType::Count); ++i)
        {
            queues[i] = std::make_shared<GCommandQueue>(Device, GQueueTypeToCommandListType(static_cast<GQueueType>(i)));
        }
    }

    void GDevice::InitialQueryTimeStamp()
    {
        // Two timestamps for each frame.
        constexpr UINT resultCount = 2 * globalCountFrameResources;
        constexpr UINT resultBufferSize = resultCount * sizeof(UINT64);

        timestampResultBuffer = Lazy<GResource>([resultBufferSize, this]
        {
            return GResource(std::shared_ptr<GDevice>(this), CD3DX12_RESOURCE_DESC::Buffer(resultBufferSize),
                             name + L" TimestampBuffer", nullptr, D3D12_RESOURCE_STATE_COPY_DEST,
                             CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK));
        });

        timestampQueryHeap = Lazy<ComPtr<ID3D12QueryHeap>>([this, resultCount]
        {
            D3D12_QUERY_HEAP_DESC timestampHeapDesc = {};
            timestampHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            timestampHeapDesc.Count = resultCount;
            timestampHeapDesc.NodeMask = GetNodeMask();

            ComPtr<ID3D12QueryHeap> heap;
            ThrowIfFailed(device->CreateQueryHeap(&timestampHeapDesc, IID_PPV_ARGS(&heap)));
            return heap;
        });
    }

    HANDLE GDevice::SharedHandle(const ComPtr<ID3D12DeviceChild>& deviceObject,
                                 const SECURITY_ATTRIBUTES* attributes = nullptr,
                                 const DWORD access = GENERIC_ALL, const LPCWSTR name = L"") const
    {
        HANDLE sharedHandle = nullptr;
        ThrowIfFailed(
            device->CreateSharedHandle(deviceObject.Get(), attributes, access, name, &sharedHandle));
        return sharedHandle;
    }


    void GDevice::ShareResource(const GResource& resource, const std::shared_ptr<GDevice>& destDevice,
                                GResource& destResource,
                                const SECURITY_ATTRIBUTES* attributes, const DWORD access, const LPCWSTR name) const
    {
        const HANDLE handle = SharedHandle(resource.GetD3D12Resource(), attributes, access, name);

        ComPtr<ID3D12Resource> sharedResource;
        ThrowIfFailed(destDevice->device->OpenSharedHandle(handle, IID_PPV_ARGS(&sharedResource)));

        destResource.SetD3D12Resource(destDevice, sharedResource);

        CloseHandle(handle);
    }

    void GDevice::InitialDescriptorAllocator()
    {
        auto Device = std::shared_ptr<GDevice>(this);
        for (UINT i = 0; i != D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
        {
            graphicAllocators[i] = std::make_shared<GAllocator>(Device,
                                                                static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(i));
        }
    }

    void GDevice::InitialDevice()
    {
        if (adapter == nullptr)
        {
            assert("Cant create device. Null Adapter");
        }

        DXGI_ADAPTER_DESC2 desc;
        ThrowIfFailed(adapter->GetDesc2(&desc));

        ThrowIfFailed(D3D12CreateDevice(
            adapter.Get(),
            D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device)));

        ThrowIfFailed(device->SetName(desc.Description));

        name = std::wstring(desc.Description);

#if defined(DEBUG) || defined(_DEBUG)

        ComPtr<ID3D12InfoQueue> pInfoQueue;
        if (SUCCEEDED(device.As(&pInfoQueue)))
        {
            ThrowIfFailed(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
            ThrowIfFailed(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
            ThrowIfFailed(pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE));


            // Suppress messages based on their severity level
            D3D12_MESSAGE_SEVERITY Severities[] =
            {
                D3D12_MESSAGE_SEVERITY_INFO
            };

            // Suppress individual messages by their ID
            D3D12_MESSAGE_ID DenyIds[] = {
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                // I'm really not sure how to avoid this message.
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
                // This warning occurs when using capture frame while graphics debugging.
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
                // This warning occurs when using capture frame while graphics debugging.
            };

            D3D12_INFO_QUEUE_FILTER NewFilter = {};
            NewFilter.DenyList.NumSeverities = _countof(Severities);
            NewFilter.DenyList.pSeverityList = Severities;
            NewFilter.DenyList.NumIDs = _countof(DenyIds);
            NewFilter.DenyList.pIDList = DenyIds;

            ThrowIfFailed(pInfoQueue->PushStorageFilter(&NewFilter));
        }
#endif
    }

    void GDevice::Initialize()
    {
        if (isInitialized) return;

        InitialDevice();

        InitialCommandQueue();

        InitialQueryTimeStamp();

        InitialDescriptorAllocator();

        D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
        ThrowIfFailed(
            device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(
                    options)
            ));

        crossAdapterTextureSupport = options.CrossAdapterRowMajorTextureSupported;

        isInitialized = true;
    }

    GDevice::GDevice(const ComPtr<IDXGIAdapter3>& adapter) : adapter(adapter), crossAdapterTextureSupport(false)
    {
    }

    ComPtr<ID3D12Device> GDevice::GetDXDevice() const
    {
        return device;
    }

    GDevice::~GDevice()
    {
        Flush();

        TerminatedQueuesWorker();
        for (auto&& queue : queues)
        {
            queue->Flush();
        }

        std::abort();

        if (device)
            device->Release();

        if (adapter)
            adapter->Release();
    }


    void GDevice::ResetAllocators(uint64_t frameCount)
    {
        for (auto& allocator : graphicAllocators)
        {
            uint64_t fenceValue = 0;

            for (auto&& queue : queues)
            {
                fenceValue = std::max(fenceValue, queue->GetFenceValue());
            }

            allocator->ReleaseStaleDescriptors(fenceValue);
        }
    }

    GDescriptor GDevice::AllocateDescriptors(const D3D12_DESCRIPTOR_HEAP_TYPE type, const uint32_t descriptorCount)
    {
        return graphicAllocators[type]->Allocate(descriptorCount);
    }

    std::shared_ptr<GCommandQueue> GDevice::GetCommandQueue(const GQueueType type) const
    {
        return queues[static_cast<int>(type)];
    }

    UINT GDevice::GetDescriptorHandleIncrementSize(const D3D12_DESCRIPTOR_HEAP_TYPE type) const
    {
        const auto size = device->GetDescriptorHandleIncrementSize(type);
        return size;
    }

    void GDevice::Flush()
    {
        for (auto&& queue : queues)
        {
            queue->Signal();
            queue->Flush();
        }
        ResetAllocators(0);
    }

    void GDevice::TerminatedQueuesWorker()
    {
        for (auto&& queue : queues)
        {
            queue->HardStop();
        }
    }
}
