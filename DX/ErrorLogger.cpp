#include "ErrorLogger.h"
#include <comdef.h>

namespace GameEngine
{
	namespace Logger
	{

		void ErrorLogger::Log(std::string message)
		{
			const std::string error_message = "Error: " + message;
			MessageBoxA(nullptr, error_message.c_str(), "Error", MB_ICONERROR);
		}

		void ErrorLogger::Log(HRESULT hr, std::string message)
		{
			const _com_error error(hr);
			const std::wstring error_message = L"Error: " + Utility::StringConverter::StringToWide(message) + L"\n" + error.ErrorMessage();
			MessageBoxW(nullptr, error_message.c_str(), L"Error", MB_ICONERROR);
		}

		void ErrorLogger::Log(HRESULT hr, std::wstring message)
		{
			const _com_error error(hr);
			const std::wstring error_message = L"Error: " + message + L"\n" + error.ErrorMessage();
			MessageBoxW(nullptr, error_message.c_str(), L"Error", MB_ICONERROR);
		}

		void ErrorLogger::Log(COMException& exception)
		{
			const std::wstring error_message = exception.what();
			MessageBoxW(nullptr, error_message.c_str(), L"Error", MB_ICONERROR);
		}
	}
}