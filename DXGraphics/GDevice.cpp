#include "d3dUtil.h"
#include "GDevice.h"

#include <dxgi1_5.h>

#include "GCommandQueue.h"
#include "MemoryAllocator.h"
#include "GAllocator.h"

ComPtr<IDXGIFactory4> GDevice::CreateFactory()
{
#if defined(DEBUG) || defined(_DEBUG)
	{
		ComPtr<ID3D12Debug> debugController;
		ThrowIfFailed(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)));
		debugController->EnableDebugLayer();

		ComPtr<ID3D12Debug1> spDebugController1;
		ThrowIfFailed(debugController->QueryInterface(IID_PPV_ARGS(&spDebugController1)));
		//spDebugController1->SetEnableGPUBasedValidation(true);
	}
#endif

	ComPtr<IDXGIFactory4> factory;

	UINT createFactoryFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
	createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&factory)));

	return factory;
}

custom_vector<ComPtr<IDXGIAdapter3>> GDevice::CreateAdapters()
{
	auto adapters = MemoryAllocator::CreateVector<ComPtr<IDXGIAdapter3>>();

	auto factory = GetFactory();

	UINT adapterindex = 0;
	ComPtr<IDXGIAdapter1> adapter;
	while (factory->EnumAdapters1(adapterindex++, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			continue;
		}

		ComPtr<IDXGIAdapter3> adapter3;
		ThrowIfFailed(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)));
		adapters.push_back((adapter3));
	}

	return adapters;
}

custom_vector<DXLib::Lazy<std::shared_ptr<GDevice>>> GDevice::CreateDevices()
{
	auto devices = MemoryAllocator::CreateVector<DXLib::Lazy<std::shared_ptr<GDevice>>>();

	for (UINT i = 0; i < adapters.size(); ++i)
	{
		auto index = i;
		auto adapter = adapters[i];

		devices.push_back(DXLib::Lazy<std::shared_ptr<GDevice>>([adapter, index]
		{
			return std::make_shared<GDevice>(adapter, index);
		}));
	}

	ComPtr<IDXGIAdapter1> adapter;
	if (devices.size() <= 1)
	{
		dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
		ComPtr<IDXGIAdapter3> adapter3;
		ThrowIfFailed(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)));
		adapters.push_back((adapter3));
		devices.push_back((DXLib::Lazy<std::shared_ptr<GDevice>>([adapter3]
		{
			return std::make_shared<GDevice>(adapter3, 1);
		})));
	}

	return devices;
}

bool GDevice::CheckTearingSupport()
{
	BOOL allowTearing = FALSE;
	ComPtr<IDXGIFactory5> factory5;
	if (SUCCEEDED(dxgiFactory.As(&factory5)))
	{
		factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
		                              &allowTearing, sizeof(allowTearing));
	}

	return allowTearing == TRUE;
}

ComPtr<IDXGIFactory4> GDevice::dxgiFactory = CreateFactory();
DXLib::Lazy<bool> GDevice::isTearingSupport = DXLib::Lazy<bool>(CheckTearingSupport);
custom_vector<ComPtr<IDXGIAdapter3>> GDevice::adapters = CreateAdapters();
custom_vector<DXLib::Lazy<std::shared_ptr<GDevice>>> GDevice::devices = CreateDevices();

UINT GDevice::GetNodeMask() const
{
	return nodeMask;
}

bool GDevice::IsCrossAdapterTextureSupported()
{
	return crossAdapterTextureSupport.value();
}

void GDevice::SharedFence(ComPtr<ID3D12Fence> primaryFence, const std::shared_ptr<GDevice> sharedDevice,
                          ComPtr<ID3D12Fence> sharedFence, UINT64 fenceValue, const SECURITY_ATTRIBUTES* attributes,
                          const DWORD access, const LPCWSTR name) const
{
	// Create fence for cross adapter resources
	ThrowIfFailed(device->CreateFence(fenceValue,
		D3D12_FENCE_FLAG_SHARED | D3D12_FENCE_FLAG_SHARED_CROSS_ADAPTER,
		IID_PPV_ARGS(&primaryFence)));

	const auto handle = SharedHandle(primaryFence, attributes, access, name);

	// Open shared handle to fence on secondaryDevice GPU
	ThrowIfFailed(sharedDevice->device->OpenSharedHandle(handle, IID_PPV_ARGS(&sharedFence)));
}

ComPtr<IDXGIFactory4> GDevice::GetFactory()
{
	return dxgiFactory;
}

std::shared_ptr<GDevice> GDevice::GetDevice(GraphicsAdapter adapter)
{
	return devices[adapter].value();
}

bool GDevice::IsTearingSupport()
{
	return isTearingSupport.value();
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

GDevice::GDevice(ComPtr<IDXGIAdapter3> adapter, UINT nodeMask) : adapter(adapter), nodeMask(nodeMask)
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
}

ComPtr<IDXGISwapChain4> GDevice::CreateSwapChain(DXGI_SWAP_CHAIN_DESC1& desc, const HWND hwnd) const
{
	ComPtr<IDXGISwapChain4> swapChain;

	desc.Flags = IsTearingSupport()
		             ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
		             : DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
		GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT)->GetD3D12CommandQueue().Get(),
		hwnd,
		&desc,
		nullptr,
		nullptr,
		&swapChain1));

	// Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
	// will be handled manually.
	ThrowIfFailed(dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain1.As(&swapChain));

	return swapChain;
}

ComPtr<ID3D12Device2> GDevice::GetDXDevice() const
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
				allocator.value()->ReleaseStaleDescriptors(frameCount);
			}
		}
	}
}

GMemory GDevice::AllocateDescriptors(const D3D12_DESCRIPTOR_HEAP_TYPE type, const uint32_t descriptorCount)
{
	auto allocator = graphicAllocators.value()[type];
	return allocator.value()->Allocate(descriptorCount);
}

custom_unordered_map<D3D12_DESCRIPTOR_HEAP_TYPE, UINT> descriptorHandlerSize = MemoryAllocator::CreateUnorderedMap<
	D3D12_DESCRIPTOR_HEAP_TYPE, UINT>();

std::shared_ptr<DXLib::GCommandQueue> GDevice::GetCommandQueue(const D3D12_COMMAND_LIST_TYPE type) const
{
	auto queue = queues.value()[type].value();

	return queue;
}

UINT GDevice::GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const
{
	const auto it = descriptorHandlerSize.find(type);

	if (it == descriptorHandlerSize.end())
	{
		const auto size = device->GetDescriptorHandleIncrementSize(type);
		descriptorHandlerSize[type] = size;
		return size;
	}

	return it->second;
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
