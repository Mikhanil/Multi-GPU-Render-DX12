#include "RootSignature.h"

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> RootSignature::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressW
		0.0f, // mipLODBias
		8); // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressW
		0.0f, // mipLODBias
		8); // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC cubesTexSampler(
		6, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressW
		0.0f, // mipLODBias
		8, // maxAnisotropy
		D3D12_COMPARISON_FUNC_NEVER);


	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp, cubesTexSampler
	};
}

void RootSignature::AddParameter(CD3DX12_ROOT_PARAMETER parameter)
{
	slotRootParameters.push_back(parameter);
}

void RootSignature::AddDescriptorParameter(CD3DX12_DESCRIPTOR_RANGE* rangeParameters, UINT size,
                                           D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER slotParameter;
	slotParameter.InitAsDescriptorTable(size, rangeParameters, visibility);
	AddParameter(slotParameter);
}

void RootSignature::AddConstantBufferParameter(UINT shaderRegister, UINT registerSpace,
                                               D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER slotParameter;
	slotParameter.InitAsConstantBufferView(shaderRegister, registerSpace, visibility);
	AddParameter(slotParameter);
}

void RootSignature::AddConstantParameter(UINT value, UINT shaderRegister, UINT registerSpace,
                                         D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER slotParameter;
	slotParameter.InitAsConstants(value, shaderRegister, registerSpace, visibility);
	AddParameter(slotParameter);
}

void RootSignature::AddShaderResourceView(UINT shaderRegister, UINT registerSpace, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER slotParameter;
	slotParameter.InitAsShaderResourceView(shaderRegister, registerSpace, visibility);
	AddParameter(slotParameter);
}

void RootSignature::AddUnorderedAccessView(UINT shaderRegister, UINT registerSpace, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER slotParameter;
	slotParameter.InitAsUnorderedAccessView(shaderRegister, registerSpace, visibility);
	AddParameter(slotParameter);
}

void RootSignature::AddStaticSampler(const CD3DX12_STATIC_SAMPLER_DESC sampler)
{
	staticSampler.push_back(sampler);
}

void RootSignature::AddStaticSampler(const D3D12_STATIC_SAMPLER_DESC sampler)
{
	staticSampler.push_back(sampler);
}


void RootSignature::Initialize(ID3D12Device* device)
{
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc;
	if(!staticSampler.empty())
	{
		rootSigDesc = CD3DX12_ROOT_SIGNATURE_DESC(slotRootParameters.size(), slotRootParameters.data(),
			staticSampler.size(), staticSampler.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	}
	else
	{
		auto defaultSampler = GetStaticSamplers();

		rootSigDesc = CD3DX12_ROOT_SIGNATURE_DESC (slotRootParameters.size(), slotRootParameters.data(),
			defaultSampler.size(), defaultSampler.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	}
	
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
	                                               serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(signature.GetAddressOf())));
}

ComPtr<ID3D12RootSignature> RootSignature::GetRootSignature() const
{
	return signature;
}
