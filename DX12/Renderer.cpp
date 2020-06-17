#include "Renderer.h"
#include "GameObject.h"
#include "d3dApp.h"
Renderer::Renderer(): Component()
{
	auto& device = D3DApp::GetApp()->GetDevice();
	
	objectConstantBuffer = std::make_unique<ConstantBuffer<ObjectConstants>>(&device, 1);
}

void Renderer::SendDataToConstantBuffer()
{
	objectConstantBuffer->CopyData(0, bufferConstant);
}

void Renderer::Update()
{
	auto transform = gameObject->GetTransform();
	if (transform->IsDirty())
	{
		bufferConstant.TextureTransform = transform->TextureTransform.Transpose();
		bufferConstant.World = transform->GetWorldMatrix().Transpose();
		bufferConstant.materialIndex = material->GetIndex();
		SendDataToConstantBuffer();
	}	
}

void Renderer::Draw(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->SetGraphicsRootConstantBufferView(StandardShaderSlot::ObjectData, objectConstantBuffer->Resource()->GetGPUVirtualAddress());
	
	material->Draw(cmdList);
	
	cmdList->IASetVertexBuffers(0, 1, &mesh->VertexBufferView());
	cmdList->IASetIndexBuffer(&mesh->IndexBufferView());
	cmdList->IASetPrimitiveTopology(PrimitiveType);
	
	
	
	cmdList->DrawIndexedInstanced(IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0);
}
