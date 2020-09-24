#include "GDevice.h"
#include "d3dUtil.h"
#include "GAllocator.h"
#include "GCommandQueue.h"
#include "GResource.h"
#include "MemoryAllocator.h"


UINT GDevice::GetNodeMask() const
{
	return 0;
}

bool GDevice::IsCrossAdapterTextureSupported() const
{
	return crossAdapterTextureSupport.value();
}

void GDevice::SharedFence(ComPtr<ID3D12Fence>& primaryFence, const std::shared_ptr<GDevice> sharedDevice,
                          ComPtr<ID3D12Fence>& sharedFence, UINT64 fenceValue, const SECURITY_ATTRIBUTES* attributes,
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


void GDevice::InitialCommandQueue()
{
	queues = DXLib::Lazy<custom_vector<DXLib::Lazy<std::shared_ptr<DXLib::GCommandQueue>>>>([this]
	{
		auto shared = std::shared_ptr<GDevice>(this);
		auto queues = MemoryAllocator::CreateVector<DXLib::Lazy<std::shared_ptr<DXLib::GCommandQueue>>>();

		for (UINT i = 0; i != D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE; ++i)
		{
			D3D12_COMMAND_LIST_TYPE type = static_cast<D3D12_COMMAND_LIST_TYPE>(i);

			queues.push_back(DXLib::Lazy<std::shared_ptr<DXLib::GCommandQueue>>([type, shared]
			{
				return std::make_shared<DXLib::GCommandQueue>(shared, type);
			}));
		}
		return queues;
	});
}

void GDevice::InitialQueryTimeStamp()
{
	// Two timestamps for each frame.
	const UINT resultCount = 2 * globalCountFrameResources;
	const UINT resultBufferSize = resultCount * sizeof(UINT64);

	timestampResultBuffer = DXLib::Lazy<GResource>([resultBufferSize, this]
	{
		return GResource(std::shared_ptr<GDevice>(this), CD3DX12_RESOURCE_DESC::Buffer(resultBufferSize),
		                 name + L" TimestampBuffer", nullptr, D3D12_RESOURCE_STATE_COPY_DEST,
		                 CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK));
	});

	timestampQueryHeap = DXLib::Lazy<ComPtr<ID3D12QueryHeap>>([this, resultCount]
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

HANDLE GDevice::SharedHandle(ComPtr<ID3D12DeviceChild> deviceObject, const SECURITY_ATTRIBUTES* attributes = nullptr,
                             const DWORD access = GENERIC_ALL, const LPCWSTR name = L"") const
{
	HANDLE sharedHandle = nullptr;
	ThrowIfFailed(
		device->CreateSharedHandle(deviceObject.Get(), attributes, access, name, &sharedHandle));
	return sharedHandle;
}


void GDevice::ShareResource(GResource& resource, const std::shared_ptr<GDevice> destDevice, GResource& destResource,
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
	graphicAllocators = DXLib::Lazy<custom_vector<DXLib::Lazy<std::unique_ptr<GAllocator>>>>([this]
	{
		auto device = std::shared_ptr<GDevice>(this);
		auto adapterAllocators = MemoryAllocator::CreateVector<DXLib::Lazy<std::unique_ptr<GAllocator>>>();
		for (uint8_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
		{
			auto type = i;
			adapterAllocators.push_back(DXLib::Lazy<std::unique_ptr<GAllocator>>([type, device]
			{
				return std::make_unique<GAllocator>(device, static_cast<D3D12_DESCRIPTOR_HEAP_TYPE>(type));
			}));
		}

		return adapterAllocators;
	});
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

GDevice::GDevice(ComPtr<IDXGIAdapter3> adapter) : adapter(adapter)
{
	InitialDevice();

	InitialCommandQueue();

	InitialQueryTimeStamp();

	InitialDescriptorAllocator();

	crossAdapterTextureSupport = DXLib::Lazy<bool>([this]
	{
		D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
		ThrowIfFailed(
			device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, reinterpret_cast<void*>(&options), sizeof(options)
			));
		return options.CrossAdapterRowMajorTextureSupported;
	});

	crossAdapterTextureSupport.value();
}

ComPtr<ID3D12Device> GDevice::GetDXDevice() const
{
	return device;
}

GDevice::~GDevice()
{
	Flush();
	if (queues.IsInit())
	{
		for (auto&& queue : queues.value())
		{
			if (queue.IsInit())
			{
				queue.value().reset();
			}
		}
	}

	device->Release();
}

void GDevice::ResetAllocator(uint64_t frameCount)
{
	if (graphicAllocators.IsInit())
	{
		for (auto&& allocator : graphicAllocators.value())
		{
			if (allocator.IsInit())
			{
				uint64_t fenceValue = 0;

				for (auto&& queue : queues.value())
				{
					if(queue.IsInit())
					{
						fenceValue = std::max(fenceValue, queue.value()->GetFenceValue());
					}
				}
				
				allocator.value()->ReleaseStaleDescriptors(fenceValue);
			}
		}
	}
}

GMemory GDevice::AllocateDescriptors(const D3D12_DESCRIPTOR_HEAP_TYPE type, const uint32_t descriptorCount)
{
	auto allocator = graphicAllocators.value()[type];
	return allocator.value()->Allocate(descriptorCount);
}

std::shared_ptr<DXLib::GCommandQueue> GDevice::GetCommandQueue(const D3D12_COMMAND_LIST_TYPE type) const
{
	auto queue = queues.value()[type].value();

	return queue;
}

UINT GDevice::GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const
{
	const auto size = device->GetDescriptorHandleIncrementSize(type);
	return size;
}

void GDevice::Flush() const
{
	if (queues.IsInit())
	{
		for (auto&& queue : queues.value())
		{
			if (queue.IsInit())
			{
				queue.value()->Signal();
				queue.value()->Flush();
			}
		}
	}
}
