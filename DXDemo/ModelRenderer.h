#pragma once
#include "Model.h"
#include "Renderer.h"

class Transform;
class GCommandList;

class ModelRenderer : public Renderer
{	
	std::shared_ptr<Model> model = nullptr;

protected:
	void Draw(std::shared_ptr<GCommandList> cmdList) override;

	void Update() override;

public:
	
	void AddModel(std::shared_ptr<Model> asset);

	UINT GetMeshesCount() const;

	void SetMeshMaterial(UINT index, const std::shared_ptr<Material> material) const;
};
