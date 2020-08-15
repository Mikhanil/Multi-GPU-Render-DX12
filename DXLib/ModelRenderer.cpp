#include "ModelRenderer.h"

#include "d3dApp.h"
#include "assimp/postprocess.h"
#include "assimp/Importer.hpp"
#include "assimp/mesh.h"
#include "DirectXMesh.h"
#include "GCommandList.h"
#include "STLCustomAllocator.h"

ModelMesh::ModelMesh(std::shared_ptr<GCommandList> cmdList, std::string name,
	custom_vector<Vertex>& vertices,
	custom_vector<DWORD>& indices, D3D12_PRIMITIVE_TOPOLOGY topology): name(std::move(name))
{
	auto& device = DXLib::D3DApp::GetApp().GetDevice();
	
	objectConstantBuffer = std::make_unique<ConstantBuffer<ObjectConstants>>( 1);

	PrimitiveType = topology;

	auto d3d12CmdList = cmdList->GetGraphicsCommandList();
	
	vertexBuffer = std::make_unique<VertexBuffer>(&device, d3d12CmdList.Get(), vertices.data(), vertices.size());
	indexBuffer = std::make_unique<IndexBuffer>(&device, d3d12CmdList.Get(), DXGI_FORMAT_R32_UINT, indices.data(), indices.size());
}

void ModelMesh::Update(Transform* transform)
{
	if (transform->IsDirty())
	{
		bufferConstant.TextureTransform = transform->TextureTransform.Transpose();
		bufferConstant.World = transform->GetWorldMatrix().Transpose();
		bufferConstant.materialIndex = material->GetIndex();
		objectConstantBuffer->CopyData(0, bufferConstant);
	}
}

void ModelMesh::Draw(std::shared_ptr<GCommandList> cmdList) const
{	
	cmdList->SetRootConstantBufferView(StandardShaderSlot::ObjectData,
	                                           objectConstantBuffer->Resource()->GetGPUVirtualAddress());
	cmdList->SetVBuffer(0, 1, &vertexBuffer->VertexBufferView());
	cmdList->SetIBuffer(&indexBuffer->IndexBufferView());
	cmdList->SetPrimitiveTopology(PrimitiveType);
	cmdList->DrawIndexed(indexBuffer->GetElementsCount(), 1, 0, 0, 0);
}

void ModelRenderer::ProcessNode(aiNode* node, const aiScene* scene,
	std::shared_ptr<GCommandList> cmdList)
{
	for (UINT i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		meshes.push_back(ProcessMesh(mesh, scene, cmdList));
	}

	for (UINT i = 0; i < node->mNumChildren; i++)
	{
		ProcessNode(node->mChildren[i], scene, cmdList);
	}
}

void ModelMesh::CalculateTangent(UINT i1, UINT i2, UINT i3, custom_vector<Vertex>& vertex)
{
	UINT idx[3];
	idx[0] = i1;
	idx[1] = i2;
	idx[2] = i3;

	DirectX::XMFLOAT3 pos[3];
	pos[0] = vertex[i1].Position;
	pos[1] = vertex[i2].Position;
	pos[2] = vertex[i3].Position;

	DirectX::XMFLOAT3 normals[3];
	normals[0] = vertex[i1].Normal;
	normals[1] = vertex[i2].Normal;
	normals[2] = vertex[i3].Normal;

	DirectX::XMFLOAT2 t[3];
	t[0] = vertex[i1].TexCord;
	t[1] = vertex[i2].TexCord;
	t[2] = vertex[i3].TexCord;

	DirectX::XMFLOAT4 tangent[3];

	ComputeTangentFrame(idx, 1, pos, normals, t, 3, tangent);

	vertex[i1].TangentU = Vector3(tangent[0].x, tangent[0].y, tangent[0].z);
	vertex[i2].TangentU = Vector3(tangent[1].x, tangent[1].y, tangent[1].z);
	vertex[i3].TangentU = Vector3(tangent[2].x, tangent[2].y, tangent[2].z);
}

ModelMesh ModelRenderer::ProcessMesh(aiMesh* mesh, const aiScene* scene,
	std::shared_ptr<GCommandList> cmdList)
{
	
	// Data to fill
	custom_vector<Vertex> vertices  = DXAllocator::CreateVector<Vertex>();
	custom_vector<DWORD> indices = DXAllocator::CreateVector<DWORD>();

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

			//
			//if ( (i + 1) % 3 == 0)
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
		{
			indices.push_back(face.mIndices[j]);
		}
	}

	for (UINT i = 0; i < indices.size() - 3; i += 3)
	{
		ModelMesh::CalculateTangent(indices[i], indices[i + 1], indices[i + 2], vertices);
	}


	return ModelMesh(cmdList, mesh->mName.C_Str(), vertices, indices);
}

void ModelRenderer::Draw(std::shared_ptr<GCommandList> cmdList)
{
	if (material != nullptr)
		material->Draw(cmdList);

	for (auto&& mesh : meshes)
	{
		mesh.Draw(cmdList);
	}
}

void ModelRenderer::Update()
{
	for (auto&& mesh : meshes)
	{
		mesh.Update(gameObject->GetTransform());
	}
}

bool ModelRenderer::AddModel(std::shared_ptr<GCommandList> cmdList, const std::string& filePath)
{
	Assimp::Importer importer;

	const aiScene* pScene = importer.ReadFile(filePath,
	                                          aiProcess_Triangulate |
	                                          aiProcess_ConvertToLeftHanded);

	if (pScene == nullptr)
		return false;

	ProcessNode(pScene->mRootNode, pScene, cmdList);
	return true;
}
