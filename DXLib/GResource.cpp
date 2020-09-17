#include "GResource.h"

#include <utility>
#include "GMemory.h"

#include "d3dApp.h"
#include "d3dUtil.h"
#include "GResourceStateTracker.h"

uint64_t GResource::resourceId = 0;

GResource::GResource(const std::wstring& name)
	: resourceName(std::move(name))
{
	id = ++resourceId;
}

GResource::GResource(const D3D12_RESOURCE_DESC& resourceDesc, const std::wstring& name, const D3D12_CLEAR_VALUE* clearValue, D3D12_RESOURCE_STATES initState, D3D12_HEAP_PROPERTIES heapProp)
{
	id = ++resourceId;
	
	auto& device = DXLib::D3DApp::GetApp().GetDevice();
	
	if (clearValue)
	{
		this->clearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
	}

	ThrowIfFailed(device.CreateCommittedResource(
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		initState,
		this->clearValue.get(),
		IID_PPV_ARGS(&dxResource)
	));

	GResourceStateTracker::AddCurrentState(dxResource.Get(), initState);
	
	SetName(name);

}

GResource::GResource(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, const std::wstring& name)
	: dxResource(std::move(resource))
{
	id = ++resourceId;	
	SetName(name);
}

GResource::GResource(const GResource& copy)
	: id(copy.id)
	  , dxResource(copy.dxResource)
	  , resourceName(copy.resourceName)
{

	if (copy.clearValue)
		clearValue = (std::make_unique<D3D12_CLEAR_VALUE>(*copy.clearValue.get()));
	else
		clearValue = nullptr;

}

GResource::GResource(GResource&& move)
	: id(std::move(move.id))
	  , dxResource(std::move(move.dxResource))
	  , clearValue(std::move(move.clearValue))
, resourceName(std::move(move.resourceName))
{
}

GResource& GResource::operator=(const GResource& other)
{
	if (this != &other)
	{
		id = other.id;
		dxResource = other.dxResource;
		resourceName = other.resourceName;
		if (other.clearValue)
		{
			clearValue = std::make_unique<D3D12_CLEAR_VALUE>(*other.clearValue);
		}

	}
	return *this;
}

GResource& GResource::operator=(GResource&& other) noexcept
{
	if (this != &other)
	{
		id = other.id;
		dxResource = other.dxResource;
		resourceName = other.resourceName;
		clearValue = std::move(other.clearValue);
		other.dxResource.Reset();
		other.resourceName.clear();

	}
	return *this;
}

GResource::~GResource()
{
	GResource::Reset();
}

bool GResource::IsValid() const
{
	return (dxResource != nullptr);
}

Microsoft::WRL::ComPtr<ID3D12Resource> GResource::GetD3D12Resource() const
{
	return dxResource;
}


D3D12_RESOURCE_DESC GResource::GetD3D12ResourceDesc() const
{
	D3D12_RESOURCE_DESC resDesc = {};
	if (dxResource)
	{
		resDesc = dxResource->GetDesc();
	}

	return resDesc;
}

void GResource::SetD3D12Resource(ComPtr<ID3D12Resource> d3d12Resource,
                                 const D3D12_CLEAR_VALUE* clearValue)
{
	dxResource = std::move(d3d12Resource);
	
	if (clearValue)
	{
		this->clearValue.reset();
		this->clearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
	}
	
	SetName(resourceName);
}


void GResource::CreateShaderResourceView(const D3D12_SHADER_RESOURCE_VIEW_DESC* srvDesc, GMemory* memory, size_t offset) const
{
	auto& app = DXLib::D3DApp::GetApp();
	auto& device = app.GetDevice();
	device.CreateShaderResourceView(dxResource.Get(), srvDesc, memory->GetCPUHandle(offset));

}

void GResource::CreateUnorderedAccessView(const D3D12_UNORDERED_ACCESS_VIEW_DESC* uavDesc, GMemory* memory, size_t offset) const
{
	auto& app = DXLib::D3DApp::GetApp();
	auto& device = app.GetDevice();

	device.CreateUnorderedAccessView(dxResource.Get(), nullptr, uavDesc, memory->GetCPUHandle(offset));

}

void GResource::CreateRenderTargetView(const D3D12_RENDER_TARGET_VIEW_DESC* rtvDesc, GMemory* memory, size_t offset) const
{
	auto& app = DXLib::D3DApp::GetApp();
	auto& device = app.GetDevice();

	device.CreateRenderTargetView(dxResource.Get(), rtvDesc, memory->GetCPUHandle(offset));

}

void GResource::CreateDepthStencilView(const D3D12_DEPTH_STENCIL_VIEW_DESC* dsvDesc, GMemory* memory, size_t offset) const
{
	auto& app = DXLib::D3DApp::GetApp();
	auto& device = app.GetDevice();
	device.CreateDepthStencilView(dxResource.Get(), dsvDesc, memory->GetCPUHandle(offset));
}

void GResource::SetName(const std::wstring& name)
{
	resourceName = name;
	if (dxResource && !resourceName.empty())
	{
		dxResource->SetName(resourceName.c_str());
	}
}



void GResource::Reset()
{
	if (dxResource)
	{
		dxResource.Reset();
	}

	
	
	if (clearValue)
	{
		clearValue.reset();
	}
}
