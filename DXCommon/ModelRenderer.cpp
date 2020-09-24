#include "pch.h"
#include "ModelRenderer.h"
#include "GModel.h"
#include "GameObject.h"
#include "GCommandList.h"
#include "Transform.h"


void ModelRenderer::Draw(std::shared_ptr<GCommandList> cmdList)
{
	if (material != nullptr)
		material->Draw(cmdList);

	if(model != nullptr)
	{
		auto modelBufferData = modelDataBuffers[cmdList->GetDevice()];

		if(modelBufferData == nullptr)
		{
			modelBufferData = std::make_shared<ConstantBuffer<ObjectConstants>>(cmdList->GetDevice(), model->GetMeshesCount(), model->GetName());

			modelDataBuffers[cmdList->GetDevice()] = (modelBufferData);
		}

		
		for (int i = 0; i < model->GetMeshesCount(); ++i)
		{
			const auto mesh = model->GetMesh(i);
			cmdList->SetRootConstantBufferView(StandardShaderSlot::ObjectData,
				*modelBufferData, i);
			cmdList->SetVBuffer(0, 1, mesh->GetVertexView(cmdList->GetDevice()));
			cmdList->SetIBuffer(mesh->GetIndexView(cmdList->GetDevice()));
			cmdList->SetPrimitiveTopology(mesh->GetPrimitiveType());
			cmdList->DrawIndexed(mesh->GetIndexCount());
		}		
	}
}

void ModelRenderer::Update()
{
	auto transform = gameObject->GetTransform();

	if (transform->IsDirty())
	{
		if (model == nullptr) return;

		constantData.TextureTransform = transform->TextureTransform.Transpose();
		constantData.World = (transform->GetWorldMatrix() * model->scaleMatrix).Transpose();
		for (int i = 0; i < model->GetMeshesCount(); ++i)
		{
			constantData.MaterialIndex = meshesMaterials[i]->GetIndex();

			for (auto && pair : modelDataBuffers)
			{
				pair.second->CopyData(i, constantData);
			}
		}		
	}
}

void ModelRenderer::SetModel(std::shared_ptr<GModel> asset)
{
	if(meshesMaterials.size() < asset->GetMeshesCount())
	{
		meshesMaterials.resize(asset->GetMeshesCount());
	}

	auto it = modelDataBuffers.find(device);
	if(it == modelDataBuffers.end())
	{
		auto modelDataBuffer = std::make_shared<ConstantBuffer<ObjectConstants>>(device, asset->GetMeshesCount(), asset->GetName());

		modelDataBuffers[device] = std::move( modelDataBuffer);		
	}
	
	
	
	model = asset;
}

UINT ModelRenderer::GetMeshesCount() const
{
	return model->GetMeshesCount();
}

void ModelRenderer::SetMeshMaterial(UINT index, const std::shared_ptr<Material> material)
{
	meshesMaterials[index] = material;	
}
