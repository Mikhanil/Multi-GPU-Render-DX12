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
		bufferConstant.TextureTransform = transform->TextureTransform.Transpose();
		bufferConstant.World = transform->GetWorldMatrix().Transpose();
		
	}

	if(model!=nullptr)
		model->UpdateGraphicConstantData(bufferConstant);
}

bool ModelRenderer::AddModel(std::shared_ptr<GCommandList> cmdList, const std::string& filePath)
{
	model = Model::LoadFromFile(filePath, cmdList);
	return true;
}

void ModelRenderer::AddModel(std::shared_ptr<Model> asset)
{
	model = asset;
}

UINT ModelRenderer::GetMeshesCount() const
{
	return model->GetMeshesCount();
}

void ModelRenderer::SetMeshMaterial(UINT index, Material* material) const
{
	model->SetMeshMaterial(index, material->GetIndex());
}
