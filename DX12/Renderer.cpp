#include "Renderer.h"

Renderer::Renderer(ID3D12Device* device, ID3D12DescriptorHeap* descriptor): Component()
{
}

void Renderer::Update()
{
	Material->Update();
	
	//if (NumFramesDirty > 0)
	//{
	//	const DirectX::XMMATRIX matTransform = XMLoadFloat4x4(&Material->MatTransform);

	//	MaterialConstants matConstants;
	//	matConstants.DiffuseAlbedo = Material->DiffuseAlbedo;
	//	matConstants.FresnelR0 = Material->FresnelR0;
	//	matConstants.Roughness = Material->Roughness;
	//	XMStoreFloat4x4(&matConstants.MaterialTransform, XMMatrixTranspose(matTransform));

	//	materialBuffer->CopyData(0, matConstants);

	//	// Next FrameResource need to be updated too.
	//	NumFramesDirty--;
	//}
}

void Renderer::Draw(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->IASetVertexBuffers(0, 1, &Mesh->VertexBufferView());
	cmdList->IASetIndexBuffer(&Mesh->IndexBufferView());
	cmdList->IASetPrimitiveTopology(PrimitiveType);

	Material->Draw(cmdList);
	/*CD3DX12_GPU_DESCRIPTOR_HANDLE tex(rendererViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
	tex.Offset(Material->DiffuseSrvHeapIndex, cbvSrvUavDescriptorSize);
	cmdList->SetGraphicsRootDescriptorTable(0, tex);
	cmdList->SetGraphicsRootConstantBufferView(3, materialBuffer->Resource()->GetGPUVirtualAddress());*/
	
	cmdList->DrawIndexedInstanced(IndexCount, 1, StartIndexLocation, BaseVertexLocation, 0);
}
