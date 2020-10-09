#pragma once


#include "MemoryAllocator.h"
#include <d3d12.h>
#include <wrl/client.h>

#include "d3dx12.h"

using namespace Microsoft::WRL;
class GDevice;

class GRootSignature
{
	ComPtr<ID3D12RootSignature> signature;
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;


	custom_vector<CD3DX12_ROOT_PARAMETER> slotRootParameters = MemoryAllocator::CreateVector<CD3DX12_ROOT_PARAMETER>();

	void AddParameter(CD3DX12_ROOT_PARAMETER parameter);

	custom_vector<D3D12_STATIC_SAMPLER_DESC> staticSampler = MemoryAllocator::CreateVector<D3D12_STATIC_SAMPLER_DESC>();

	custom_vector<uint32_t> descriptorPerTableCount = MemoryAllocator::CreateVector<uint32_t>();

	uint32_t samplerTableBitMask;

	uint32_t descriptorTableBitMask;


public:

	virtual ~GRootSignature();

	const D3D12_ROOT_SIGNATURE_DESC& GetRootSignatureDesc() const;

	void AddDescriptorParameter(CD3DX12_DESCRIPTOR_RANGE* rangeParameters, UINT size,
	                            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

	void AddConstantBufferParameter(UINT shaderRegister, UINT registerSpace = 0,
	                                D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

	void AddConstantParameter(UINT valueCount, UINT shaderRegister, UINT registerSpace = 0,
	                          D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

	void AddShaderResourceView(UINT shaderRegister, UINT registerSpace = 0,
	                           D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

	void AddUnorderedAccessView(UINT shaderRegister, UINT registerSpace = 0,
	                            D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

	void AddStaticSampler(CD3DX12_STATIC_SAMPLER_DESC sampler);
	void AddStaticSampler(D3D12_STATIC_SAMPLER_DESC sampler);;

	void Initialize(std::shared_ptr<GDevice> device);

	ComPtr<ID3D12RootSignature> GetRootSignature() const;
};
