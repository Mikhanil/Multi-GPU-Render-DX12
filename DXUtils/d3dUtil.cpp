#include "pch.h"
#include "d3dUtil.h"
#include <comdef.h>
#include <fstream>

using Microsoft::WRL::ComPtr;

DxException::DxException(HRESULT hr, const std::wstring& functionName, const std::wstring& filename, int lineNumber, std::wstring message) :
	ErrorCode(hr),
	FunctionName(functionName),
	Filename(filename),
	LineNumber(lineNumber), message(message)
{
}

DxException::DxException(const std::wstring& message): message(message)
{
}

bool d3dUtil::IsKeyDown(int vkeyCode)
{
	return (GetAsyncKeyState(vkeyCode) & 0x8000) != 0;
}


std::wstring DxException::ToString() const
{
	// Get the string description of the error code.
	_com_error err(ErrorCode);
	std::wstring msg = err.ErrorMessage();

	return FunctionName + L" failed in " + Filename + L"; line " + std::to_wstring(LineNumber) + L"; error: " + msg;
}
