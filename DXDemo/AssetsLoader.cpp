#include "AssetsLoader.h"
#include "GCommandList.h"
#include "GCommandQueue.h"
#include "GeometryGenerator.h"
#include "GraphicPSO.h"
#include "GTexture.h"
#include "Material.h"
#include "Mesh.h"
#include "Model.h"
#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/mesh.h"

using namespace DXLib;

static Assimp::Importer importer;

std::shared_ptr<Model> AssetsLoader::GenerateSphere(std::shared_ptr<GCommandList> cmdList, float radius, UINT sliceCount, UINT stackCount)
{
	auto it = models.find(L"sphere");
	if (it != models.end()) return  it->second;
		
    GeometryGenerator::MeshData sphere = geoGen.CreateSphere(radius, sliceCount, stackCount);

	auto sphereMesh = std::make_shared<Mesh>();
	sphereMesh->ChangeVertices(cmdList, sphere.Vertices.data(), sphere.Vertices.size());
	sphereMesh->ChangeIndexes(cmdList, sphere.Indices32.data(), sphere.Indices32.size());
	sphereMesh->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	auto sphereModel = std::make_shared<Model>(L"sphere");
	sphereModel->AddMesh(sphereMesh);
	models[sphereModel->GetName()] = std::move(sphereModel);
	trackGeneratedData.push_back(sphere);
	
	return models[L"sphere"];
}

std::shared_ptr<Model> AssetsLoader::GenerateQuad(std::shared_ptr<GCommandList> cmdList, float x, float y, float w,
	float h, float depth)
{
	auto it = models.find(L"quad");
	if (it != models.end()) return  it->second;

	GeometryGenerator::MeshData genMesh = geoGen.CreateQuad(x,y,w,h,depth);

	auto mesh = std::make_shared<Mesh>();
	mesh->ChangeVertices(cmdList, genMesh.Vertices.data(), genMesh.Vertices.size());
	mesh->ChangeIndexes(cmdList, genMesh.Indices32.data(), genMesh.Indices32.size());
	mesh->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	auto model = std::make_shared<Model>(L"quad");
	model->AddMesh(mesh);
	models[model->GetName()] = std::move(model);
	trackGeneratedData.push_back(genMesh);
	return models[L"quad"];
}


std::shared_ptr<Mesh> CreateSubMesh(aiMesh* mesh, std::wstring name, std::shared_ptr<GCommandList> cmdList)
{
	// Data to fill
	std::vector<Vertex> vertices;
	std::vector<DWORD> indices;

	//Get vertices
	for (UINT i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex{};

		vertex.Position.x = mesh->mVertices[i].x;
		vertex.Position.y = mesh->mVertices[i].y;
		vertex.Position.z = mesh->mVertices[i].z;

		vertex.Normal.x = mesh->mNormals[i].x;
		vertex.Normal.y = mesh->mNormals[i].y;
		vertex.Normal.z = mesh->mNormals[i].z;

		if (mesh->HasTextureCoords(0))
		{
			vertex.TexCord.x = mesh->mTextureCoords[0][i].x;
			vertex.TexCord.y = mesh->mTextureCoords[0][i].y;
		}

		if (mesh->HasTangentsAndBitangents())
		{
			vertex.TangentU.x = mesh->mTangents[i].x;
			vertex.TangentU.y = mesh->mTangents[i].y;
			vertex.TangentU.z = mesh->mTangents[i].z;
		}

		vertices.push_back(vertex);
	}

	//Get indices
	for (UINT i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];

		for (UINT j = 0; j < face.mNumIndices; j++)
		{
			indices.push_back(face.mIndices[j]);
		}
	}

	auto submesh = std::make_shared<Mesh>(name + L" " + AnsiToWString(mesh->mName.C_Str()));

	submesh->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);	

	RecalculateTangent(indices.data(), indices.size(), vertices.data());

	submesh->ChangeIndexes(cmdList, indices.data(), indices.size());
	submesh->ChangeVertices(cmdList, vertices.data(), vertices.size());

	return submesh;
}

std::shared_ptr<GTexture> AssetsLoader::LoadOrGetTexture(const aiMaterial* material, const aiTextureType type,
                                                         const std::wstring directory,
                                                         std::shared_ptr<GCommandList> cmdList)
{
		
	aiString str;
	auto result = material->GetTexture(type, 0, &str);	

	std::wstring modelTexturePath(AnsiToWString(str.C_Str()));

	if(modelTexturePath.find(L"\\") != std::wstring::npos)
	{
		auto fileName = modelTexturePath.substr(modelTexturePath.find_last_of('\\'), modelTexturePath.size() - modelTexturePath.find_last_of('\\'));

		modelTexturePath = fileName.replace(fileName.find(L"\\"), 1, L"");
	}
	
	std::wstring textureName = modelTexturePath;
	std::wstring texturePath = directory + L"\\" + textureName;

	const auto it = textures.find(textureName);
	if (it != textures.end()) return it->second;
	
	OutputDebugStringW((texturePath + L"\n").c_str());

	auto texture = GTexture::LoadTextureFromFile(texturePath, cmdList, type == aiTextureType_DIFFUSE ? TextureUsage::Albedo : TextureUsage::Normalmap);
	texture->SetName(textureName);
	
	textures[textureName] = std::move(texture);
	return textures[textureName];
}

void  AssetsLoader::CreateMaterialForMesh(std::shared_ptr<Mesh> mesh, const aiMaterial* material, const std::shared_ptr<GCommandList> cmdList)
{
	aiString name;
	material->Get(AI_MATKEY_NAME, name);
	auto materialName = mesh->GetName() + L" " + AnsiToWString(name.C_Str());

	const auto it = materials.find(materialName);
	if (it != materials.end())
	{
		defaultMaterialForMeshFromFile[mesh] = it->second;		
		return;
	}


	
	auto directory = mesh->GetName().substr(0, mesh->GetName().find_last_of('\\'));

	auto count = material->GetTextureCount(aiTextureType_DIFFUSE);

	std::shared_ptr<GTexture> diffuse;

	if(count > 0)
	{
		diffuse = LoadOrGetTexture(material, aiTextureType_DIFFUSE, directory, cmdList);
	}
	else
	{

		const auto textureIt = textures.find(L"seamless");
		if (textureIt != textures.end())
		{
			diffuse = textureIt->second;
		}
		else
		{
			auto defaultNormal = GTexture::LoadTextureFromFile(L"Data\\Textures\\seamless_grass.jpg", cmdList,
				TextureUsage::Diffuse);
			defaultNormal->SetName(L"seamless");

			textures[defaultNormal->GetName()] = std::move(defaultNormal);

			diffuse = textures[L"seamless"];
		}		
	}

	count = material->GetTextureCount(aiTextureType_HEIGHT);

	std::shared_ptr<GTexture> normal;
	
	if (count > 0)
	{
		normal = LoadOrGetTexture(material, aiTextureType_HEIGHT, directory, cmdList);
	}
	else
	{
		count = material->GetTextureCount(aiTextureType_NORMALS);

		if(count > 0)
		{
			normal = LoadOrGetTexture(material, aiTextureType_NORMALS, directory, cmdList);
		}
		else {

			const auto textureIt = textures.find(L"defaultNormalMap");
			if (textureIt != textures.end())
			{
				normal = textureIt->second;
			}
			else
			{
				auto defaultNormal = GTexture::LoadTextureFromFile(L"Data\\Textures\\default_nmap.dds", cmdList,
					TextureUsage::Normalmap);
				defaultNormal->SetName(L"defaultNormalMap");

				textures[defaultNormal->GetName()] = std::move(defaultNormal);

				normal = textures[L"defaultNormalMap"];
			}
		}
	}	 


	
	auto mat = std::make_shared<Material>(materialName);
	mat->SetDiffuseTexture(diffuse);
	mat->SetNormalMap(normal);
	mat->FresnelR0 = Vector3::One * 0.05;
	mat->Roughness = 0.95;

	AddMaterial(mat);
	defaultMaterialForMeshFromFile[mesh] = mat;
}


void  AssetsLoader::RecursivlyLoadMeshes(std::shared_ptr<Model> model, aiNode* node, const aiScene* scene, const std::shared_ptr<GCommandList> cmdList)
{
	for (UINT i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* aMesh = scene->mMeshes[node->mMeshes[i]];				
		
		auto mesh = CreateSubMesh(aMesh, model->GetName(), cmdList);
		CreateMaterialForMesh(mesh, scene->mMaterials[aMesh->mMaterialIndex], cmdList);
		meshes.push_back(std::move(mesh));
		model->AddMesh(meshes.back());
		
	}

	for (UINT i = 0; i < node->mNumChildren; i++)
	{
		RecursivlyLoadMeshes(model, node->mChildren[i], scene, cmdList);
	}
}

size_t AssetsLoader::GetLoadTexturesCount() const
{
	return textures.size();
}

void AssetsLoader::AddMaterial(std::shared_ptr<Material> material)
{
	materials[material->GetName()] = std::move( material);
}

void AssetsLoader::AddTexture(std::shared_ptr<GTexture> texture)
{
	textures[texture->GetName()] = std::move(texture);
}

custom_unordered_map<std::wstring, std::shared_ptr<Material>>& AssetsLoader::GetMaterials()
{
	return materials;
}


custom_unordered_map<std::wstring, std::shared_ptr<GTexture>>& AssetsLoader::GetTextures()
{
	return textures;
}


std::shared_ptr<GTexture> AssetsLoader::GetTexture(std::wstring name)
{
	const auto it = textures.find(name);

	if (it != textures.end()) return it->second;

	return nullptr;
}

std::shared_ptr<Material> AssetsLoader::GetMaterials(std::wstring name)
{
	const auto it = materials.find(name);

	if (it != materials.end()) return it->second;

	return nullptr;
}

std::shared_ptr<Material> AssetsLoader::GetDefaultMaterial(std::shared_ptr<Mesh> mesh)
{
	return defaultMaterialForMeshFromFile[mesh];
}

std::shared_ptr<Model> AssetsLoader::GetModelByName(const std::wstring name)
{
	auto it = models.find((name));
	if (it != models.end())
	{
		return it->second;
	}

	return nullptr;
}


std::shared_ptr<Model> AssetsLoader::GetOrCreateModelFromFile(std::shared_ptr<GCommandQueue> queue,
                                                              const std::string filePath)
{
	auto it = models.find(AnsiToWString(filePath));
	if (it != models.end()) return it->second;


	const aiScene* sceneModel = importer.ReadFile(filePath,
		 aiProcess_Triangulate   | aiProcess_GenNormals |
	                                          aiProcess_ConvertToLeftHanded);


	
	auto basePath = filePath;
	
	
	
	if (sceneModel == nullptr)
	{
		assert("Model Path dosen't exist or wrong file");
		return nullptr;
	}

	auto model = std::make_shared<Model>(AnsiToWString(filePath));

	const auto cmdList = queue->GetCommandList();
		
	RecursivlyLoadMeshes(model, sceneModel->mRootNode, sceneModel, cmdList);

	queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));

	models[model->GetName()] = std::move(model);

	importer.FreeScene();
	
	return models[AnsiToWString(filePath)];	
}

void AssetsLoader::ClearTrackedObjects()
{
	trackGeneratedData.clear();	
}


