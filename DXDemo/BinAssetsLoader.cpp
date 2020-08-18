#include "BinAssetsLoader.h"
#include "GCommandList.h"
#include "GTexture.h"
#include "Mesh.h"


std::shared_ptr<GTexture> BinAssetsLoader::LoadTexturesFromBin(const UINT8* binData, const SampleAssets::TextureResource& tex, std::shared_ptr<GCommandList> cmdList)
{
    CD3DX12_RESOURCE_DESC texDesc(
        D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        0,
        tex.Width,
        tex.Height,
        1,
        static_cast<UINT16>(tex.MipLevels),
        GetSRGBFormat(tex.Format),
        1,
        0,
        D3D12_TEXTURE_LAYOUT_UNKNOWN,
        D3D12_RESOURCE_FLAG_NONE);

    auto texture = std::make_shared<GTexture>(texDesc, tex.FileName);

    cmdList->TransitionBarrier(texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->FlushResourceBarriers();

    D3D12_SUBRESOURCE_DATA textureData = {};
    textureData.pData = binData + tex.Data->Offset;
    textureData.RowPitch = tex.Data->Pitch;
    textureData.SlicePitch = tex.Data->Size;

    cmdList->UpdateSubresource(*texture.get(), &textureData, 1);

    cmdList->TransitionBarrier(texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->FlushResourceBarriers();

    return texture;
}

std::shared_ptr<Mesh> BinAssetsLoader::LoadModelFromBin(const UINT8* binData, const SampleAssets::DrawParameters& mdl, std::shared_ptr<GBuffer> baseVertexBuffer, std::shared_ptr<GBuffer> baseIndexBuffer,
	std::shared_ptr<GCommandList> cmdList)
{
    auto mesh = std::make_shared<Mesh>(mdl.FileName);

    mesh->SetIndexBuffer(baseIndexBuffer);
    mesh->SetVertexBuffer(baseVertexBuffer);
    mesh->baseVertex = mdl.VertexBase;
    mesh->indexCount = mdl.IndexCount;
    mesh->indexStart = mdl.IndexStart;
    mesh->isTest = true;
    	
    

    mesh->SetTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	
    return mesh;
	
}
