#include "ModelRenderer.h"
#include "Model.h"
#include "GameObject.h"
#include "GCommandList.h"
#include "Transform.h"


void ModelRenderer::Draw(std::shared_ptr<GCommandList> cmdList)
{
	if (material != nullptr)
		material->Draw(cmdList);

	if(model != nullptr)
	{
		for (int i = 0; i < model->GetMeshesCount(); ++i)
		{
			const auto mesh = model->GetMesh(i);
			cmdList->SetRootConstantBufferView(StandardShaderSlot::ObjectData,
				*modelDataBuffer, i);
			cmdList->SetVBuffer(0, 1, mesh->GetVertexView());
			cmdList->SetIBuffer(mesh->GetIndexView());
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
			modelDataBuffer->CopyData(i, constantData);
		}		
	}
}

void ModelRenderer::SetModel(std::shared_ptr<Model> asset)
{
	if(meshesMaterials.size() < asset->GetMeshesCount())
	{
		meshesMaterials.resize(asset->GetMeshesCount());
	}
	
	if(modelDataBuffer == nullptr || modelDataBuffer->GetElementCount() < asset->GetMeshesCount() )
	{
		modelDataBuffer.reset();
		modelDataBuffer = std::make_unique<ConstantBuffer<ObjectConstants>>(asset->GetMeshesCount(), asset->GetName());
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
