#include "Material.h"

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> Material::GetStaticSamplers()
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

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp
	};
}

D3D12_INPUT_LAYOUT_DESC Material::GetInputLayoutInfo()
{
	D3D12_INPUT_LAYOUT_DESC info{inputLayout.data(), inputLayout.size()};
	return info;
}

void Material::CreateRootSignature(ID3D12Device* device)
{
	if (rootSignature != nullptr) rootSignature->Release();

	CD3DX12_DESCRIPTOR_RANGE texTable[1];

	for (int i = 0; i < _countof(texTable); ++i)
	{
		texTable[i].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		                 1, // number of descriptors
		                 i); // register t
	}


	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable[0], D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);

	auto staticSamplers = GetStaticSamplers();


	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParameter), slotRootParameter,
	                                        staticSamplers.size(), staticSamplers.data(),
	                                        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
	                                               serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(device->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(rootSignature.GetAddressOf())));
}

void Material::CreatePso(ID3D12Device* device)
{
	if (pso != nullptr) pso->Release();

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;

	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	CreateRootSignature(device);

	psoDesc.InputLayout = GetInputLayoutInfo();
	psoDesc.pRootSignature = rootSignature.Get();
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = backBufferFormat;
	psoDesc.SampleDesc.Count = ism4xmsaa ? 4 : 1;
	psoDesc.SampleDesc.Quality = ism4xmsaa ? (m4xMsaaQuality - 1) : 0;
	psoDesc.DSVFormat = depthStencilFormat;

	for (auto&& shader : shaders)
	{
		switch (shader->GetType())
		{
		case VertexShader:
			{
				psoDesc.VS = shader->GetShaderInfo();
				break;
			}
		case PixelShader:
			{
				psoDesc.PS = shader->GetShaderInfo();
				break;
			}
		case DomainShader:
			{
				psoDesc.DS = shader->GetShaderInfo();
				break;
			}
		case GeometryShader:
			{
				psoDesc.GS = shader->GetShaderInfo();
				break;
			}
		case HullShader:
			{
				psoDesc.HS = shader->GetShaderInfo();
				break;
			}
		}
	}

	switch (type)
	{
	case Opaque:
		{
			break;
		}
	case Wireframe:
		{
			psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
			break;
		}
	case AlphaDrop:
		{
			psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
			break;
		}
	case Transparent:
		{
			D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
			transparencyBlendDesc.BlendEnable = true;
			transparencyBlendDesc.LogicOpEnable = false;
			transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
			transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
			transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
			transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
			transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
			transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			psoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
			break;
		}
	}


	ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));

	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&debugPso)));
}

MaterialType Material::GetType()
{
	return type;
}

void Material::SetDiffuseTexture(Texture* texture)
{
	textures[0] = texture;
}

Material::Material(ID3D12Device* device, std::string name, MaterialType type, DXGI_FORMAT backBufferFormat,
                   DXGI_FORMAT depthStencilFormat, const std::vector<D3D12_INPUT_ELEMENT_DESC> layout, bool isM4xMsaa,
                   UINT m4xMsaaQuality): Name(std::move(name)), backBufferFormat(backBufferFormat),
                                         depthStencilFormat(depthStencilFormat), inputLayout(layout),
                                         ism4xmsaa(isM4xMsaa), m4xMsaaQuality(m4xMsaaQuality), type(type)
{
	cbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	materialBuffer = std::make_unique<ConstantBuffer<MaterialConstants>>(device, 1);
}

void Material::AddShader(Shader* shader)
{
	shaders.push_back(shader);
}

void Material::InitMaterial(ID3D12Device* device)
{
	CreatePso(device);

	if (materialViewDescriptorHeap != nullptr) materialViewDescriptorHeap->Release();

	//
	// Create the SRV heap.
	//		
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = textures.size();
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&materialViewDescriptorHeap)));


	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(materialViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

	for (int i = 0; i < textures.size(); ++i)
	{
		srvDesc.Format = textures[i]->GetResource()->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = textures[i]->GetResource()->GetDesc().MipLevels;
		device->CreateShaderResourceView(textures[i]->GetResource(), &srvDesc, hDescriptor);
		hDescriptor.Offset(1, cbvSrvUavDescriptorSize);
	}
}

ID3D12PipelineState* Material::GetPSO()
{
	if(globalVar->globalIsDebug)
	{
		return debugPso.Get();
	}
	else
	{
		return pso.Get();
	}
	
}

ID3D12RootSignature* Material::GetRootSignature()
{
	return rootSignature.Get();
}

void Material::Draw(ID3D12GraphicsCommandList* cmdList) const
{
	cmdList->SetGraphicsRootConstantBufferView(3, materialBuffer->Resource()->GetGPUVirtualAddress());

	cmdList->SetDescriptorHeaps(1, materialViewDescriptorHeap.GetAddressOf());

	CD3DX12_GPU_DESCRIPTOR_HANDLE tex(materialViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	for (int i = 0; i < textures.size(); ++i)
	{
		tex.Offset(i, cbvSrvUavDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(i, tex);
	}
}

void Material::Update()
{
	if (NumFramesDirty > 0)
	{
		matConstants.DiffuseAlbedo = DiffuseAlbedo;
		matConstants.FresnelR0 = FresnelR0;
		matConstants.Roughness = Roughness;
		XMStoreFloat4x4(&matConstants.MaterialTransform, XMMatrixTranspose(XMLoadFloat4x4(&MatTransform)));

		materialBuffer->CopyData(0, matConstants);

		// Next FrameResource need to be updated too.
		NumFramesDirty--;
	}
}

std::string& Material::GetName()
{
	return Name;
}
