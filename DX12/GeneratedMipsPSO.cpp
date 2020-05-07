#include "GeneratedMipsPSO.h"

GeneratedMipsPSO::GeneratedMipsPSO(ID3D12Device* device)
{
	CD3DX12_DESCRIPTOR_RANGE srcMip(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0,
	                                D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
	CD3DX12_DESCRIPTOR_RANGE outMip(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0, 0,
	                                D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

	CD3DX12_ROOT_PARAMETER rootParameters[3];
	rootParameters[0].InitAsConstantBufferView(0);
	rootParameters[1].InitAsDescriptorTable(1, &srcMip);
	rootParameters[2].InitAsDescriptorTable(1, &outMip);

	CD3DX12_STATIC_SAMPLER_DESC linearClampSampler(0,
	                                               D3D12_FILTER_MIN_MAG_MIP_LINEAR,
	                                               D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
	                                               D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
	                                               D3D12_TEXTURE_ADDRESS_MODE_CLAMP
	);

	CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc(3,
	                                              rootParameters, 1, &linearClampSampler);

	m_RootSignature.AddConstantBufferParameter(0);
	m_RootSignature.AddDescriptorParameter(&srcMip, 1);
	m_RootSignature.AddDescriptorParameter(&outMip, 1);
	m_RootSignature.AddStaticSampler(linearClampSampler);

	D3D12_COMPUTE_PIPELINE_STATE_DESC computePSOdesc{};

	auto shader = std::make_unique<Shader>(L"Shaders\\MipMapCS.hlsl", ShaderType::ComputeShader, nullptr, "GenerateMipMaps", "cs_5_1");
	shader->LoadAndCompile();
	
	computePSOdesc.pRootSignature = m_RootSignature.GetRootSignature().Get();
	computePSOdesc.CS = shader->GetShaderResource();
	ThrowIfFailed(device->CreateComputePipelineState(&computePSOdesc, IID_PPV_ARGS(&m_PipelineState)));


	//Create the descriptor heap with layout: source texture - destination texture
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 4;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&UAVdescriptorHeap));
	
	UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	CD3DX12_CPU_DESCRIPTOR_HANDLE handler = CD3DX12_CPU_DESCRIPTOR_HANDLE(
		UAVdescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	for (UINT i = 0; i < 4; ++i)
	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		uavDesc.Texture2D.MipSlice = i;
		uavDesc.Texture2D.PlaneSlice = 0;

		device->CreateUnorderedAccessView(nullptr, nullptr, &uavDesc,
		                                  UAVdescriptorHeap->GetCPUDescriptorHandleForHeapStart());
		handler.Offset(1, descriptorSize);
	}
}

const RootSignature& GeneratedMipsPSO::GetRootSignature() const
{
	return m_RootSignature;
}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> GeneratedMipsPSO::GetUAVHeap() const
{
	return UAVdescriptorHeap;
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> GeneratedMipsPSO::GetPipelineState() const
{
	return m_PipelineState;
}
