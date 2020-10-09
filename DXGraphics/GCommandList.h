#pragma once
#include <d3d12.h>
#include <DirectXColors.h>
#include <mutex>
#include <wrl.h>
#include "MemoryAllocator.h"
#include "GDataUploader.h"

class ShaderBuffer;
using namespace Microsoft::WRL;

namespace DXLib
{
	class GCommandQueue;
}

class GDevice;
class GResource;
class GTexture;
class GDataUploader;
class GMemory;
class GResourceStateTracker;
class GRootSignature;
class GraphicPSO;
class ComputePSO;
struct UploadAllocation;

class GCommandList
{
private:

	using TrackedObjects = custom_vector<ComPtr<ID3D12Object>>;
	TrackedObjects trackedObject = MemoryAllocator::CreateVector<ComPtr<ID3D12Object>>();

	std::unique_ptr<GDataUploader> uploadBuffer;
	UploadAllocation UploadData(size_t sizeInBytes, const void* bufferData, size_t alignment) const;

	ComPtr<ID3D12GraphicsCommandList2> cmdList;
	ComPtr<ID3D12CommandAllocator> cmdAllocator;

	ID3D12DescriptorHeap* setedDescriptorHeaps[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];

	D3D12_COMMAND_LIST_TYPE type;
	std::unique_ptr<GResourceStateTracker> tracker;

	D3D12_PRIMITIVE_TOPOLOGY setedPrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
	ComPtr<ID3D12RootSignature> setedRootSignature = nullptr;
	ComPtr<ID3D12PipelineState> setedPSO = nullptr;


	void TrackResource(ComPtr<ID3D12Object> object);
	void TrackResource(const GResource& res);

	std::shared_ptr<DXLib::GCommandQueue> queue;

public:

	std::shared_ptr<GDevice> GetDevice() const;

	GCommandList(std::shared_ptr<DXLib::GCommandQueue> queue, D3D12_COMMAND_LIST_TYPE type);
	virtual ~GCommandList();
	void BeginQuery(UINT index) const;
	void EndQuery(UINT index) const;
	void ResolveQuery(UINT index, UINT quriesCount, UINT64 aligned) const;

	D3D12_COMMAND_LIST_TYPE GetCommandListType() const;

	ComPtr<ID3D12GraphicsCommandList2> GetGraphicsCommandList() const;

	void UpdateDescriptorHeaps();

	void SetGMemory(const GMemory* memory);

	void SetRootSignature(GRootSignature* signature);

	void SetRootShaderResourceView(UINT rootSignatureSlot, ShaderBuffer& resource, UINT offset = 0);

	void SetRootConstantBufferView(UINT rootSignatureSlot, ShaderBuffer& resource, UINT offset = 0);

	void SetRootUnorderedAccessView(UINT rootSignatureSlot, ShaderBuffer& resource, UINT offset = 0);

	void SetRoot32BitConstants(UINT rootSignatureSlot, UINT Count32BitValueToSet, const void* data,
	                           UINT DestOffsetIn32BitValueToSet) const;

	void SetRoot32BitConstant(UINT shaderRegister, UINT value, UINT offset) const;

	void SetRootDescriptorTable(UINT rootSignatureSlot, const GMemory* memory, UINT offset = 0) const;

	void UpdateSubresource(GResource& destResource, D3D12_SUBRESOURCE_DATA* subresources, size_t countSubresources);

	void SetViewports(const D3D12_VIEWPORT* viewports, size_t count) const;

	void SetScissorRects(const D3D12_RECT* scissorRects, size_t count) const;

	void SetPipelineState(GraphicPSO& pso);

	void SetPipelineState(ComputePSO& pso);

	void SetVBuffer(UINT slot = 0, UINT count = 0, D3D12_VERTEX_BUFFER_VIEW* views = nullptr) const;
	void SetIBuffer(D3D12_INDEX_BUFFER_VIEW* views = nullptr) const;

	void SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY primitiveTopology);

	void ClearRenderTarget(GMemory* memory, size_t offset = 0, const FLOAT rgba[4] = DirectX::Colors::Black,
	                       D3D12_RECT* rects = nullptr, size_t rectCount = 0) const;

	void SetRenderTargets(size_t RTCount = 0, GMemory* rtvMemory = nullptr, size_t rtvOffset = 0,
	                      GMemory* dsvMemory = nullptr, size_t dsvOffset = 0) const;

	void ClearDepthStencil(GMemory* dsvMemory, size_t dsvOffset,
	                       D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
	                       FLOAT depthValue = 1.0f, UINT stencilValue = 0, D3D12_RECT* rects = nullptr, size_t
	                       rectCount = 0) const;

	void TransitionBarrier(const GResource& resource, D3D12_RESOURCE_STATES stateAfter,
	                       UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
	                       bool flushBarriers = false) const;

	void CopyTextureRegion(ComPtr<ID3D12Resource> dstRes, UINT DstX,
	                       UINT DstY,
	                       UINT DstZ, ComPtr<ID3D12Resource> srcRes, const D3D12_BOX* srcBox);

	void CopyTextureRegion(const GResource& dstRes, UINT DstX, UINT DstY, UINT DstZ, const GResource& srcRes,
	                       const D3D12_BOX* srcBox);

	void TransitionBarrier(ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES stateAfter,
	                       UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
	                       bool flushBarriers = false) const;

	void UAVBarrier(const GResource& resource, bool flushBarriers = false) const;
	void UAVBarrier(ComPtr<ID3D12Resource> resource, bool flushBarriers = false) const;

	void AliasingBarrier(const GResource& beforeResource, const GResource& afterResource,
	                     bool flushBarriers = false) const;
	void AliasingBarrier(ComPtr<ID3D12Resource> beforeResource, ComPtr<ID3D12Resource> afterResource,
	                     bool flushBarriers = false) const;

	void FlushResourceBarriers() const;

	void CopyResource(const GResource& dstRes, const GResource& srcRes);
	void CopyResource(ComPtr<ID3D12Resource> dstRes, ComPtr<ID3D12Resource> srcRes);

	/**
	 * Resolve a multisampled resource into a non-multisampled resource.
	 */
	void ResolveSubresource(GResource& dstRes, const GResource& srcRes, uint32_t dstSubresource = 0,
	                        uint32_t srcSubresource = 0);

	void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t startVertex = 0,
	          uint32_t startInstance = 0) const;
	void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t startIndex = 0, int32_t baseVertex = 0,
	                 uint32_t startInstance = 0) const;

	void Dispatch(uint32_t numGroupsX, uint32_t numGroupsY = 1, uint32_t numGroupsZ = 1) const;

	void ReleaseTrackedObjects();

	void Reset(GraphicPSO* pso = nullptr);

	bool Close(std::shared_ptr<GCommandList>& pendingCommandList) const;

	void Close() const;
};
