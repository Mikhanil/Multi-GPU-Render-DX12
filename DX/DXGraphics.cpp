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
			HRESULT result;
			
			// Reset (re-use) the memory associated command allocator.
			result = commandAllocator->Reset();
			if (FAILED(result))
			{
				return;
			}

			// Reset the command list, use empty pipeline state for now since there are no shaders and we are just clearing the screen.
			result = commandList->Reset(commandAllocator.Get(), pipelineState);
			if (FAILED(result))
			{
				return;
			}
			
			D3D12_RESOURCE_BARRIER barrier;
			ZeroMemory(&barrier, sizeof(barrier));
			// Record commands in the command list now.
			// Start by setting the resource barrier.
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			barrier.Transition.pResource = backBufferRenderTarget[bufferIndex].Get();
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			commandList->ResourceBarrier(1, &barrier);


			// Get the render target view handle for the current back buffer.
			auto renderTargetViewHandle = renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
			auto renderTargetViewDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			if (bufferIndex == 1)
			{
				renderTargetViewHandle.ptr += renderTargetViewDescriptorSize;
			}

			// Set the back buffer as the render target.
			commandList->OMSetRenderTargets(1, &renderTargetViewHandle, FALSE, nullptr);
			
			// Then set the color to clear the window to.
			commandList->ClearRenderTargetView(renderTargetViewHandle, color, 0, nullptr);

			// Indicate that the back buffer will now be used to present.
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			commandList->ResourceBarrier(1, &barrier);

			// Close the list of commands.
			result = commandList->Close();
			if (FAILED(result))
			{
				return;
			}

			// Load the command list array (only one command list for now).
			std::vector<ID3D12CommandList*> pCommandList = { commandList.Get() };

			// Execute the list of commands.
			commandQueue->ExecuteCommandLists(pCommandList.size(), pCommandList.data());

			swapChain->Present(1, 0);

			// Signal and increment the fence value.
			const auto fenceToWaitFor = fenceValue;
			result = commandQueue->Signal(fence.Get(), fenceToWaitFor);
			if (FAILED(result))
			{
				return;
			}
			fenceValue++;

			// Wait until the GPU is done rendering.
			if (fence->GetCompletedValue() < fenceToWaitFor)
			{
				result = fence->SetEventOnCompletion(fenceToWaitFor, fenceEvent);
				if (FAILED(result))
				{
					return;
				}
				WaitForSingleObject(fenceEvent, INFINITE);
			}

			// Alternate the back buffer index back and forth between 0 and 1 each frame.
			bufferIndex == 0 ? bufferIndex = 1 : bufferIndex = 0;
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

				D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};
				
				// Initialize the description of the command queue.
				ZeroMemory(&commandQueueDesc, sizeof(commandQueueDesc));
				
				// Set up the description of the command queue.
				commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
				commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
				commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				commandQueueDesc.NodeMask = 0;
				
				hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));
				CheckComError(hr, "Failed to create command queue.");

				ComPtr<IDXGIOutput> adapterOutput;
				// Enumerate the primary adapter output (monitor).
				hr = adapters[0].pAdapter->EnumOutputs(0, &adapterOutput);
				CheckComError(hr, "Failed get adapter output");

				unsigned int numModes;
				// Get the number of modes that fit the DXGI_FORMAT_R8G8B8A8_UNORM display format for the adapter output (monitor).
				hr = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, nullptr);
				CheckComError(hr, "Failed to get number of modes");
				
				// Create a list to hold all the possible display modes for this monitor/video card combination.

				std::vector<DXGI_MODE_DESC> displayModeList(numModes);				
				if (displayModeList.empty())
				{
					return false;
				}

				// Now fill the display mode list structures.
				hr = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, displayModeList.data());
				CheckComError(hr, "Failed fill the display mode list structures");


				// Now go through all the display modes and find the one that matches the screen height and width.
				// When a match is found store the numerator and denominator of the refresh rate for that monitor.
				unsigned int numerator;
				unsigned int denominator;
				for (unsigned int i = 0; i < numModes; i++)
				{
					if (displayModeList[i].Height == this->windowHeight)
					{
						if (displayModeList[i].Width == this->windowWidth)
						{
							numerator = displayModeList[i].RefreshRate.Numerator;
							denominator = displayModeList[i].RefreshRate.Denominator;
						}
					}
				}
				
				// Get the adapter (video card) description.
				DXGI_ADAPTER_DESC adapterDesc;
				ZeroMemory(&adapterDesc, sizeof(adapterDesc));				
				hr = adapters[0].pAdapter->GetDesc(&adapterDesc);
				CheckComError(hr, "Failed get the adapter (video card) description");
				
				// Store the dedicated video card memory in megabytes.
				videoCardMemory = (int)(adapterDesc.DedicatedVideoMemory / 1024 / 1024);
				// Convert the name of the video card to a character array and store it.
				unsigned long long stringLength;
				auto error = wcstombs_s(&stringLength, videoCardDescription, 128, adapterDesc.Description, 128);
				if (error != 0)
				{
					return false;
				}

				// Release the display mode list.
				displayModeList.clear();

				// Release the adapter output.
				adapterOutput->Release();

				

				// Initialize the swap chain description.
				DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
				ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));

				// Set the swap chain to use double buffering.
				swapChainDesc.BufferCount = 2;
				
				// Set the height and width of the back buffers in the swap chain.
				swapChainDesc.BufferDesc.Height = windowHeight;
				swapChainDesc.BufferDesc.Width = windowWidth;
				// Set a regular 32-bit surface for the back buffers.
				swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				// Set the usage of the back buffers to be render target outputs.
				swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				// Set the swap effect to discard the previous buffer contents after swapping.
				swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				// Set the handle for the window to render to.
				swapChainDesc.OutputWindow = hwnd;
				swapChainDesc.Windowed = true;
				
				//TODO сделать тут красивое условие с учетом частоты обновления монитора
				swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
				swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;

				// Turn multisampling off.
				swapChainDesc.SampleDesc.Count = 1;
				swapChainDesc.SampleDesc.Quality = 0;
				// Set the scan line ordering and scaling to unspecified.
				swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
				swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
				// Don't set the advanced flags.
				swapChainDesc.Flags = 0;

				// Finally create the swap chain using the swap chain description.
				ComPtr<IDXGISwapChain> swapChain0;
				ComPtr<IDXGIFactory4> factory;
				hr = CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory);
				CheckComError(hr, "Failed to create swapchain factory.");
				hr = factory->CreateSwapChain(commandQueue.Get(), &swapChainDesc, &swapChain0);
				CheckComError(hr, "Failed to create Swapchain.");

				// Next upgrade the IDXGISwapChain to a IDXGISwapChain3 interface and store it in a private member variable named m_swapChain.
				// This will allow us to use the newer functionality such as getting the current back buffer index.
				hr = swapChain0->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&swapChain);
				CheckComError(hr, "Failed to create Swapchain.");

				swapChain0->Release();
				factory->Release();


				// Initialize the render target view heap description for the two back buffers.
				D3D12_DESCRIPTOR_HEAP_DESC renderTargetViewHeapDesc = {};
				ZeroMemory(&renderTargetViewHeapDesc, sizeof(renderTargetViewHeapDesc));
				renderTargetViewHeapDesc.NumDescriptors = 2;
				renderTargetViewHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
				renderTargetViewHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

				hr = device->CreateDescriptorHeap(&renderTargetViewHeapDesc, IID_PPV_ARGS(&renderTargetViewHeap));
				CheckComError(hr, "Failed to create Description heap.");

				// Get a handle to the starting memory location in the render target view heap to identify where the render target views will be located for the two back buffers.
				auto renderTargetViewHandle = renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();

				// Get the size of the memory location for the render target view descriptors.
				auto renderTargetViewDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);


				// Get a pointer to the first back buffer from the swap chain.
				hr = swapChain->GetBuffer(0, __uuidof(ID3D12Resource), (void**)&backBufferRenderTarget[0]);
				CheckComError(hr, "Failed get a pointer to the first back buffer from the swap chain.");

				// Create a render target view for the first back buffer.
				device->CreateRenderTargetView(backBufferRenderTarget[0].Get(), nullptr, renderTargetViewHandle);

				// Increment the view handle to the next descriptor location in the render target view heap.
				renderTargetViewHandle.ptr += renderTargetViewDescriptorSize;

				// Get a pointer to the second back buffer from the swap chain.
				hr = swapChain->GetBuffer(1, __uuidof(ID3D12Resource), (void**)&backBufferRenderTarget[1]);
				CheckComError(hr, "Failed get a pointer to the second back buffer from the swap chain.");

				// Create a render target view for the second back buffer.
				device->CreateRenderTargetView(backBufferRenderTarget[1].Get(), nullptr, renderTargetViewHandle);

				// Finally get the initial index to which buffer is the current back buffer.
				bufferIndex = swapChain->GetCurrentBackBufferIndex();
				
				// Create a command allocator.
				hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
				CheckComError(hr, "Failed to create Command allocator.");

				// Create a basic command list.
				hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), pipelineState, IID_PPV_ARGS(&commandList));
				CheckComError(hr, "Failed to create Command list.");

				// Initially we need to close the command list during initialization as it is created in a recording state.
				hr = commandList->Close();
				CheckComError(hr, "Failed close the command list during initialization as it is created in a recording state.");

				// Create a fence for GPU synchronization.
				hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fence);
				CheckComError(hr, "Failed create a fence for GPU synchronization.");

				// Create an event object for the fence.
				fenceEvent = CreateEventEx(nullptr, FALSE, FALSE, EVENT_ALL_ACCESS);
				if (fenceEvent == nullptr)
				{
					return false;
				}

				// Initialize the starting fence value. 
				fenceValue = 1;
				

				//?
				D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
				hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipelineState));
				//CheckComError(hr, "Failed to create Graphic pipline.");
				
				

				
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
