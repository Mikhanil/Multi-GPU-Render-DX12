#pragma once
#include "d3dUtil.h"

enum ShaderType
{
	VertexShader,
	PixelShader,
	DomainShader,
	GeometryShader,
	HullShader
};

class Shader
{
	std::wstring FileName;
	Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob = nullptr;
	ShaderType type;
	const D3D_SHADER_MACRO* defines;
	std::string entryPoint;
	std::string target;
public:

	Shader(const std::wstring fileName, const ShaderType type, const D3D_SHADER_MACRO* defines = nullptr, const std::string entryPoint = "Main", const std::string target = "ps_5_1")
		: FileName(fileName), type(type), defines(defines), entryPoint(entryPoint), target(target)
	{
	}

	void LoadAndCompile()
	{
		shaderBlob = d3dUtil::CompileShader(FileName, defines, entryPoint, target);
	}


	ID3DBlob* GetShaderData()
	{
		return shaderBlob.Get();
	}

	D3D12_SHADER_BYTECODE GetShaderInfo() const
	{
		D3D12_SHADER_BYTECODE info{ shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };
		return info;
	}

	ShaderType GetType()
	{
		return type;
	}
};

