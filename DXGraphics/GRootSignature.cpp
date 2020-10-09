#include "GRootSignature.h"


#include "d3dUtil.h"
#include "GDevice.h"


std::vector<CD3DX12_STATIC_SAMPLER_DESC> GetStaticSamplers()
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

	const CD3DX12_STATIC_SAMPLER_DESC shadow(
		6, // shaderRegister
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressW
		0.0f, // mipLODBias
		16, // maxAnisotropy
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);


	return std::vector<CD3DX12_STATIC_SAMPLER_DESC>{
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp, shadow
	};
}

static std::vector<CD3DX12_STATIC_SAMPLER_DESC> staticSamplersVector = GetStaticSamplers();

GRootSignature::~GRootSignature()
{
	slotRootParameters.clear();
	staticSampler.clear();
}

const D3D12_ROOT_SIGNATURE_DESC& GRootSignature::GetRootSignatureDesc() const
{
	return rootSigDesc;
}

void GRootSignature::AddParameter(CD3DX12_ROOT_PARAMETER parameter)
{
	slotRootParameters.push_back(parameter);
}

void GRootSignature::AddDescriptorParameter(CD3DX12_DESCRIPTOR_RANGE* rangeParameters, UINT size,
                                            D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER slotParameter;
	slotParameter.InitAsDescriptorTable(size, rangeParameters, visibility);
	AddParameter(slotParameter);
}

void GRootSignature::AddConstantBufferParameter(UINT shaderRegister, UINT registerSpace,
                                                D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER slotParameter;
	slotParameter.InitAsConstantBufferView(shaderRegister, registerSpace, visibility);
	AddParameter(slotParameter);
}

void GRootSignature::AddConstantParameter(UINT valueCount, UINT shaderRegister, UINT registerSpace,
                                          D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER slotParameter;
	slotParameter.InitAsConstants(valueCount, shaderRegister, registerSpace, visibility);
	AddParameter(slotParameter);
}

void GRootSignature::AddShaderResourceView(UINT shaderRegister, UINT registerSpace, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER slotParameter;
	slotParameter.InitAsShaderResourceView(shaderRegister, registerSpace, visibility);
	AddParameter(slotParameter);
}

void GRootSignature::AddUnorderedAccessView(UINT shaderRegister, UINT registerSpace, D3D12_SHADER_VISIBILITY visibility)
{
	CD3DX12_ROOT_PARAMETER slotParameter;
	slotParameter.InitAsUnorderedAccessView(shaderRegister, registerSpace, visibility);
	AddParameter(slotParameter);
}

void GRootSignature::AddStaticSampler(const CD3DX12_STATIC_SAMPLER_DESC sampler)
{
	staticSampler.push_back(sampler);
}

void GRootSignature::AddStaticSampler(const D3D12_STATIC_SAMPLER_DESC sampler)
{
	staticSampler.push_back(sampler);
}


void GRootSignature::Initialize(const std::shared_ptr<GDevice> device)
{
	if (!staticSampler.empty())
	{
		rootSigDesc = CD3DX12_ROOT_SIGNATURE_DESC(slotRootParameters.size(), slotRootParameters.data(),
		                                          staticSampler.size(), staticSampler.data(),
		                                          D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	}
	else
	{
		rootSigDesc = CD3DX12_ROOT_SIGNATURE_DESC(slotRootParameters.size(), slotRootParameters.data(),
		                                          staticSamplersVector.size(), staticSamplersVector.data(),
		                                          D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	}


	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
	                                               serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(device->GetDXDevice()->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(signature.GetAddressOf())));
}

ComPtr<ID3D12RootSignature> GRootSignature::GetRootSignature() const
{
	return signature;
}
