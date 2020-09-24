#include "GRootSignature.h"

#include "GDevice.h"


std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GRootSignature::GetStaticSamplers()
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


	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp, shadow
	};
}

GRootSignature::~GRootSignature()
{
	slotRootParameters.clear();
	staticSampler.clear();
}

const D3D12_ROOT_SIGNATURE_DESC& GRootSignature::GetRootSignatureDesc() const
{
	return rootSigDesc;
}

uint32_t GRootSignature::GetParametersCount() const
{
	return slotRootParameters.size();
}

uint32_t GRootSignature::GetDescriptorTableBitMask(D3D12_DESCRIPTOR_HEAP_TYPE descriptorHeapType) const
{
	uint32_t descriptorTableBitMask = 0;
	switch (descriptorHeapType)
	{
	case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
		descriptorTableBitMask = descriptorTableBitMask;
		break;
	case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
		descriptorTableBitMask = samplerTableBitMask;
		break;
	}

	return descriptorTableBitMask;
}

uint32_t GRootSignature::GetNumDescriptors(uint32_t rootIndex) const
{
	assert(rootIndex < 32);
	return descriptorPerTableCount[rootIndex];
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
		auto defaultSampler = GetStaticSamplers();

		rootSigDesc = CD3DX12_ROOT_SIGNATURE_DESC(slotRootParameters.size(), slotRootParameters.data(),
		                                          defaultSampler.size(), defaultSampler.data(),
		                                          D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
	}

	for (UINT i = 0; i < rootSigDesc.NumParameters; ++i)
	{
		descriptorPerTableCount.push_back(0);

		const D3D12_ROOT_PARAMETER& rootParameter = rootSigDesc.pParameters[i];

		if (rootParameter.ParameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
		{
			UINT numDescriptorRanges = rootParameter.DescriptorTable.NumDescriptorRanges;

			// Set the bit mask depending on the type of descriptor table.
			if (numDescriptorRanges > 0)
			{
				switch (rootParameter.DescriptorTable.pDescriptorRanges[0].RangeType)
				{
				case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
				case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
				case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
					descriptorTableBitMask |= (1 << i);
					break;
				case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
					samplerTableBitMask |= (1 << i);
					break;
				}
			}


			// Count the number of descriptors in the descriptor table.
			for (UINT j = 0; j < numDescriptorRanges; ++j)
			{
				descriptorPerTableCount[i] += rootParameter.DescriptorTable.pDescriptorRanges[j].NumDescriptors;
			}
		}
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
