#pragma once
#include <d3d12.h>
#include <d3dcommon.h>
#include <string>
#include <wrl/client.h>

using namespace Microsoft::WRL;

enum ShaderType
{
	VertexShader,
	ComputeShader,
	PixelShader,
	DomainShader,
	GeometryShader,
	HullShader
};

class GShader
{
	std::wstring FileName;
	ComPtr<ID3DBlob> shaderBlob = nullptr;
	ShaderType type;
	const D3D_SHADER_MACRO* defines;
	std::string entryPoint;
	std::string target;
public:

	GShader(std::wstring fileName, ShaderType type, const D3D_SHADER_MACRO* defines = nullptr,
	        std::string entryPoint = "Main", std::string target = "ps_5_1");

	void LoadAndCompile();


	ID3DBlob* GetShaderData();

	D3D12_SHADER_BYTECODE GetShaderResource() const;

	ShaderType GetType();
};
