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

	custom_vector<std::shared_ptr<GTexture>> textures = DXAllocator::CreateVector<std::shared_ptr<GTexture>>();
	custom_vector<std::shared_ptr<GShader>> shaders = DXAllocator::CreateVector<std::shared_ptr<GShader>>();
	custom_vector<std::shared_ptr<Material>> materials = DXAllocator::CreateVector<std::shared_ptr<Material>>();
	
	custom_unordered_map<std::wstring, UINT> texturesMap = DXAllocator::CreateUnorderedMap<std::wstring, UINT>();

	custom_unordered_map<std::wstring, UINT> shadersMap = DXAllocator::CreateUnorderedMap<std::wstring, UINT>();

	custom_unordered_map<std::wstring, UINT> materialsMap = DXAllocator::CreateUnorderedMap<std::wstring, UINT>();

	custom_unordered_map<std::shared_ptr<Mesh>, std::shared_ptr<Material>> defaultMaterialForMeshFromFile = DXAllocator::CreateUnorderedMap<std::shared_ptr<Mesh>, std::shared_ptr<Material>>();

	
	std::vector<GeometryGenerator::MeshData> trackGeneratedData;

	std::shared_ptr<GTexture> LoadOrGetTexture(const aiMaterial* material, aiTextureType type,
	                                           std::wstring directory, std::shared_ptr<GCommandList> cmdList);

	void CreateMaterialForMesh(std::shared_ptr<Mesh> mesh,
	                           const aiMaterial* material, const std::shared_ptr<GCommandList> cmdList);
	void RecursivlyLoadMeshes(std::shared_ptr<Model> model, aiNode* node, const aiScene* scene,
		std::shared_ptr<GCommandList> cmdList);
	
public:

	UINT GetTextureIndex(std::wstring name) {
		auto it = texturesMap.find(name);
		if(it == texturesMap.end())
		{
			return  -1;
		}
		return it->second;
	}

	UINT GetShaderIndex(std::wstring name) {
		auto it = shadersMap.find(name);
		if (it == shadersMap.end())
		{
			return  -1;
		}
		return it->second;
	}

	UINT GetMaterialIndex(std::wstring name) {
		auto it = materialsMap.find(name);
		if (it == materialsMap.end())
		{
			return  -1;
		}
		return it->second;
	}
	
	size_t GetLoadTexturesCount() const;

	void AddMaterial(std::shared_ptr<Material> material);
	
	void AddTexture(std::shared_ptr<GTexture> texture);

	custom_vector<std::shared_ptr<Material>>& GetMaterials();

	custom_vector<std::shared_ptr<GTexture>>& GetTextures();
	
	std::shared_ptr<GTexture> GetTexture(UINT index);
	std::shared_ptr<Material> GetMaterials(UINT index);
	std::shared_ptr<Material> GetDefaultMaterial(std::shared_ptr<Mesh> mesh);
	std::shared_ptr<Model> GetModelByName(std::wstring name);

	std::shared_ptr<Model> GenerateSphere( std::shared_ptr<GCommandList> cmdList, float radius = 1.0f, UINT sliceCount = 20, UINT stackCount = 20);

	 std::shared_ptr<Model> GenerateQuad(std::shared_ptr<GCommandList> cmdList, float x = 1.0f, float y = 1.0f, float w = 1.0f, float h = 1.0f, float depth = 0.0);	

	std::shared_ptr<Model> GetOrCreateModelFromFile(std::shared_ptr<DXLib::GCommandQueue> queue, const std::string filePath);

	void ClearTrackedObjects();

	/*static std::shared_ptr<Model> */
};

