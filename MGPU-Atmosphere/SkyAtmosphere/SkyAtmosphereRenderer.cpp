#include "SkyAtmosphereRenderer.h"
#include "GDescriptor.h"
#include "GMesh.h"
#include "GModel.h"

SkyAtmosphereRenderer::SkyAtmosphereRenderer(
	const std::shared_ptr<GDevice>& device, const std::shared_ptr<GModel>& model,
	const GDescriptor* rayMarchSrvMemory,
	UINT offset, uint32_t terrainResolution) : ModelRenderer(device, model)
{
	rayMarchGpuTextureHandle = rayMarchSrvMemory->GetGPUHandle(offset);
	rayMarchCpuTextureHandle = rayMarchSrvMemory->GetCPUHandle(offset);
}

void SkyAtmosphereRenderer::Draw(const std::shared_ptr<GCommandList>& cmdList)
{
	cmdList->SetRootConstantBufferView(StandardShaderSlot::ObjectData,
		*modelDataBuffer, 0);

	// 7th root parameter
	cmdList->GetGraphicsCommandList()->SetGraphicsRootDescriptorTable(7,
		rayMarchGpuTextureHandle);

	cmdList->GetGraphicsCommandList()->IASetVertexBuffers(0, 0, nullptr);
	cmdList->GetGraphicsCommandList()->IASetIndexBuffer(nullptr);
	cmdList->GetGraphicsCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->Draw(3, 1, 0, 0);
}
