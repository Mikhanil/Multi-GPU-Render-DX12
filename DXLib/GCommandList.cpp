#include "GCommandList.h"



#include "d3dApp.h"
#include "d3dUtil.h"
#include "GDataUploader.h"
#include "GResource.h"
#include "GResourceStateTracker.h"
#include "GMemory.h"
#include "PSO.h"
#include "RootSignature.h"

custom_map<std::wstring, ID3D12Resource* > GCommandList::ms_TextureCache = DXAllocator::CreateMap<std::wstring, ID3D12Resource* >();
std::mutex GCommandList::ms_TextureCacheMutex;

void GCommandList::TrackResource(Microsoft::WRL::ComPtr<ID3D12Object> object)
{
    m_TrackedObjects.push_back(object);
}

void GCommandList::TrackResource(const GResource& res)
{
    TrackResource(res.GetD3D12Resource());
}

GCommandList::GCommandList(D3D12_COMMAND_LIST_TYPE type)
    : m_d3d12CommandListType(type)
{
    auto& device = DXLib::D3DApp::GetApp().GetDevice();

    ThrowIfFailed(device.CreateCommandAllocator(m_d3d12CommandListType, IID_PPV_ARGS(&m_d3d12CommandAllocator)));

    ThrowIfFailed(device.CreateCommandList(0, m_d3d12CommandListType, m_d3d12CommandAllocator.Get(),
        nullptr, IID_PPV_ARGS(&m_d3d12CommandList)));

    m_UploadBuffer = std::make_unique<GDataUploader>();

    m_ResourceStateTracker = std::make_unique<GResourceStateTracker>();

}

GCommandList::~GCommandList()
{}

D3D12_COMMAND_LIST_TYPE GCommandList::GetCommandListType() const
{
	return m_d3d12CommandListType;
}

ComPtr<ID3D12GraphicsCommandList2> GCommandList::GetGraphicsCommandList() const
{
	return m_d3d12CommandList;
}

void GCommandList::SetGMemory(GMemory** memory, size_t count) const
{
	std::vector<ID3D12DescriptorHeap*> heaps(count);

	for (int i = 0; i < count; ++i)
	{
		heaps[i] = memory[i]->GetDescriptorHeap();
	}

    m_d3d12CommandList->SetDescriptorHeaps(heaps.size(), heaps.data());

}

void GCommandList::SetRootSignature(RootSignature* signature)
{
	const auto d3d12RootSignature = signature->GetRootSignature();
	
    if(setedRootSignature == d3d12RootSignature) return;
    
    setedRootSignature = d3d12RootSignature;
	
	if(m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
	{
        m_d3d12CommandList->SetGraphicsRootSignature(setedRootSignature.Get());
	}

	if(m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
	{
        m_d3d12CommandList->SetComputeRootSignature(setedRootSignature.Get());
	}
	
    TrackResource(setedRootSignature);
}

void GCommandList::SetRootShaderResourceView(UINT rootSignatureSlot, GResource& resource)
{
    if (setedRootSignature == nullptr) assert("Root Signature not set into the CommandList");

    if (!resource.IsValid()) assert("Resource is invalid");
	
    auto res = resource.GetD3D12Resource();   
	
	if(m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
	{		
        m_d3d12CommandList->SetGraphicsRootShaderResourceView(rootSignatureSlot, res->GetGPUVirtualAddress());
	}

	if(m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
	{
        m_d3d12CommandList->SetComputeRootShaderResourceView(rootSignatureSlot, res->GetGPUVirtualAddress());
	}

    TrackResource(resource);
}

void GCommandList::SetRootConstantBufferView(UINT rootSignatureSlot, GResource& resource)
{
    if (setedRootSignature == nullptr) assert("Root Signature not set into the CommandList");

    if (!resource.IsValid()) assert("Resource is invalid");

    auto res = resource.GetD3D12Resource();
	
    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        m_d3d12CommandList->SetGraphicsRootConstantBufferView(rootSignatureSlot, res->GetGPUVirtualAddress());
    }

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        m_d3d12CommandList->SetComputeRootConstantBufferView(rootSignatureSlot, res->GetGPUVirtualAddress());
    }

    TrackResource(resource);
}

void GCommandList::SetRootUnorderedAccessView(UINT rootSignatureSlot, GResource& resource)
{
    if (setedRootSignature == nullptr) assert("Root Signature not set into the CommandList");

    if (!resource.IsValid()) assert("Resource is invalid");

    auto res = resource.GetD3D12Resource();

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        m_d3d12CommandList->SetGraphicsRootUnorderedAccessView(rootSignatureSlot, res->GetGPUVirtualAddress());
    }

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        m_d3d12CommandList->SetComputeRootUnorderedAccessView(rootSignatureSlot, res->GetGPUVirtualAddress());
    }

    TrackResource(resource);
}

void GCommandList::SetRootDescriptorTable(UINT rootSignatureSlot, GMemory& memory, UINT offset) const
{
	
    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        m_d3d12CommandList->SetGraphicsRootDescriptorTable(rootSignatureSlot, memory.GetGPUHandle(offset));
    }

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        m_d3d12CommandList->SetComputeRootDescriptorTable(rootSignatureSlot, memory.GetGPUHandle(offset));
    }
}

void GCommandList::SetViewports(const D3D12_VIEWPORT* viewports, size_t count) const
{
    assert(count < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    m_d3d12CommandList->RSSetViewports(count, viewports);
}

void GCommandList::SetScissorRects(const D3D12_RECT* scissorRects, size_t count) const
{
    assert(count < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    m_d3d12CommandList->RSSetScissorRects(count, scissorRects);
}

void GCommandList::SetPipelineState(PSO& pso)
{
	const auto pipeState = pso.GetPSO();

	if(pipeState == setedPSO) return;

    setedPSO = pipeState;

    m_d3d12CommandList->SetPipelineState(setedPSO.Get());

    TrackResource(pipeState);
}

void GCommandList::TransitionBarrier(Microsoft::WRL::ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES stateAfter, UINT subresource, bool flushBarriers)
{
    if (resource)
    {
        // The "before" state is not important. It will be resolved by the resource state tracker.
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), D3D12_RESOURCE_STATE_COMMON, stateAfter, subresource);
        m_ResourceStateTracker->ResourceBarrier(barrier);
    }

    if (flushBarriers)
    {
        FlushResourceBarriers();
    }
}

void GCommandList::UAVBarrier(const GResource& resource, bool flushBarriers) const
{
	
    UAVBarrier(resource.GetD3D12Resource(), flushBarriers);

}

void GCommandList::UAVBarrier(Microsoft::WRL::ComPtr<ID3D12Resource> resource, bool flushBarriers) const
{
    const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(resource.Get());

    m_ResourceStateTracker->ResourceBarrier(barrier);

    if (flushBarriers)
    {
        FlushResourceBarriers();
    }
	
   
}

void GCommandList::AliasingBarrier(const GResource& beforeResource, const GResource& afterResource, bool flushBarriers) const
{
    AliasingBarrier(beforeResource.GetD3D12Resource(), afterResource.GetD3D12Resource(), flushBarriers);	
}

void GCommandList::AliasingBarrier(Microsoft::WRL::ComPtr<ID3D12Resource> beforeResource,
	Microsoft::WRL::ComPtr<ID3D12Resource> afterResource, bool flushBarriers) const
{
    const auto barrier = CD3DX12_RESOURCE_BARRIER::Aliasing(beforeResource.Get(), afterResource.Get());

    m_ResourceStateTracker->ResourceBarrier(barrier);

    if (flushBarriers)
    {
        FlushResourceBarriers();
    }
}

void GCommandList::FlushResourceBarriers() const
{
    m_ResourceStateTracker->FlushResourceBarriers(*m_d3d12CommandList.Get());
}

void GCommandList::TransitionBarrier(const GResource& resource, D3D12_RESOURCE_STATES stateAfter, UINT subresource, bool flushBarriers)
{
    TransitionBarrier(resource.GetD3D12Resource(), stateAfter, subresource, flushBarriers);
}

void GCommandList::CopyResource(Microsoft::WRL::ComPtr<ID3D12Resource> dstRes, Microsoft::WRL::ComPtr<ID3D12Resource> srcRes)
{
    TransitionBarrier(dstRes, D3D12_RESOURCE_STATE_COPY_DEST);
    TransitionBarrier(srcRes, D3D12_RESOURCE_STATE_COPY_SOURCE);

    FlushResourceBarriers();

    m_d3d12CommandList->CopyResource(dstRes.Get(), srcRes.Get());

    TrackResource(dstRes.Get());
    TrackResource(srcRes.Get());
}

void GCommandList::CopyResource(GResource& dstRes, const GResource& srcRes)
{
    CopyResource(dstRes.GetD3D12Resource(), srcRes.GetD3D12Resource());
}


void GCommandList::ResolveSubresource(GResource& dstRes, const GResource& srcRes, uint32_t dstSubresource, uint32_t srcSubresource)
{
    TransitionBarrier(dstRes, D3D12_RESOURCE_STATE_RESOLVE_DEST, dstSubresource);
    TransitionBarrier(srcRes, D3D12_RESOURCE_STATE_RESOLVE_SOURCE, srcSubresource);

    FlushResourceBarriers();

    m_d3d12CommandList->ResolveSubresource(dstRes.GetD3D12Resource().Get(), dstSubresource, srcRes.GetD3D12Resource().Get(), srcSubresource, dstRes.GetD3D12ResourceDesc().Format);

    TrackResource(srcRes);
    TrackResource(dstRes);
}

void GCommandList::SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology) const
{
    m_d3d12CommandList->IASetPrimitiveTopology(primitiveTopology);
}
