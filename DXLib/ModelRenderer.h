#pragma once
#include "GBuffer.h"
#include "Renderer.h"
#include "STLCustomAllocator.h"
#include "assimp/scene.h"

class Transform;
class GCommandList;
class Model;

class ModelRenderer : public Renderer
{	
	std::shared_ptr<Model> model = nullptr;

	void Draw(std::shared_ptr<GCommandList> cmdList) override;

	void Update() override;

public:
	
	bool AddModel(std::shared_ptr<GCommandList> cmdList, const std::string& filePath);

	UINT GetMeshesCount() const;

	void SetMeshMaterial(UINT index, Material* material) const;
};
