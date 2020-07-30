#include "GResource.h"

#include <utility>

#include "d3dApp.h"
#include "d3dUtil.h"

uint64_t GResource::resourceId = 0;

GResource::GResource(const std::wstring& name)
	: m_ResourceName(std::move(name))
{
	id = ++resourceId;
}

GResource::GResource(const D3D12_RESOURCE_DESC& resourceDesc, const D3D12_CLEAR_VALUE* clearValue,
                   const std::wstring& name)
{
	id = ++resourceId;
	
	auto& device = DXLib::D3DApp::GetApp().GetDevice();
	
	if (clearValue)
	{
		m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
	}

	ThrowIfFailed(device.CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COMMON,
		m_d3d12ClearValue.get(),
		IID_PPV_ARGS(&m_d3d12Resource)
	));

	SetName(name);
}

GResource::GResource(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, const std::wstring& name)
	: m_d3d12Resource(std::move(resource))
{
	id = ++resourceId;
	
	SetName(name);
}

GResource::GResource(const GResource& copy)
	: id(copy.id)
	  , m_d3d12Resource(copy.m_d3d12Resource)
	  , m_d3d12ClearValue(std::make_unique<D3D12_CLEAR_VALUE>(*copy.m_d3d12ClearValue))
	  , m_ResourceName(copy.m_ResourceName)
{
	
}

GResource::GResource(GResource&& move)
	: id(std::move(move.id))
	  , m_d3d12Resource(std::move(move.m_d3d12Resource))
	  , m_d3d12ClearValue(std::move(move.m_d3d12ClearValue))
, m_ResourceName(std::move(move.m_ResourceName))
{
}

GResource& GResource::operator=(const GResource& other)
{
	if (this != &other)
	{
		id = other.id;
		m_d3d12Resource = other.m_d3d12Resource;
		m_ResourceName = other.m_ResourceName;
		if (other.m_d3d12ClearValue)
		{
			m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>(*other.m_d3d12ClearValue);
		}
	}

	return *this;
}

GResource& GResource::operator=(GResource&& other) noexcept
{
	if (this != &other)
	{
		id = other.id;
		m_d3d12Resource = other.m_d3d12Resource;
		m_ResourceName = other.m_ResourceName;
		m_d3d12ClearValue = std::move(other.m_d3d12ClearValue);

		other.m_d3d12Resource.Reset();
		other.m_ResourceName.clear();
	}

	return *this;
}


GResource::~GResource()
{
	GResource::Reset();
}

bool GResource::IsValid() const
{
	return (m_d3d12Resource != nullptr);
}

Microsoft::WRL::ComPtr<ID3D12Resource> GResource::GetD3D12Resource() const
{
	return m_d3d12Resource;
}

D3D12_RESOURCE_DESC GResource::GetD3D12ResourceDesc() const
{
	D3D12_RESOURCE_DESC resDesc = {};
	if (m_d3d12Resource)
	{
		resDesc = m_d3d12Resource->GetDesc();
	}

	return resDesc;
}

void GResource::SetD3D12Resource(ComPtr<ID3D12Resource> d3d12Resource,
                                 const D3D12_CLEAR_VALUE* clearValue)
{
	m_d3d12Resource = std::move(d3d12Resource);
	if (m_d3d12ClearValue)
	{
		m_d3d12ClearValue.reset();
		m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
	}
	else
	{
		m_d3d12ClearValue = std::make_unique<D3D12_CLEAR_VALUE>(*clearValue);
	}
	
	SetName(m_ResourceName);
}

void GResource::SetName(const std::wstring& name)
{
	m_ResourceName = name;
	if (m_d3d12Resource && !m_ResourceName.empty())
	{
		m_d3d12Resource->SetName(m_ResourceName.c_str());
	}
}

void GResource::Reset()
{
	if(m_d3d12Resource)
	m_d3d12Resource.Reset();

	if(m_d3d12ClearValue)
	m_d3d12ClearValue.reset();
}
