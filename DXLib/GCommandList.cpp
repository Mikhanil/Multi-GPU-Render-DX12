#include "GCommandList.h"



#include "ComputePSO.h"
#include "d3dApp.h"
#include "d3dUtil.h"
#include "GDataUploader.h"
#include "GResource.h"
#include "GResourceStateTracker.h"
#include "GMemory.h"
#include "GraphicPSO.h"
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

GCommandList::GCommandList(ComPtr<ID3D12Device> device,D3D12_COMMAND_LIST_TYPE type)
    : m_d3d12CommandListType(type), device(device)
{

    ThrowIfFailed(device->CreateCommandAllocator(m_d3d12CommandListType, IID_PPV_ARGS(&m_d3d12CommandAllocator)));

    ThrowIfFailed(device->CreateCommandList(0, m_d3d12CommandListType, m_d3d12CommandAllocator.Get(),
        nullptr, IID_PPV_ARGS(&cmdList)));

    m_UploadBuffer = std::make_unique<GDataUploader>();

    m_ResourceStateTracker = std::make_unique<GResourceStateTracker>();

    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        m_DescriptorHeaps[i] = nullptr;
    }
}

GCommandList::~GCommandList()
{}

D3D12_COMMAND_LIST_TYPE GCommandList::GetCommandListType() const
{
	return m_d3d12CommandListType;
}

ComPtr<ID3D12GraphicsCommandList2> GCommandList::GetGraphicsCommandList() const
{
	return cmdList;
}

void GCommandList::SetGMemory(const GMemory* memory) 
{
	if(memory == nullptr || memory->IsNull())
		assert("Memory Descriptor Heap is null");

    const auto type = memory->GetType();
    const auto heap = memory->GetDescriptorHeap();
	
    if (m_DescriptorHeaps[type] != heap)
    {
        m_DescriptorHeaps[type] = heap;
    	
		BindDescriptorHeaps();
    }
}

void GCommandList::BindDescriptorHeaps()
{
	UINT numDescriptorHeaps = 0;
	ID3D12DescriptorHeap* descriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES] = {};

	for (uint32_t i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
	{
		ID3D12DescriptorHeap* descriptorHeap = m_DescriptorHeaps[i];
		if (descriptorHeap)
		{
			descriptorHeaps[numDescriptorHeaps++] = descriptorHeap;
		}
	}

	cmdList->SetDescriptorHeaps(numDescriptorHeaps, descriptorHeaps);
}

void GCommandList::SetRootSignature(RootSignature* signature)
{
	const auto d3d12RootSignature = signature->GetRootSignature();
	
    if(setedRootSignature == d3d12RootSignature) return;
    
    setedRootSignature = d3d12RootSignature;
	
	if(m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
	{
        cmdList->SetGraphicsRootSignature(setedRootSignature.Get());
	}

	if(m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
	{
        cmdList->SetComputeRootSignature(setedRootSignature.Get());
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
        cmdList->SetGraphicsRootShaderResourceView(rootSignatureSlot, res->GetGPUVirtualAddress());
	}

	if(m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
	{
        cmdList->SetComputeRootShaderResourceView(rootSignatureSlot, res->GetGPUVirtualAddress());
	}

    TrackResource(resource);
}

void GCommandList::SetRootShaderResourceView(UINT rootSignatureSlot, D3D12_GPU_VIRTUAL_ADDRESS address)
{

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        cmdList->SetGraphicsRootShaderResourceView(rootSignatureSlot, address);
    }

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        cmdList->SetComputeRootShaderResourceView(rootSignatureSlot, address);
    }
}

void GCommandList::SetRootConstantBufferView(UINT rootSignatureSlot, GResource& resource)
{
    if (setedRootSignature == nullptr) assert("Root Signature not set into the CommandList");

    if (!resource.IsValid()) assert("Resource is invalid");

    auto res = resource.GetD3D12Resource();
	
    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        cmdList->SetGraphicsRootConstantBufferView(rootSignatureSlot, res->GetGPUVirtualAddress());
    }

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        cmdList->SetComputeRootConstantBufferView(rootSignatureSlot, res->GetGPUVirtualAddress());
    }

    TrackResource(resource);
}

void GCommandList::SetRootConstantBufferView(UINT rootSignatureSlot, D3D12_GPU_VIRTUAL_ADDRESS address)
{

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        cmdList->SetGraphicsRootConstantBufferView(rootSignatureSlot, address);
    }

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        cmdList->SetComputeRootConstantBufferView(rootSignatureSlot, address);
    }
}

void GCommandList::SetRoot32BitConstants(UINT rootSignatureSlot, UINT Count32BitValueToSet, const void* data, UINT DestOffsetIn32BitValueToSet )
{
	
    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        cmdList->SetGraphicsRoot32BitConstants(rootSignatureSlot, Count32BitValueToSet, data, DestOffsetIn32BitValueToSet);
    }

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        cmdList->SetComputeRoot32BitConstants(rootSignatureSlot, Count32BitValueToSet, data,DestOffsetIn32BitValueToSet);
    }

}

void GCommandList::SetRoot32BitConstant(UINT shaderRegister, UINT value, UINT offset)
{
    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        cmdList->SetGraphicsRoot32BitConstant(shaderRegister, value, offset);
    }

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        cmdList->SetComputeRoot32BitConstant(shaderRegister, value, offset);
    }	
}


void GCommandList::SetRootUnorderedAccessView(UINT rootSignatureSlot, GResource& resource)
{
    if (setedRootSignature == nullptr) assert("Root Signature not set into the CommandList");

    if (!resource.IsValid()) assert("Resource is invalid");

    auto res = resource.GetD3D12Resource();

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        cmdList->SetGraphicsRootUnorderedAccessView(rootSignatureSlot, res->GetGPUVirtualAddress());
    }

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        cmdList->SetComputeRootUnorderedAccessView(rootSignatureSlot, res->GetGPUVirtualAddress());
    }

    TrackResource(resource);
}

void GCommandList::SetRootDescriptorTable(UINT rootSignatureSlot,const GMemory* memory, UINT offset) const
{
    if(memory == nullptr || memory->IsNull())
    {
        assert("Memory null");
    }

	
    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        cmdList->SetGraphicsRootDescriptorTable(rootSignatureSlot, memory->GetGPUHandle(offset));
    }

    if (m_d3d12CommandListType == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        cmdList->SetComputeRootDescriptorTable(rootSignatureSlot, memory->GetGPUHandle(offset));
    }
}

void GCommandList::UpdateSubresource(GResource& destResource, D3D12_SUBRESOURCE_DATA* subresources,
                                      size_t countSubresources)
{
    auto res = destResource.GetD3D12Resource();
	
    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(res.Get(),
        0, countSubresources);

    auto upload = DXAllocator::UploadData(uploadBufferSize, nullptr, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

    m_ResourceStateTracker->TransitionResource(res.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
    FlushResourceBarriers();
	
    UpdateSubresources(cmdList.Get(),
        res.Get(), &upload.d3d12Resource,
        upload.Offset, 0, countSubresources,
        subresources);

    TrackResource(res.Get());
}

void GCommandList::SetViewports(const D3D12_VIEWPORT* viewports, size_t count) const
{
    assert(count < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    cmdList->RSSetViewports(count, viewports);
}

void GCommandList::SetScissorRects(const D3D12_RECT* scissorRects, size_t count) const
{
    assert(count < D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    cmdList->RSSetScissorRects(count, scissorRects);
}

void GCommandList::SetPipelineState(GraphicPSO& pso)
{
	const auto pipeState = pso.GetPSO();

	if(pipeState == setedPSO) return;

    setedPSO = pipeState;

    cmdList->SetPipelineState(setedPSO.Get());

    TrackResource(pipeState);
}

void GCommandList::SetPipelineState(ComputePSO& pso)
{
    const auto pipeState = pso.GetPSO();

    if (pipeState == setedPSO) return;

    setedPSO = pipeState;

    cmdList->SetPipelineState(setedPSO.Get());

    TrackResource(pipeState);
}

void GCommandList::SetVBuffer(UINT slot, UINT count, D3D12_VERTEX_BUFFER_VIEW* views)
{
    cmdList->IASetVertexBuffers(slot, count, views);
}

void GCommandList::SetIBuffer(D3D12_INDEX_BUFFER_VIEW* views)
{
    cmdList->IASetIndexBuffer(views);
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
    m_ResourceStateTracker->FlushResourceBarriers(*cmdList.Get());
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

    cmdList->CopyResource(dstRes.Get(), srcRes.Get());

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

    cmdList->ResolveSubresource(dstRes.GetD3D12Resource().Get(), dstSubresource, srcRes.GetD3D12Resource().Get(), srcSubresource, dstRes.GetD3D12ResourceDesc().Format);

    TrackResource(srcRes);
    TrackResource(dstRes);
}

void GCommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertex, uint32_t startInstance)
{
    FlushResourceBarriers();

    cmdList->DrawInstanced(vertexCount, instanceCount, startVertex, startInstance);
}

void GCommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t startIndex, int32_t baseVertex,
	uint32_t startInstance)
{
    FlushResourceBarriers();

    cmdList->DrawIndexedInstanced(indexCount, instanceCount, startIndex, baseVertex, startInstance);
}

void GCommandList::Dispatch(uint32_t numGroupsX, uint32_t numGroupsY, uint32_t numGroupsZ) const
{	
    FlushResourceBarriers();
    cmdList->Dispatch(numGroupsX, numGroupsY, numGroupsZ);
}

void GCommandList::ReleaseTrackedObjects()
{
    m_TrackedObjects.clear();
}

void GCommandList::Reset(GraphicPSO* pso)
{	
	ThrowIfFailed(m_d3d12CommandAllocator->Reset());

    m_ResourceStateTracker->Reset();
    m_UploadBuffer->Reset();

    ReleaseTrackedObjects();

    setedRootSignature = nullptr;
    setedPSO = nullptr;

    for (int i = 0; i < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++i)
    {
        m_DescriptorHeaps[i] = nullptr;
    }
	
    if (pso != nullptr)
    {
	    setedPSO = pso->GetPSO();
        TrackResource(setedPSO);
    }
	
	ThrowIfFailed(cmdList->Reset(m_d3d12CommandAllocator.Get(), setedPSO.Get()));
}

bool GCommandList::Close(GCommandList& pendingCommandList)
{
    // Flush any remaining barriers.
    FlushResourceBarriers();

    cmdList->Close();

    const auto peddingCmdList = pendingCommandList.GetGraphicsCommandList();
    // Flush pending resource barriers.
    const uint32_t numPendingBarriers = m_ResourceStateTracker->FlushPendingResourceBarriers(*peddingCmdList.Get());
    // Commit the final resource state to the global state.
    m_ResourceStateTracker->CommitFinalResourceStates();

    return numPendingBarriers > 0;
}

void GCommandList::Close()
{
    FlushResourceBarriers();
    cmdList->Close();
}

void GCommandList::SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology) const
{
    cmdList->IASetPrimitiveTopology(primitiveTopology);
}

void GCommandList::ClearRenderTarget(GMemory* memory, size_t offset, const FLOAT rgba[4], D3D12_RECT* rects,
	size_t rectCount)
{
    if(memory == nullptr || memory->IsNull())
    {
        assert("Bad Clear Render Target");
    }	
    cmdList->ClearRenderTargetView(memory->GetCPUHandle(offset), rgba, rectCount, rects);
}

void GCommandList::SetRenderTargets(size_t RTCount, GMemory* rtvMemory, size_t rtvOffset, GMemory* dsvMemory,
	size_t dsvOffset)
{
    auto* rtvPtr = (rtvMemory == nullptr || rtvMemory->IsNull()) ? nullptr : &rtvMemory->GetCPUHandle(rtvOffset);

    auto* dsvPtr = (dsvMemory == nullptr || dsvMemory->IsNull()) ? nullptr : &dsvMemory->GetCPUHandle(dsvOffset);
	
    cmdList->OMSetRenderTargets(RTCount, rtvPtr, RTCount == 1, dsvPtr);
}

void GCommandList::ClearDepthStencil(GMemory* dsvMemory, size_t dsvOffset, D3D12_CLEAR_FLAGS flags, FLOAT depthValue,  UINT stencilValue, D3D12_RECT* rects, size_t rectCount )
{
    cmdList->ClearDepthStencilView(dsvMemory->GetCPUHandle(dsvOffset), flags, depthValue, stencilValue, rectCount, rects);
}
