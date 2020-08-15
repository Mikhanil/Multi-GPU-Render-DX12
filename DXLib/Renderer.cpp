#include "Renderer.h"
#include "GameObject.h"
#include "d3dApp.h"
#include "GCommandList.h"

Renderer::Renderer(): Component()
{
	auto& device = DXLib::D3DApp::GetApp().GetDevice();

	objectConstantBuffer = std::make_unique<ConstantBuffer<ObjectConstants>>(1);
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

void Renderer::Draw(std::shared_ptr<GCommandList> cmdList)
{
	cmdList->SetRootConstantBufferView(StandardShaderSlot::ObjectData,
	                                           objectConstantBuffer->Resource()->GetGPUVirtualAddress());

	material->Draw(cmdList);
	
	cmdList->SetVBuffer(0, 1, &mesh->VertexBufferView());
	cmdList->SetIBuffer(&mesh->IndexBufferView());
	cmdList->SetPrimitiveTopology(PrimitiveType);


	cmdList->DrawIndexed(IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0);
}
