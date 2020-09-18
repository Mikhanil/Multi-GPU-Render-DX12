#include "d3dUtil.h"
#include "GDevice.h"
#include "GCommandQueue.h"
#include "DXAllocator.h"

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
	auto adapters = DXAllocator::CreateVector<ComPtr<IDXGIAdapter3>>();

	auto factory = GDevice::GetFactory();

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

		ComPtr<IDXGIAdapter3>  adapter3;
		ThrowIfFailed(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)));
		adapters.push_back((adapter3));
	}

	return adapters;
}

custom_vector<DXLib::Lazy<std::shared_ptr<GDevice>>> GDevice::CreateDevices()
{
	auto devices = DXAllocator::CreateVector<DXLib::Lazy<std::shared_ptr<GDevice>>>();

	for (auto && adapter : adapters)
	{
		devices.push_back((DXLib::Lazy<std::shared_ptr<GDevice>>([adapter]
			{
				return std::make_shared<GDevice>(adapter);
			})));
	}

	ComPtr<IDXGIAdapter1> adapter;
	if (devices.size() <= 1)
	{
		dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
		ComPtr<IDXGIAdapter3> adapter3;
		ThrowIfFailed(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)));
		adapters.push_back((adapter3));
		gdevices.push_back((DXLib::Lazy<std::shared_ptr<GDevice>>([adapter3]
			{
				return std::make_shared<GDevice>(adapter3);
			})));
	}
	
	return devices;
}

ComPtr<IDXGIFactory4> GDevice::dxgiFactory = CreateFactory();

custom_vector<ComPtr<IDXGIAdapter3>> GDevice::adapters = CreateAdapters();

custom_vector<DXLib::Lazy<std::shared_ptr<GDevice>>> GDevice::gdevices = CreateDevices();

bool GDevice::IsCrossAdapterTextureSupported()
{
	return crossAdapterTextureSupport.value();
}

ComPtr<IDXGIFactory4> GDevice::GetFactory()
{
	return dxgiFactory;
}

std::shared_ptr<GDevice> GDevice::GetDevice(GraphicsAdapter adapter)
{
	return gdevices[adapter].value();
}

void GDevice::InitialQueue()
{
	
	directCommandQueue = DXLib::Lazy< std::shared_ptr<DXLib::GCommandQueue>>([this]
	{
		auto shared = std::shared_ptr<GDevice>(this);
		return std::make_shared<DXLib::GCommandQueue>(shared, D3D12_COMMAND_LIST_TYPE_DIRECT);
	});
	computeCommandQueue = DXLib::Lazy< std::shared_ptr<DXLib::GCommandQueue>>([this]
	{
			auto shared = std::shared_ptr<GDevice>(this);
		return std::make_shared<DXLib::GCommandQueue>(shared, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	});
	copyCommandQueue = DXLib::Lazy< std::shared_ptr<DXLib::GCommandQueue>>([this]
	{
			auto shared = std::shared_ptr<GDevice>(this);
		return std::make_shared<DXLib::GCommandQueue>(shared, D3D12_COMMAND_LIST_TYPE_COPY);
	});
}

GDevice::GDevice(ComPtr<IDXGIAdapter3> adapter) : adapter(adapter)
{
	if (adapter == nullptr)
	{
		assert("Cant create device. Null Adapter");
	}

	DXGI_ADAPTER_DESC2 desc;
	ThrowIfFailed(adapter->GetDesc2(&desc));
	
	ThrowIfFailed(D3D12CreateDevice(
		adapter.Get(), // default adapter
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&device)));	
	
	ThrowIfFailed(device->SetName(desc.Description));
	
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

	InitialQueue();

	// Two timestamps for each frame.
	const UINT resultCount = 2 * globalCountFrameResources ;
	const UINT resultBufferSize = resultCount * sizeof(UINT64);
		

	timestampResultBuffer = DXLib::Lazy<GResource>([resultBufferSize, desc, this] 
		{return GResource(std::shared_ptr<GDevice>(this),CD3DX12_RESOURCE_DESC::Buffer(resultBufferSize), std::wstring(desc.Description) + L" TimestampBuffer", nullptr, D3D12_RESOURCE_STATE_COPY_DEST, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK));	});

	timestampQueryHeap = DXLib::Lazy<ComPtr<ID3D12QueryHeap>>([this, resultCount]
	{
			D3D12_QUERY_HEAP_DESC timestampHeapDesc = {};
			timestampHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
			timestampHeapDesc.Count = resultCount;
		
			ComPtr<ID3D12QueryHeap> heap;
			ThrowIfFailed(device->CreateQueryHeap(&timestampHeapDesc, IID_PPV_ARGS(&heap)));
			return heap;
	});
	
	

	crossAdapterTextureSupport = DXLib::Lazy<bool>([this]
	{
			D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
			ThrowIfFailed(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, reinterpret_cast<void*>(&options), sizeof(options)));
			return options.CrossAdapterRowMajorTextureSupported;
	});

	auto temp = crossAdapterTextureSupport.value();
}

ComPtr<ID3D12Device2> GDevice::GetDXDevice() const
{
	return device;
}

GDevice::~GDevice()
{
	Flush();

	if(directCommandQueue.IsInit())
	{
		directCommandQueue.value().reset();
	}
	if (copyCommandQueue.IsInit())
	{
		copyCommandQueue.value().reset();
	}
	if (computeCommandQueue.IsInit())
	{
		computeCommandQueue.value().reset();
	}
	
	device->Release();
}

custom_unordered_map<D3D12_DESCRIPTOR_HEAP_TYPE, UINT> descriptorHandlerSize = DXAllocator::CreateUnorderedMap<
	D3D12_DESCRIPTOR_HEAP_TYPE, UINT>();

std::shared_ptr<DXLib::GCommandQueue> GDevice::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
	std::shared_ptr<DXLib::GCommandQueue> queue;

	switch (type)
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT: queue = directCommandQueue.value();
		break;
	case D3D12_COMMAND_LIST_TYPE_COMPUTE: queue = computeCommandQueue.value();
		break;
	case D3D12_COMMAND_LIST_TYPE_COPY: queue = copyCommandQueue.value();
		break;
	default:;
	}

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
	if(directCommandQueue.IsInit())
	{
		directCommandQueue.value()->Signal();
		directCommandQueue.value()->Flush();
	}

	if (computeCommandQueue.IsInit())
	{
		computeCommandQueue.value()->Signal();
		computeCommandQueue.value()->Flush();
	}

	if (copyCommandQueue.IsInit())
	{
		copyCommandQueue.value()->Signal();
		copyCommandQueue.value()->Flush();
	}
	
}