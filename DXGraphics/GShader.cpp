#include "GShader.h"
#include "D3DShaderLoad.h"

GShader::GShader(const std::wstring fileName, const ShaderType type, const D3D_SHADER_MACRO* defines,
                 const std::string entryPoint, const std::string target): FileName(fileName), type(type),
                                                                          defines(defines), entryPoint(entryPoint),
                                                                          target(target)
{
}

void GShader::LoadAndCompile()
{
	shaderBlob = CompileShader(FileName, defines, entryPoint, target);
}

ID3DBlob* GShader::GetShaderData()
{
	return shaderBlob.Get();
}

D3D12_SHADER_BYTECODE GShader::GetShaderResource() const
{
	D3D12_SHADER_BYTECODE info{shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize()};
	return info;
}

ShaderType GShader::GetType()
{
	return type;
}
