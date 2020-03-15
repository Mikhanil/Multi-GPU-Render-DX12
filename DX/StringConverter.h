#pragma once
#include <string>

namespace GameEngine
{
	namespace Utility
	{


		class StringConverter
		{
		public:
			static std::wstring StringToWide(std::string str);
		};

	}
}