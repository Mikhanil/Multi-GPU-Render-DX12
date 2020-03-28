#include "ErrorLogger.h"
#include <comdef.h>


namespace GameEngine
{
	namespace Logger
	{

		void ErrorLogger::Log(std::string message)
		{
			DBOUT(message.c_str());
		}

		void ErrorLogger::Log(HRESULT hr, std::string message)
		{
			CheckComErrorFull(hr, message.c_str());
		}		
	}
}