#include "DXGraphics.h"
#include "COMException.h"
#include "ErrorLogger.h"
#include "DXAdapterReader.h"

namespace GameEngine
{
	namespace Graphics
	{

		using namespace Utility;
		using namespace Logger;
		
		bool DXGraphics::Initialize(HWND hwnd, int width, int height)
		{
			this->windowWidth = width;
			this->windowHeight = height;
			
			if (!InitializeDirectX(hwnd))
				return false;

			if (!InitializeShaders())
				return false;

			if (!InitializeScene())
				return false;

			return true;
		}

		void DXGraphics::RenderFrame()
		{
			const float color[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

			commandList->ClearRenderTargetView(descHeap->GetCPUDescriptorHandleForHeapStart(),
				color, 0, nullptr);

			commandList->Close();

			std::vector<ID3D12CommandList*> pCommandList = { commandList.Get()};

			commandQueue->ExecuteCommandLists(pCommandList.size(), pCommandList.data());

			swapChain->Present(1, 0);

			pCommandList.clear();
		}

		bool DXGraphics::InitializeDirectX(HWND hwnd)
		{
			try
			{
				const auto adapters = DXAdapterReader::GetAdapters();

				if(adapters.empty())
				{
					ErrorLogger::Log("No IDXGI Adapters found.");
					return false;
				}
				
				HRESULT hr;

				std::vector<D3D_FEATURE_LEVEL> feature_levels = {
					D3D_FEATURE_LEVEL_12_1,
					D3D_FEATURE_LEVEL_12_0,
					D3D_FEATURE_LEVEL_11_1,
					D3D_FEATURE_LEVEL_11_0
				};

				for (auto adapter : adapters)
				{
					for (auto feature_level : feature_levels)
					{
						hr = D3D12CreateDevice(adapter.pAdapter, feature_level, IID_PPV_ARGS(&device));
						if(SUCCEEDED(hr))
						{
							break;
						}
					}

					if (SUCCEEDED(hr))
					{
						break;
					}
				}

				CheckComError(hr, "Failed to create device");

				
				D3D12_COMMAND_QUEUE_DESC queue_desc = {};
				hr = device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&commandQueue));
				CheckComError(hr, "Failed to create command queue.");

				

				DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
				swapChainDesc.BufferCount = 2;
				swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				swapChainDesc.BufferDesc.Height = windowHeight;
				swapChainDesc.BufferDesc.Width = windowWidth;
				swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
				swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
				swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				swapChainDesc.OutputWindow = hwnd;
				swapChainDesc.SampleDesc.Count = 1;
				swapChainDesc.Windowed = true;

				hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
				CheckComError(hr, "Failed to create DXGI factory.");

				ComPtr<IDXGISwapChain> swapChain0;
				hr = factory->CreateSwapChain(commandQueue.Get(), &swapChainDesc, &swapChain0);
				CheckComError(hr, "Failed to create Swapchain.");
				CheckComError(swapChain0.As(&swapChain), "Failed to create Swapchain.");


				
				D3D12_DESCRIPTOR_HEAP_DESC hDes = {};
				hDes.NumDescriptors = 2;
				hDes.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

				hr = device->CreateDescriptorHeap(&hDes, IID_PPV_ARGS(&descHeap));
				CheckComError(hr, "Failed to create Description heap.");

				ComPtr<ID3D12Resource> resources;
				swapChain->GetBuffer(0, IID_PPV_ARGS(&resources));

				device->CreateRenderTargetView(resources.Get(), nullptr, descHeap->GetCPUDescriptorHandleForHeapStart());
			
				
				resources.ReleaseAndGetAddressOf();

				hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
				CheckComError(hr, "Failed to create Command allocator.");

				//TODO Спросить у Павла, почему данная строч
				/**/
				D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
				hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipelineState));
				//CheckComError(hr, "Failed to create Graphic pipline.");
				
				hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), pipelineState.Get(), IID_PPV_ARGS(&commandList));
				CheckComError(hr, "Failed to create Command list.");

				
			}
			catch (COMException & ex)
			{
				ErrorLogger::Log(ex);
				return false;
			}
		}

		bool DXGraphics::InitializeShaders()
		{
			return true;
		}

		bool DXGraphics::InitializeScene()
		{

			auto camTr = camera.GetTransform();
			camTr->IsUseOnlyParentPosition = true;

			camera.SetProjectionValues(90.0f, static_cast<float>(windowWidth) / static_cast<float>(windowHeight), 0.1f, 1000.0f);
			camTr->SetPosition(0.0f, 12.5, 7.5f);

			
			return true;
		}
	}
}
