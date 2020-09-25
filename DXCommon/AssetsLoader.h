#pragma once
#include <memory>
#include <string>
#include <vector>
#include "assimp/scene.h"
#include "MemoryAllocator.h"
#include "GeometryGenerator.h"

class GCommandList;

namespace DXLib
{
	class GCommandQueue;
}
class GTexture;
class GModel;
class GMesh;
class MeshData;
class GShader;
class GBuffer;
class Material;
class GDevice;

class AssetsLoader
{
	GeometryGenerator geoGen;
	custom_unordered_map<std::wstring, std::shared_ptr<GModel>> models = MemoryAllocator::CreateUnorderedMap<
		std::wstring, std::shared_ptr<GModel>>();

	custom_unordered_map<std::wstring, std::shared_ptr< MeshData>> meshesData = MemoryAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<MeshData>>();
	custom_vector<std::shared_ptr<GMesh>> meshes = MemoryAllocator::CreateVector<std::shared_ptr<GMesh>>();
	custom_vector<std::shared_ptr<GTexture>> textures = MemoryAllocator::CreateVector<std::shared_ptr<GTexture>>();
	custom_vector<std::shared_ptr<Material>> materials = MemoryAllocator::CreateVector<std::shared_ptr<Material>>();
	
	custom_unordered_map<std::wstring, UINT> texturesMap = MemoryAllocator::CreateUnorderedMap<std::wstring, UINT>();
	
	custom_unordered_map<std::wstring, UINT> materialsMap = MemoryAllocator::CreateUnorderedMap<std::wstring, UINT>();

	custom_unordered_map<std::wstring, std::shared_ptr<Material>> defaultMaterialForMeshFromFile = MemoryAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<Material>>();

	
	std::vector<GeometryGenerator::MeshData> trackGeneratedData;

	std::shared_ptr<GTexture> LoadOrGetTexture(const aiMaterial* material, aiTextureType type,
	                                           std::wstring directory, std::shared_ptr<GCommandList> cmdList);

	void CreateMaterialForMesh(std::shared_ptr<GMesh> mesh,
	                           const aiMaterial* material, const std::shared_ptr<GCommandList> cmdList);
	void RecursivlyLoadMeshes(std::shared_ptr<GModel> model, aiNode* node, const aiScene* scene,
		std::shared_ptr<GCommandList> cmdList);

	std::shared_ptr<GDevice> device;
	
public:

	AssetsLoader(const
		std::shared_ptr<GDevice> device);

	custom_unordered_map<std::wstring, std::shared_ptr<Material>>& GetDefaultMaterialForMeshes()
	{
		return  defaultMaterialForMeshFromFile;
	}

	void SetModelDefaultMaterial(std::shared_ptr<GMesh> mesh, std::shared_ptr<Material> material);

	void AddMesh(std::shared_ptr<GMesh> mesh)
	{
		meshes.push_back(mesh);
	}

	std::shared_ptr<GMesh> GetMesh(std::wstring name);

	UINT GetTextureIndex(std::wstring name);

	UINT GetMaterialIndex(std::wstring name);

	size_t GetLoadTexturesCount() const;

	void AddMaterial(std::shared_ptr<Material> material);
	
	void AddTexture(std::shared_ptr<GTexture> texture);
	void AddModel(std::shared_ptr<GModel> model);

	custom_vector<std::shared_ptr<Material>>& GetMaterials();

	custom_vector<std::shared_ptr<GTexture>>& GetTextures();
	
	std::shared_ptr<GTexture> GetTexture(UINT index);
	std::shared_ptr<Material> GetMaterials(UINT index);
	std::shared_ptr<Material> GetDefaultMaterial(std::shared_ptr<GMesh> mesh);
	std::shared_ptr<GModel> GetModelByName(std::wstring name);

	std::shared_ptr<GModel> GenerateSphere( std::shared_ptr<GCommandList> cmdList, float radius = 1.0f, UINT sliceCount = 20, UINT stackCount = 20);

	 std::shared_ptr<GModel> GenerateQuad(std::shared_ptr<GCommandList> cmdList, float x = 1.0f, float y = 1.0f, float w = 1.0f, float h = 1.0f, float depth = 0.0);
	std::shared_ptr<GMesh> CreateSubMesh(aiMesh* mesh, std::wstring name, std::shared_ptr<GCommandList> cmdList);

	std::shared_ptr<GModel> GetOrCreateModelFromFile(std::shared_ptr<DXLib::GCommandQueue> queue, const std::string filePath);



	void ClearTrackedObjects();
};

