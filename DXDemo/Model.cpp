#include "GBuffer.h"
#include "Model.h"
#include "Mesh.h"
#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/mesh.h"
#include "string"

static Assimp::Importer importer;
static std::unordered_map<std::string, std::shared_ptr<Model>> cashedLoadTextures;


void Model::ProcessNode(std::shared_ptr<Model> model, aiNode* node, const aiScene* scene, std::shared_ptr<GCommandList> cmdList)
{
	for (UINT i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		
		model->meshes.push_back(std::move(CreateSubMesh(mesh, model, cmdList)));
	}

	for (UINT i = 0; i < node->mNumChildren; i++)
	{
		ProcessNode(model, node->mChildren[i], scene, cmdList);
	}
}

Mesh Model::CreateSubMesh(aiMesh* mesh, std::shared_ptr<Model> model, std::shared_ptr<GCommandList> cmdList)
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

	Mesh submesh(model->name + L" " + AnsiToWString(mesh->mName.C_Str()));

	switch (mesh->mPrimitiveTypes)
	{
	case (aiPrimitiveType_TRIANGLE):
		{
			submesh.SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			break;
		}

	case (aiPrimitiveType_LINE):
		{
			submesh.SetTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
			break;
		}

	case (aiPrimitiveType_POINT):
		{
			submesh.SetTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
			break;
		}
	}

	Mesh::RecalculateTangent(indices.data(), indices.size(), vertices.data());
	
	submesh.ChangeIndexes(cmdList, indices.data(), indices.size());	
	submesh.ChangeVertices(cmdList, vertices.data(), vertices.size());

	return submesh;
}

void Model::Draw(std::shared_ptr<GCommandList> cmdList)
{
	for (auto&& mesh : meshes)
	{
		mesh.Draw(cmdList, StandardShaderSlot::ObjectData);
	}
}

void Model::UpdateGraphicConstantData(ObjectConstants constantData)
{
	for (auto&& mesh : meshes)
	{
		mesh.UpdateGraphicConstantData(constantData);
	}
}


std::shared_ptr<Model> Model::LoadFromFile(const std::string& filePath, std::shared_ptr<GCommandList> cmdList)
{
	const auto it = cashedLoadTextures.find(filePath);
	if (it != cashedLoadTextures.end())
	{
		return it->second;
	}
	
	const aiScene* pScene = importer.ReadFile(filePath,
		aiProcess_Triangulate  |
		aiProcess_ConvertToLeftHanded );

	if (pScene == nullptr)
	{
		assert("Model Path dosen't exist or wrong file");
		return nullptr;
	}

	auto model = std::make_shared<Model>(AnsiToWString(filePath));

	ProcessNode(model, pScene->mRootNode, pScene, cmdList);

	cashedLoadTextures[filePath] = std::move(model);
	
	return cashedLoadTextures[filePath];
}

UINT Model::GetMeshesCount() const
{
	return meshes.size();
}

Model::Model(const std::wstring modelName): name(modelName)
{
}


void Model::SetMeshMaterial(const UINT submesh,const  UINT materialIndex)
{
	meshes[submesh].SetMaterialIndex(materialIndex);
}

