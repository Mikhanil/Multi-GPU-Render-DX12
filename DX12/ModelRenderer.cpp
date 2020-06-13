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

		if(mesh->HasTangentsAndBitangents())
		{
			vertex.TangentU.x = mesh->mTangents[i].x;
			vertex.TangentU.y = mesh->mTangents[i].y;
			vertex.TangentU.z = mesh->mTangents[i].z;
		}
		else
		{
			auto t1 = vertex.Normal.Cross(Vector3(0, 0, 1));
			auto t2 = vertex.Normal.Cross(Vector3(0, 1, 0));
			if (t1.Length() - t2.Length() < sqrt(3))
			{
				vertex.TangentU = t1;
			}
			else
			{
				vertex.TangentU = t2;
				//binormal = cross(tangent, normal);
			}
				
			
			//if (mesh->HasTextureCoords(0) && (i + 1) % 3 == 0)
			//{
			//	Vector3 v0 = Vector3{ mesh->mVertices[i - 2].x, mesh->mVertices[i - 2].y, mesh->mVertices[i - 2].z };
			//	Vector3 v1 = Vector3{ mesh->mVertices[i - 1].x, mesh->mVertices[i - 1].y, mesh->mVertices[i - 1].z };
			//	Vector3 v2 = Vector3{ mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };

			//	Vector2 v0UV = Vector2{ mesh->mTextureCoords[0][i - 2].x ,mesh->mTextureCoords[0][i - 2].y };
			//	Vector2 v1UV = Vector2{ mesh->mTextureCoords[0][i - 1].x ,mesh->mTextureCoords[0][i - 1].y };
			//	Vector2 v2UV = Vector2{ mesh->mTextureCoords[0][i].x ,mesh->mTextureCoords[0][i].y };

			//	Vector3 e0 = v1 - v0;
			//	Vector3 e1 = v2 - v0;
			//	Vector2 e0uv = v1UV - v0UV;
			//	Vector2 e1uv = v2UV - v0UV;

			//	float cp = e0uv.y * e1uv.x - e0uv.x * e1uv.y;

			//	if (cp != 0.0f) {
			//		float k = 1.0f / cp;
			//		vertex.TangentU = (e0 * -e1uv.y + e1 * e0uv.y) * k;
			//		//bitangent = (e0 * -e1uv.x + e1 * e0uv.x) * k;

			//		vertex.TangentU.Normalize();
			//		//bitangent.Normalize();
			//	}
			//}
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
