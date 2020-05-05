#include "Material.h"
#include "PSO.h"


PSO* Material::GetPSO()
{
	return pso;
}

void Material::SetDiffuseTexture(Texture* texture)
{
	textures[0] = texture;
}

Material::Material( std::string name, PSO* pso): Name(std::move(name)), pso(pso)
{	
}


void Material::InitMaterial(ID3D12Device* device)
{	
	if (materialViewDescriptorHeap != nullptr) materialViewDescriptorHeap->Release();

	cbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	materialBuffer = std::make_unique<ConstantBuffer<MaterialConstants>>(device, 1);
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
	
	

	for (int i = 0; i < textures.size(); ++i)
	{
		auto desc = textures[i]->GetResource()->GetDesc();
		srvDesc.Format = desc.Format;

		switch (pso->GetType())
		{
		case PsoType::SkyBox:
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
			srvDesc.TextureCube.MipLevels = desc.MipLevels;
			srvDesc.TextureCube.MostDetailedMip = 0;
			break;
		case PsoType::AlphaSprites:
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MostDetailedMip = 0;
			srvDesc.Texture2DArray.MipLevels = -1;
			srvDesc.Texture2DArray.FirstArraySlice = 0;
			srvDesc.Texture2DArray.ArraySize = desc.DepthOrArraySize;
			break;
		default:
			{
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
				srvDesc.Texture2D.MipLevels = desc.MipLevels;
			};
		}
		
		device->CreateShaderResourceView(textures[i]->GetResource(), &srvDesc, hDescriptor);
		hDescriptor.Offset(1, cbvSrvUavDescriptorSize);
	}
}

void Material::Draw(ID3D12GraphicsCommandList* cmdList) const
{
	cmdList->SetGraphicsRootConstantBufferView(3, materialBuffer->Resource()->GetGPUVirtualAddress());

	cmdList->SetDescriptorHeaps(1, materialViewDescriptorHeap.GetAddressOf());

	CD3DX12_GPU_DESCRIPTOR_HANDLE tex(materialViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	for (int i = 0; i < textures.size(); ++i)
	{
		tex.Offset(i, cbvSrvUavDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(0, tex);		
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
