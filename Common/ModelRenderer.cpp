#include "pch.h"
#include "ModelRenderer.h"
#include "GameObject.h"
#include "GCommandList.h"
#include "GMesh.h"
#include "GModel.h"
#include "Transform.h"


void ModelRenderer::Draw(std::shared_ptr<GCommandList> cmdList)
{
	for (int i = 0; i < model->GetMeshesCount(); ++i)
	{
		const auto mesh = model->GetMesh(i);
		
		cmdList->SetRootConstantBufferView(StandardShaderSlot::ObjectData,
		                                   *modelDataBuffer, i);
		mesh->Draw(cmdList);
	}
}

void ModelRenderer::Update()
{
	const auto transform = gameObject->GetTransform();

	if (transform->IsDirty())
	{
		objectWorldData.TextureTransform = transform->TextureTransform.Transpose();
		objectWorldData.World = (transform->GetWorldMatrix() * model->scaleMatrix).Transpose();
		for (int i = 0; i < model->GetMeshesCount(); ++i)
		{
			objectWorldData.MaterialIndex = model->GetMeshMaterial(i)->GetIndex();
			modelDataBuffer->CopyData(i, objectWorldData);
		}		
	}
}

ModelRenderer::ModelRenderer(const std::shared_ptr<GDevice> device,
                             std::shared_ptr<GModel> model): Renderer(), device(device), model(model)
{
	SetModel(model);	
}

void ModelRenderer::SetModel(std::shared_ptr<GModel> asset)
{	
	if(modelDataBuffer == nullptr || modelDataBuffer->GetElementCount() < asset->GetMeshesCount() )
	{
		modelDataBuffer.reset();
		modelDataBuffer = std::make_shared<ConstantBuffer<ObjectConstants>>(device,asset->GetMeshesCount(), asset->GetName());
	}
	
	model = asset;
}



