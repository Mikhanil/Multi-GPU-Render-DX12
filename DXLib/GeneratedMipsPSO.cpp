#include "GeneratedMipsPSO.h"
#include "Shader.h"

GeneratedMipsPSO::GeneratedMipsPSO(ID3D12Device* device)
{
	//The compute shader expects 2 floats, the source texture and the destination texture
	CD3DX12_DESCRIPTOR_RANGE srvCbvRanges[2];
	srvCbvRanges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
	srvCbvRanges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);


	//Static sampler used to get the linearly interpolated color for the mipmaps
	D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
	samplerDesc.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
	samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	samplerDesc.MinLOD = 0.0f;
	samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
	samplerDesc.MaxAnisotropy = 0;
	samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
	samplerDesc.ShaderRegister = 0;
	samplerDesc.RegisterSpace = 0;
	samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;


	m_RootSignature.AddConstantParameter(2, 0);
	m_RootSignature.AddDescriptorParameter(&srvCbvRanges[0], 1);
	m_RootSignature.AddDescriptorParameter(&srvCbvRanges[1], 1);
	m_RootSignature.AddStaticSampler(samplerDesc);
	m_RootSignature.Initialize(device);

	D3D12_COMPUTE_PIPELINE_STATE_DESC computePSOdesc{};

	auto shader = std::make_unique<Shader>(L"Shaders\\MipMapCS.hlsl", ComputeShader, nullptr, "GenerateMipMaps",
	                                       "cs_5_1");
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

	mipsBuffer = std::make_unique<ConstantBuffer<GenerateMipsCB>>(device, 1);
}

const ComPtr<ID3D12RootSignature> GeneratedMipsPSO::GetRootSignature() const
{
	return m_RootSignature.GetRootSignature();
}

ComPtr<ID3D12DescriptorHeap> GeneratedMipsPSO::GetUAVHeap() const
{
	return UAVdescriptorHeap;
}

ComPtr<ID3D12PipelineState> GeneratedMipsPSO::GetPipelineState() const
{
	return m_PipelineState;
}
