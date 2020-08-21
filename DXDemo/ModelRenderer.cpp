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
		model->Draw(cmdList);
	}
}

void ModelRenderer::Update()
{
	auto transform = gameObject->GetTransform();
	
	if (transform->IsDirty())
	{
		bufferConstant.TextureTransform = transform->TextureTransform;
		bufferConstant.World = transform->GetWorldMatrix();
		bufferConstant.gObjPad0 = transform->GetPosition().z;
	}

	if(model!=nullptr)
		model->UpdateGraphicConstantData(bufferConstant);
}


void ModelRenderer::AddModel(std::shared_ptr<Model> asset)
{
	model = asset;
}

UINT ModelRenderer::GetMeshesCount() const
{
	return model->GetMeshesCount();
}

void ModelRenderer::SetMeshMaterial(UINT index, const std::shared_ptr<Material> material) const
{
	model->SetMeshMaterial(index, material);
}
