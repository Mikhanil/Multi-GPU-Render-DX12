#pragma once
#include "Model.h"
#include "Renderer.h"

class Transform;
class GCommandList;

class ModelRenderer : public Renderer
{	
	std::shared_ptr<Model> model = nullptr;

	void Draw(std::shared_ptr<GCommandList> cmdList) override;

	void Update() override;

public:
	
	bool AddModel(std::shared_ptr<GCommandList> cmdList, const std::string& filePath);

	void AddModel(std::shared_ptr<Model> asset);

	UINT GetMeshesCount() const;

	void SetMeshMaterial(UINT index, Material* material) const;
};
