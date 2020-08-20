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
		bufferConstant.MaterialIndex = material->GetIndex();
		SendDataToConstantBuffer();
	}
}

