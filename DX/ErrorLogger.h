#pragma once
#include <string>
#include "COMException.h"
#include <Windows.h>

using namespace GameEngine::Utility;

namespace GameEngine
{
	namespace Logger
	{
		class ErrorLogger
		{
		public:
			static void Log(std::string message);
			static void Log(HRESULT hr, std::string message);
			static void Log(HRESULT hr, std::wstring message);
			static void Log(COMException& exception);
		};

	}
}