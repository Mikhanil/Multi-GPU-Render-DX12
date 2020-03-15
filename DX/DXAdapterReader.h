#pragma once

#include <vector>
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>

namespace GameEngine
{
	namespace Graphics
	{
		class AdapterData
		{
		public:
			AdapterData(IDXGIAdapter* pAdapter);
			IDXGIAdapter* pAdapter = nullptr;
			DXGI_ADAPTER_DESC description{};
		};

		class DXAdapterReader
		{
		public:
			static std::vector<AdapterData> GetAdapters();
		private:
			static std::vector<AdapterData> adapters;
		};
	}
}



