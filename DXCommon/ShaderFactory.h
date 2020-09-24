#pragma once

#include "GraphicPSO.h"
#include "GShader.h"
#include "MemoryAllocator.h"

class GDevice;
class GRootSignature;


class ShaderFactory
{
	static custom_unordered_map<std::string, std::shared_ptr<GShader>> shaders;

	custom_unordered_map<PsoType::Type, std::shared_ptr<GraphicPSO>> PSO = MemoryAllocator::CreateUnorderedMap<PsoType::Type, std::shared_ptr<GraphicPSO>>();
	
public:

	void LoadDefaultPSO(std::shared_ptr<GDevice> device, std::shared_ptr<GRootSignature> rootSignature,
	                    D3D12_INPUT_LAYOUT_DESC defautlInputDesc, DXGI_FORMAT backBufferFormat,
	                    DXGI_FORMAT depthStencilFormat, std::shared_ptr<GRootSignature> ssaoRootSignature, DXGI_FORMAT normalMapFormat, DXGI_FORMAT
	                    ambientMapFormat);

	void LoadDefaultShaders() const;

	static std::shared_ptr<GShader> GetShader(std::string name);
	std::shared_ptr<GraphicPSO> GetPSO(PsoType::Type type)
	{
		return PSO[type];
	}
	
};

