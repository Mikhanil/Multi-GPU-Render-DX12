#include "Renderer.h"

Renderer::Renderer(): Component()
{
}

void Renderer::Update()
{
	Material->Update();	
}

void Renderer::Draw(ID3D12GraphicsCommandList* cmdList)
{
	Material->Draw(cmdList);
	
	cmdList->IASetVertexBuffers(0, 1, &Mesh->VertexBufferView());
	cmdList->IASetIndexBuffer(&Mesh->IndexBufferView());
	cmdList->IASetPrimitiveTopology(PrimitiveType);
	
	
	
	cmdList->DrawIndexedInstanced(IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0);
}
