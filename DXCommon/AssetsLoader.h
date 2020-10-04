#pragma once
#include "GeometryGenerator.h"
#include "GTexture.h"
#include "MemoryAllocator.h"
#include "assimp/scene.h"
#include "assimp/Importer.hpp"
#include "assimp/mesh.h"
#include "assimp/postprocess.h"
class GCommandList;

namespace DXLib
{
	class GCommandQueue;
}

class GTexture;
class GModel;
class GMesh;
class NativeMesh;
class NativeModel;
class GShader;
class GBuffer;
class Material;
class GDevice;

class AssetsLoader
{
	GeometryGenerator geoGen;

	static custom_unordered_map<std::wstring, std::shared_ptr<NativeModel>> loadedModels;
	static custom_unordered_map<std::shared_ptr<NativeMesh>, std::shared_ptr<aiMaterial>> loadedAiMaterialForMesh;
	static custom_unordered_map<std::shared_ptr<NativeMesh>, std::vector<UINT>> loadedTexturesForMesh;

	custom_vector<std::shared_ptr<GTexture>> textures = MemoryAllocator::CreateVector<std::shared_ptr<GTexture>>();
	custom_vector<std::shared_ptr<Material>> materials = MemoryAllocator::CreateVector<std::shared_ptr<Material>>();

	custom_unordered_map<std::wstring, UINT> texturesMap = MemoryAllocator::CreateUnorderedMap<std::wstring, UINT>();
	custom_unordered_map<std::wstring, UINT> materialsMap = MemoryAllocator::CreateUnorderedMap<std::wstring, UINT>();


	std::vector<GeometryGenerator::MeshData> trackGeneratedData;

	std::shared_ptr<GTexture> LoadTextureByAiMaterial(const aiMaterial* material, aiTextureType type,
	                                                  std::wstring directory, std::shared_ptr<GCommandList> cmdList);
	std::shared_ptr<GTexture> LoadTextureByPath(std::wstring name, std::wstring fullPath,
	                                            std::shared_ptr<GCommandList> cmdList, TextureUsage usage);

	void LoadTextureForModel(std::shared_ptr<GModel> model, std::shared_ptr<GCommandList> cmdList
	);

	std::shared_ptr<NativeMesh> CreateSubMesh(aiMesh* mesh, std::wstring modelName) const;
	
	void RecursivlyLoadMeshes(std::shared_ptr<NativeModel> model, aiNode* node, const aiScene* scene) const;

	std::shared_ptr<GDevice> device;

public:

	AssetsLoader(
		std::shared_ptr<GDevice> device);

	UINT GetTextureIndex(std::wstring name);

	UINT GetMaterialIndex(std::wstring name);

	size_t GetLoadTexturesCount() const;

	void AddMaterial(std::shared_ptr<Material> material);

	void AddTexture(std::shared_ptr<GTexture> texture);

	custom_vector<std::shared_ptr<Material>>& GetMaterials();

	custom_vector<std::shared_ptr<GTexture>>& GetTextures();

	std::shared_ptr<GTexture> GetTexture(UINT index);
	std::shared_ptr<Material> GetMaterial(UINT index);

	std::shared_ptr<GModel> GenerateSphere(std::shared_ptr<GCommandList> cmdList, float radius = 1.0f,
	                                     UINT sliceCount = 20, UINT stackCount = 20);

	std::shared_ptr<GModel> GenerateQuad(std::shared_ptr<GCommandList> cmdList, float x = 1.0f, float y = 1.0f,
	                                     float w = 1.0f, float h = 1.0f, float depth = 0.0);

	std::shared_ptr<GModel> CreateModelFromFile(std::shared_ptr<GCommandList> cmdList, std::string filePath);


	void ClearTrackedObjects();
};
