#include "ModelRenderer.h"
#include "assimp/postprocess.h"

void ModelRenderer::ProcessNode(aiNode* node, const aiScene* scene, ID3D12Device* device,
                                ID3D12GraphicsCommandList* cmdList)
{
	for (UINT i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		meshes.push_back(ProcessMesh(mesh, scene, device, cmdList));
	}

	for (UINT i = 0; i < node->mNumChildren; i++)
	{
		ProcessNode(node->mChildren[i], scene, device, cmdList);
	}
}

ModelMesh ModelRenderer::ProcessMesh(aiMesh* mesh, const aiScene* scene, ID3D12Device* device,
                                     ID3D12GraphicsCommandList* cmdList)
{
	// Data to fill
	std::vector<Vertex> vertices;
	std::vector<DWORD> indices;

	//Get vertices
	for (UINT i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex{};

		vertex.position.x = mesh->mVertices[i].x;
		vertex.position.y = mesh->mVertices[i].y;
		vertex.position.z = mesh->mVertices[i].z;

		vertex.normal.x = mesh->mNormals[i].x;
		vertex.normal.y = mesh->mNormals[i].y;
		vertex.normal.z = mesh->mNormals[i].z;

		if (mesh->mTextureCoords[0])
		{
			vertex.texCord.x = mesh->mTextureCoords[0][i].x;
			vertex.texCord.y = mesh->mTextureCoords[0][i].y;
		}

		vertices.push_back(vertex);
	}

	//Get indices
	for (UINT i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];

		for (UINT j = 0; j < face.mNumIndices; j++)
			indices.push_back(face.mIndices[j]);
	}

	return ModelMesh(device, cmdList, vertices, indices);
}

void ModelRenderer::Draw(ID3D12GraphicsCommandList* cmdList)
{
	Material->Draw(cmdList);

	for (auto&& mesh : meshes)
	{
		mesh.Draw(cmdList);
	}
}

bool ModelRenderer::AddModel(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const std::string& filePath)
{
	Assimp::Importer importer;

	const aiScene* pScene = importer.ReadFile(filePath,
	                                          aiProcess_Triangulate |
	                                          aiProcess_ConvertToLeftHanded);

	if (pScene == nullptr)
		return false;

	ProcessNode(pScene->mRootNode, pScene, device, cmdList);
	return true;
}
