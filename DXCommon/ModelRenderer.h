#pragma once
#include "GModel.h"
#include "Renderer.h"
#include "MemoryAllocator.h"

class Transform;
class GCommandList;

class ModelRenderer : public Renderer
{	

	ObjectConstants constantData{};
	
	std::shared_ptr<ConstantBuffer<ObjectConstants>> modelDataBuffer = nullptr;
	
	custom_vector<std::shared_ptr<Material>> meshesMaterials = MemoryAllocator::CreateVector< std::shared_ptr<Material>>();
	
	std::shared_ptr<GDevice> device;
	
protected:
		
	void Draw(std::shared_ptr<GCommandList> cmdList) override;

	void Update() override;

public:

	ModelRenderer(const std::shared_ptr<GDevice> device) : Renderer(), device(device) {  };
	
	std::shared_ptr<GModel> model = nullptr;

	void SetModel(std::shared_ptr<GModel> asset);

	UINT GetMeshesCount() const;

	void SetMeshMaterial(UINT index, const std::shared_ptr<Material> material);
};
