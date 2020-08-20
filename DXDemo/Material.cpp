#include "Material.h"


#include "d3dApp.h"
#include "GCommandList.h"
#include "GMemory.h"
#include "GraphicPSO.h"

UINT Material::materialIndexGlobal = 0;

MaterialConstants& Material::GetMaterialConstantData()
{
	return matConstants;
}

UINT Material::GetIndex() const
{
	return materialIndex;
}

void Material::SetDirty()
{
	NumFramesDirty = globalCountFrameResources;
}

void Material::SetNormalMap(std::shared_ptr<GTexture> texture)
{
	normalMap = texture;
	NormalMapIndex = texture->GetTextureIndex();
}

void Material::SetType(PsoType::Type pso)
{
	this->type = pso;
}

PsoType::Type Material::GetPSO() const
{
	return type;
}

void Material::SetDiffuseTexture(std::shared_ptr<GTexture> texture)
{
	diffuseMap = texture;
	DiffuseMapIndex = texture->GetTextureIndex();
}

Material::Material(std::wstring name, PsoType::Type pso): Name(std::move(name)), type(pso)
{
	materialIndex = materialIndexGlobal++;
}


void Material::InitMaterial(GMemory& textureHeap)
{
	gpuTextureHandle = textureHeap.GetGPUHandle(this->DiffuseMapIndex ); 
	cpuTextureHandle = textureHeap.GetCPUHandle(this->DiffuseMapIndex );

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	
	//TODO: Подумать как можно от этого избавиться, и работать всегда только с индексами
	if (diffuseMap)
	{
		auto desc = diffuseMap->GetD3D12Resource()->GetDesc();

		if (diffuseMap)
		{
			srvDesc.Format = GetSRGBFormat(desc.Format);
		}
		else
		{
			srvDesc.Format = (desc.Format);
		}


		
		switch (type)
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
			}
		}
		diffuseMap->CreateShaderResourceView(&srvDesc, &textureHeap, DiffuseMapIndex);
	}

	if (normalMap)
	{
		srvDesc.Format = normalMap->GetD3D12Resource()->GetDesc().Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
		srvDesc.Texture2D.MipLevels = normalMap->GetD3D12Resource()->GetDesc().MipLevels;
		normalMap->CreateShaderResourceView(&srvDesc, &textureHeap, NormalMapIndex);

	}
}

void Material::Draw(std::shared_ptr<GCommandList> cmdList) const
{
	//TODO: Спросить у Павла, можно ли как-то это обойти, как получилось с билбордами деревьев, через привязку одного большого массива текстур, а дальше в зависимости от индекса и шейдера работать с текстурой как с текстурой нужного мне типа (Texture2DArray, TextureCube)
	if (type == PsoType::SkyBox)
	{
		cmdList->GetGraphicsCommandList()->SetGraphicsRootDescriptorTable(StandardShaderSlot::SkyMap, gpuTextureHandle);
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
		matConstants.DiffuseMapIndex = DiffuseMapIndex;
		matConstants.NormalMapIndex = NormalMapIndex;

		NumFramesDirty--;
	}
}

std::wstring& Material::GetName()
{
	return Name;
}
