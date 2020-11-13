#pragma once

#include "GraphicPSO.h"
#include "GRootSignature.h"
#include "GShader.h"
#include "MemoryAllocator.h"

using namespace PEPEngine;
using namespace Graphics;
using namespace Allocator;

class RenderModeFactory
{
	static custom_unordered_map<std::string, std::shared_ptr<GShader>> shaders;

	custom_unordered_map<RenderMode::Mode, std::shared_ptr<GraphicPSO>> PSO = MemoryAllocator::CreateUnorderedMap<RenderMode::Mode, std::shared_ptr<GraphicPSO>>();
	
public:

	void LoadDefaultPSO(std::shared_ptr<GDevice> device, std::shared_ptr<GRootSignature> rootSignature,
	                    D3D12_INPUT_LAYOUT_DESC defautlInputDesc, DXGI_FORMAT backBufferFormat,
	                    DXGI_FORMAT depthStencilFormat, std::shared_ptr<GRootSignature> ssaoRootSignature, DXGI_FORMAT normalMapFormat, DXGI_FORMAT
	                    ambientMapFormat, std::shared_ptr<GRootSignature> particleRS);

	void LoadDefaultShaders() const;

	static std::shared_ptr<GShader> GetShader(std::string name);
	std::shared_ptr<GraphicPSO> GetPSO(RenderMode::Mode type)
	{
		return PSO[type];
	}
	
};

