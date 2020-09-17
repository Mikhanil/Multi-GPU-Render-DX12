#include "GDevice.h"
#include "GCommandQueue.h"
#include "DXAllocator.h"
GDevice::GDevice(ComPtr<IDXGIAdapter3> adapter) : adapter(adapter)
{
	if(adapter == nullptr)
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


	directCommandQueue = std::make_shared<DXLib::GCommandQueue>(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
	computeCommandQueue = std::make_shared<DXLib::GCommandQueue>(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
	copyCommandQueue = std::make_shared<DXLib::GCommandQueue>(device, D3D12_COMMAND_LIST_TYPE_COPY);
	
}

ComPtr<ID3D12Device2> GDevice::GetDevice() const
{
	return device;
}

GDevice::~GDevice()
{
	Flush();
	directCommandQueue.reset();
	computeCommandQueue.reset();
	copyCommandQueue.reset();
	device->Release();
}

custom_unordered_map<D3D12_DESCRIPTOR_HEAP_TYPE, UINT> descriptorHandlerSize = DXAllocator::CreateUnorderedMap<
	D3D12_DESCRIPTOR_HEAP_TYPE, UINT>();

std::shared_ptr<DXLib::GCommandQueue> GDevice::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
{
	std::shared_ptr<DXLib::GCommandQueue> queue;

	switch (type)
	{
	case D3D12_COMMAND_LIST_TYPE_DIRECT: queue = directCommandQueue;
		break;
	case D3D12_COMMAND_LIST_TYPE_COMPUTE: queue = computeCommandQueue;
		break;
	case D3D12_COMMAND_LIST_TYPE_COPY: queue = copyCommandQueue;
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
	directCommandQueue->Signal();
	directCommandQueue->Flush();

	computeCommandQueue->Signal();
	computeCommandQueue->Flush();

	copyCommandQueue->Signal();
	copyCommandQueue->Flush();
}