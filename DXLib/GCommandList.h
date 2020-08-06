#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <map>
#include <mutex>
#include "DXAllocator.h"
#include "GResourceStateTracker.h"

using namespace  Microsoft::WRL;

class GResource;

class GCommandList
{
private:
	
	// Keep track of loaded textures to avoid loading the same texture multiple times.
	static custom_map<std::wstring, ID3D12Resource* > ms_TextureCache;
	static std::mutex ms_TextureCacheMutex;

	using TrackedObjects = custom_vector < Microsoft::WRL::ComPtr<ID3D12Object> >;
	TrackedObjects m_TrackedObjects = DXAllocator::CreateVector<Microsoft::WRL::ComPtr<ID3D12Object> >();

	std::unique_ptr<GDataUploader> m_UploadBuffer;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList2> m_d3d12CommandList;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_d3d12CommandAllocator;
	
	D3D12_COMMAND_LIST_TYPE m_d3d12CommandListType;
	std::unique_ptr<GResourceStateTracker> m_ResourceStateTracker;


	void TrackResource(Microsoft::WRL::ComPtr<ID3D12Object> object);
	void TrackResource(const GResource& res);

public:

	GCommandList(D3D12_COMMAND_LIST_TYPE type);
	virtual ~GCommandList();

	D3D12_COMMAND_LIST_TYPE GetCommandListType() const;

	ComPtr<ID3D12GraphicsCommandList2> GetGraphicsCommandList() const;

	
	void TransitionBarrier(const GResource& resource, D3D12_RESOURCE_STATES stateAfter, UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bool flushBarriers = false);
	void TransitionBarrier(Microsoft::WRL::ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES stateAfter, UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, bool flushBarriers = false);

	void UAVBarrier(const GResource& resource, bool flushBarriers = false) const;
	void UAVBarrier(Microsoft::WRL::ComPtr<ID3D12Resource> resource, bool flushBarriers = false) const;

	void AliasingBarrier(const GResource& beforeResource, const GResource& afterResource, bool flushBarriers = false) const;
	void AliasingBarrier(Microsoft::WRL::ComPtr<ID3D12Resource> beforeResource, Microsoft::WRL::ComPtr<ID3D12Resource> afterResource, bool flushBarriers = false) const;
	
	void FlushResourceBarriers() const;

	void CopyResource(GResource& dstRes, const GResource& srcRes);
	void CopyResource(Microsoft::WRL::ComPtr<ID3D12Resource> dstRes, Microsoft::WRL::ComPtr<ID3D12Resource> srcRes);

	/**
	 * Resolve a multisampled resource into a non-multisampled resource.
	 */
	void ResolveSubresource(GResource& dstRes, const GResource& srcRes, uint32_t dstSubresource = 0, uint32_t srcSubresource = 0);

	void SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology) const;
};

