#include "DXAdapterReader.h"
#include "ErrorLogger.h"

namespace GameEngine
{
	namespace Graphics
	{
		std::vector<AdapterData> DXAdapterReader::adapters;

		std::vector<AdapterData> DXAdapterReader::GetAdapters()
		{
			if (!adapters.empty()) //If already initialized
				return adapters;

			adapters.clear();
			
			Microsoft::WRL::ComPtr<IDXGIFactory6> pFactory;

			// Create a DXGIFactory object.
			HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(pFactory.GetAddressOf()));
			if (FAILED(hr))
			{
				Logger::ErrorLogger::Log(hr, "Failed to create DXGIFactory for enumerating adapters.");
				exit(-1);
			}

			IDXGIAdapter* pAdapter;
			UINT index = 0;
			while (SUCCEEDED(pFactory->EnumAdapters(index, &pAdapter)))
			{
				adapters.emplace_back(pAdapter);
				index += 1;
			}
			return adapters;
		}

		AdapterData::AdapterData(IDXGIAdapter* pAdapter)
		{
			this->pAdapter = pAdapter;
			HRESULT hr = pAdapter->GetDesc(&this->description);
			if (FAILED(hr))
			{
				Logger::ErrorLogger::Log(hr, "Failed to Get Description for IDXGIAdapter.");
			}
		}
		
	}
}
