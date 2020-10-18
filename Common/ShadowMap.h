#pragma once

#include "d3dUtil.h"
#include "GDescriptor.h"
#include "GTexture.h"
using namespace Microsoft::WRL;

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;
using namespace Utils;

class ShadowMap
{
public:
	ShadowMap(std::shared_ptr<GDevice> device, UINT width, UINT height);

	ShadowMap(const ShadowMap& rhs) = delete;
	ShadowMap& operator=(const ShadowMap& rhs) = delete;
	~ShadowMap();

	UINT Width() const;
	UINT Height() const;
	GTexture& GetTexture();

	GDescriptor* GetSrv();
	GDescriptor* GetDsv();

	void OnResize(UINT newWidth, UINT newHeight);

	void PopulatePreRenderCommands(std::shared_ptr<GCommandList>& cmdList);

private:
	void BuildViews();
	void BuildResource();

private:
	std::shared_ptr<GDevice> device;

	D3D12_VIEWPORT viewport;
	D3D12_RECT scissorRect;

	UINT width = 0;
	UINT height = 0;

	GDescriptor srvMemory;	
	GDescriptor dsvMemory;
	GTexture shadowMap;
};