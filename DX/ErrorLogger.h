#pragma once
#include <string>
#include <Windows.h>

#include <iostream>
#include <sstream>

#define DBOUT( s )            \
{                             \
   std::wostringstream os_;    \
   os_ << s;                   \
   OutputDebugStringW( os_.str().c_str() );  \
}

#define CheckComError( hr ) if( FAILED( hr ) ) DBOUT( hr << __FILE__<< __FUNCTION__<< __LINE__ )
#define CheckComErrorFull( hr, msg ) if( FAILED( hr ) ) DBOUT( hr << msg << __FILE__ << __FUNCTION__ <<  __LINE__ )


namespace GameEngine
{
	namespace Logger
	{
		class ErrorLogger
		{
		public:
			static void Log(std::string message);
			static void Log(HRESULT hr, std::string message);
		};

	}
}