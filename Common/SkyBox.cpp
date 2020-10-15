#include "pch.h"
#include "SkyBox.h"
#include "GameObject.h"
#include "GCommandList.h"
#include "GMemory.h"
#include "GMesh.h"
#include "GModel.h"
#include "Transform.h"

SkyBox::SkyBox(const std::shared_ptr<GDevice>& device, const std::shared_ptr<GModel>& model, GTexture& skyMapTexture,
               GMemory* srvMemory, UINT offset): ModelRenderer(device, model)
{
	gpuTextureHandle = srvMemory->GetGPUHandle(offset);
	cpuTextureHandle = srvMemory->GetCPUHandle(offset);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	auto desc = skyMapTexture.GetD3D12Resource()->GetDesc();
	srvDesc.Format = GetSRGBFormat(desc.Format);
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MipLevels = desc.MipLevels;
	srvDesc.TextureCube.MostDetailedMip = 0;	
	skyMapTexture.CreateShaderResourceView(&srvDesc, srvMemory, offset);
}

void SkyBox::Draw(std::shared_ptr<GCommandList> cmdList)
{
	cmdList->GetGraphicsCommandList()->SetGraphicsRootDescriptorTable(StandardShaderSlot::SkyMap, gpuTextureHandle);

	for (int i = 0; i < model->GetMeshesCount(); ++i)
	{
		const auto mesh = model->GetMesh(i);

		cmdList->SetRootConstantBufferView(StandardShaderSlot::ObjectData,
		                                   *modelDataBuffer, i);
		mesh->Draw(cmdList);
	}
}

void SkyBox::Update()
{
	const auto transform = gameObject->GetTransform();

	if (transform->IsDirty())
	{
		objectWorldData.TextureTransform = transform->TextureTransform.Transpose();
		objectWorldData.World = (transform->GetWorldMatrix() * model->scaleMatrix).Transpose();
		for (int i = 0; i < model->GetMeshesCount(); ++i)
		{
			modelDataBuffer->CopyData(i, objectWorldData);
		}
	}
}
