#pragma once
#include <memory>
#include <string>
#include <vector>


#include "GBuffer.h"
#include "SquidRoom.h"
#include "Windows.h"

class GCommandList;
class GTexture;
class Model;
class Mesh;

class BinAssetsLoader
{

public:

	static std::shared_ptr<GTexture> LoadTexturesFromBin(const UINT8* binData, const SampleAssets::TextureResource& tex, std::shared_ptr<GCommandList> cmdList);


	static std::shared_ptr<Mesh> LoadModelFromBin(const UINT8* binData, const SampleAssets::DrawParameters& mdl, std::shared_ptr<GBuffer> baseVertexBuffer, std::
	                                              shared_ptr<GBuffer> baseIndexBuffer, std::shared_ptr<GCommandList> cmdList);
};

