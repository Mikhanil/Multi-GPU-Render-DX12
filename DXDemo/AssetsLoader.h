#pragma once
#include <memory>
#include <string>
#include <vector>
#include "assimp/scene.h"

#include "DXAllocator.h"
#include "GCommandQueue.h"
#include "GeometryGenerator.h"
#include "Windows.h"

class GCommandList;
class GTexture;
class Model;
class Mesh;
class GShader;
class GBuffer;
class Material;

class AssetsLoader
{
	GeometryGenerator geoGen;
	custom_unordered_map<std::wstring, std::shared_ptr<Model>> models = DXAllocator::CreateUnorderedMap<
		std::wstring, std::shared_ptr<Model>>();
	
	custom_vector<std::shared_ptr<Mesh>> meshes = DXAllocator::CreateVector<std::shared_ptr<Mesh>>();

	custom_unordered_map<std::wstring, std::shared_ptr<GTexture>> textures = DXAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<GTexture>>();

	custom_unordered_map<std::wstring, std::shared_ptr<GShader>> shaders = DXAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<GShader>>();

	custom_unordered_map<std::wstring, std::shared_ptr<Material>> materials = DXAllocator::CreateUnorderedMap<std::wstring, std::shared_ptr<Material>>();

	custom_unordered_map<std::shared_ptr<Mesh>, std::shared_ptr<Material>> defaultMaterialForMeshFromFile = DXAllocator::CreateUnorderedMap<std::shared_ptr<Mesh>, std::shared_ptr<Material>>();

	
	std::vector<GeometryGenerator::MeshData> trackGeneratedData;

	std::shared_ptr<GTexture> LoadOrGetTexture(const aiMaterial* material, aiTextureType type,
	                                           std::wstring directory, std::shared_ptr<GCommandList> cmdList);

	void CreateMaterialForMesh(std::shared_ptr<Mesh> mesh,
	                           const aiMaterial* material, const std::shared_ptr<GCommandList> cmdList);
	void RecursivlyLoadMeshes(std::shared_ptr<Model> model, aiNode* node, const aiScene* scene,
		std::shared_ptr<GCommandList> cmdList);
	
public:

	size_t GetLoadTexturesCount() const;

	void AddMaterial(std::shared_ptr<Material> material);
	
	void AddTexture(std::shared_ptr<GTexture> texture);

	custom_unordered_map<std::wstring, std::shared_ptr<Material>>& GetMaterials();

	custom_unordered_map<std::wstring, std::shared_ptr<GTexture>>& GetTextures();
	
	std::shared_ptr<GTexture> GetTexture(std::wstring name);
	std::shared_ptr<Material> GetMaterials(std::wstring name);
	std::shared_ptr<Material> GetDefaultMaterial(std::shared_ptr<Mesh> mesh);
	std::shared_ptr<Model> GetModelByName(std::wstring name);

	std::shared_ptr<Model> GenerateSphere( std::shared_ptr<GCommandList> cmdList, float radius = 1.0f, UINT sliceCount = 20, UINT stackCount = 20);

	 std::shared_ptr<Model> GenerateQuad(std::shared_ptr<GCommandList> cmdList, float x = 1.0f, float y = 1.0f, float w = 1.0f, float h = 1.0f, float depth = 0.0);	

	std::shared_ptr<Model> GetOrCreateModelFromFile(std::shared_ptr<DXLib::GCommandQueue> queue, const std::string filePath);

	void ClearTrackedObjects();

	/*static std::shared_ptr<Model> */
};

