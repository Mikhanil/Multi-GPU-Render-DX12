#include "DXGraphics.h"
#include "COMException.h"
#include "ErrorLogger.h"
#include "DXAdapterReader.h"
#include "CommandQueue.h"
#include <d3dcompiler.h>

#include <algorithm> // For std::min and std::max.
#if defined(min)
#undef min
#endif

#if defined(max)
#undef max
#endif

namespace GameEngine
{
	namespace Graphics
	{
		using namespace Utility;
		using namespace Logger;

		// Clamp a value between a min and max range.
		template <typename T>
		constexpr const T& clamp(const T& val, const T& min, const T& max)
		{
			return val < min ? min : val > max ? max : val;
		}


		bool DXGraphics::Initialize(HWND hwnd, int width, int height, bool vSync)
		{
			// Check for DirectX Math library support.
			if (!DirectX::XMVerifyCPUSupport())
			{
				MessageBoxA(hwnd, "Failed to verify DirectX Math library support.", "Error", MB_OK | MB_ICONERROR);
				return false;
			}

			m_ScissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
			m_Viewport = (CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)));

			this->isVsync = vSync;
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
			float color[4] = {0.0f, 0.0f, 1.0f, 1.0f};
			HRESULT result;

			auto commandQueue = GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
			auto commandList = commandQueue->GetCommandList();

			UINT currentBackBufferIndex = GetCurrentBackBufferIndex();
			auto backBuffer = this->GetCurrentBackBuffer();
			auto rtv = GetCurrentRenderTargetView();
			auto dsv = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();

			TransitionResource(commandList, backBuffer,
				D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);


			ClearRTV(commandList, rtv, color);
			ClearDepth(commandList, dsv);

			commandList->SetPipelineState(m_PipelineState.Get());
			commandList->SetGraphicsRootSignature(m_RootSignature.Get());

			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
			commandList->IASetIndexBuffer(&indexBufferView);

			commandList->RSSetViewports(1, &m_Viewport);
			commandList->RSSetScissorRects(1, &m_ScissorRect);

			commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

			auto model = models[0];
			auto modelTranform = model->GetTransform();

			Matrix mvp = modelTranform->GetWorldMatrix() * camera.GetViewMatrix() * camera.GetProjectionMatrix();

			
			commandList->SetGraphicsRoot32BitConstants(0, sizeof(Matrix) / 4, &mvp, 0);

			commandList->DrawIndexedInstanced((g_Indicies.size()), 1, 0, 0, 0);

			// Present
			{
				TransitionResource(commandList, backBuffer,
					D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

				fenceValues[currentBackBufferIndex] = commandQueue->ExecuteCommandList(commandList);

				currentBackBufferIndex = Present();

				commandQueue->WaitForFenceValue(fenceValues[currentBackBufferIndex]);
			}
			
			//// Reset (re-use) the memory associated command allocator.
			//result = commandAllocator->Reset();
			//if (FAILED(result))
			//{
			//	return;
			//}

			//// Reset the command list, use empty pipeline state for now since there are no shaders and we are just clearing the screen.
			//result = commandList->Reset(commandAllocator.Get(), pipelineState);
			//if (FAILED(result))
			//{
			//	return;
			//}

			//D3D12_RESOURCE_BARRIER barrier;
			//ZeroMemory(&barrier, sizeof(barrier));
			//// Record commands in the command list now.
			//// Start by setting the resource barrier.
			//barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
			//barrier.Transition.pResource = backBufferRenderTarget[bufferIndex].Get();
			//barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
			//barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
			//barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
			//barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
			//commandList->ResourceBarrier(1, &barrier);


			//// Get the render target view handle for the current back buffer.
			//auto renderTargetViewHandle = renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart();
			//auto renderTargetViewDescriptorSize = device->GetDescriptorHandleIncrementSize(
			//	D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			//if (bufferIndex == 1)
			//{
			//	renderTargetViewHandle.ptr += renderTargetViewDescriptorSize;
			//}

			//// Set the back buffer as the render target.
			//commandList->OMSetRenderTargets(1, &renderTargetViewHandle, FALSE, nullptr);

			//// Then set the color to clear the window to.
			//commandList->ClearRenderTargetView(renderTargetViewHandle, color, 0, nullptr);

			//// Indicate that the back buffer will now be used to present.
			//barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			//barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
			//commandList->ResourceBarrier(1, &barrier);

			//// Close the list of commands.
			//result = commandList->Close();
			//if (FAILED(result))
			//{
			//	return;
			//}

			//// Load the command list array (only one command list for now).
			//std::vector<ID3D12CommandList*> pCommandList = {commandList.Get()};

			//// Execute the list of commands.
			//d12commandQueue->ExecuteCommandLists(pCommandList.size(), pCommandList.data());

			//swapChain->Present(1, 0);

			//// Signal and increment the fence value.
			//const auto fenceToWaitFor = fenceValue;
			//result = d12commandQueue->Signal(fence.Get(), fenceToWaitFor);
			//if (FAILED(result))
			//{
			//	return;
			//}
			//fenceValue++;

			//// Wait until the GPU is done rendering.
			//if (fence->GetCompletedValue() < fenceToWaitFor)
			//{
			//	result = fence->SetEventOnCompletion(fenceToWaitFor, fenceEvent);
			//	if (FAILED(result))
			//	{
			//		return;
			//	}
			//	WaitForSingleObject(fenceEvent, INFINITE);
			//}

			//// Alternate the back buffer index back and forth between 0 and 1 each frame.
			//bufferIndex == 0 ? bufferIndex = 1 : bufferIndex = 0;
		}

		std::shared_ptr<CommandQueue> DXGraphics::GetCommandQueue(D3D12_COMMAND_LIST_TYPE type) const
		{
			std::shared_ptr<CommandQueue> commandQueue;
			switch (type)
			{
			case D3D12_COMMAND_LIST_TYPE_DIRECT:
				commandQueue = m_DirectCommandQueue;
				break;
			case D3D12_COMMAND_LIST_TYPE_COMPUTE:
				commandQueue = m_ComputeCommandQueue;
				break;
			case D3D12_COMMAND_LIST_TYPE_COPY:
				commandQueue = m_CopyCommandQueue;
				break;
			default:
				CheckComError(ERROR, "Invalid command queue type.");
			}

			return commandQueue;
		}

		bool DXGraphics::InitializeDirectX(HWND hwnd)
		{
			try
			{
				const auto adapters = DXAdapterReader::GetAdapters();

				if (adapters.empty())
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
						if (SUCCEEDED(hr))
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


				m_DirectCommandQueue = std::make_shared<CommandQueue>();
				m_DirectCommandQueue->Initialize(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
				m_ComputeCommandQueue = std::make_shared<CommandQueue>();
				m_ComputeCommandQueue->Initialize(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
				m_CopyCommandQueue = std::make_shared<CommandQueue>();
				m_CopyCommandQueue->Initialize(device, D3D12_COMMAND_LIST_TYPE_COPY);


				D3D12_COMMAND_QUEUE_DESC commandQueueDesc = {};

				// Initialize the description of the command queue.
				ZeroMemory(&commandQueueDesc, sizeof(commandQueueDesc));

				// Set up the description of the command queue.
				commandQueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
				commandQueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
				commandQueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				commandQueueDesc.NodeMask = 0;

				hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&d12commandQueue));
				CheckComError(hr, "Failed to create command queue.");

				IDXGIOutput* adapterOutput;
				// Enumerate the primary adapter output (monitor).
				hr = adapters[0].pAdapter->EnumOutputs(0, &adapterOutput);
				CheckComError(hr, "Failed get adapter output");

				unsigned int numModes;
				// Get the number of modes that fit the DXGI_FORMAT_R8G8B8A8_UNORM display format for the adapter output (monitor).
				hr = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED,
				                                       &numModes, nullptr);
				CheckComError(hr, "Failed to get number of modes");

				// Create a list to hold all the possible display modes for this monitor/video card combination.

				std::vector<DXGI_MODE_DESC> displayModeList(numModes);
				if (displayModeList.empty())
				{
					return false;
				}

				// Now fill the display mode list structures.
				hr = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED,
				                                       &numModes, displayModeList.data());
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
				IDXGISwapChain* swapChain0;
				IDXGIFactory4* factory;
				hr = CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory);
				CheckComError(hr, "Failed to create swapchain factory.");
				hr = factory->CreateSwapChain(d12commandQueue.Get(), &swapChainDesc, &swapChain0);
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
				auto renderTargetViewDescriptorSize = device->GetDescriptorHandleIncrementSize(
					D3D12_DESCRIPTOR_HEAP_TYPE_RTV);


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
				hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), m_PipelineState.Get(),
				                               IID_PPV_ARGS(&commandList));
				CheckComError(hr, "Failed to create Command list.");

				// Initially we need to close the command list during initialization as it is created in a recording state.
				hr = commandList->Close();
				CheckComError(
					hr, "Failed close the command list during initialization as it is created in a recording state.");

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
				/*D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
				hr = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_PipelineState));*/
				//CheckComError(hr, "Failed to create Graphic pipline.");
			}
			catch (COMException& ex)
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
			auto commandQueue = GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
			auto commandList = commandQueue->GetCommandList();


			auto model = new Model();

			if (!model->Initialize(g_Vertices, g_Indicies))
			{
				CheckComError(ERROR, "Can't init model");
			}
			else
			{
				models.push_back(model);
			}

			// Upload vertex buffer data.
			ComPtr<ID3D12Resource> intermediateVertexBuffer;
			UpdateBufferResource(commandList.Get(),
			                     &vertexBuffer, &intermediateVertexBuffer,
			                     g_Vertices.size(), sizeof(VertexPosColor), g_Vertices.data());

			// Create the vertex buffer view.
			vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
			vertexBufferView.SizeInBytes = g_Vertices.size();
			vertexBufferView.StrideInBytes = sizeof(VertexPosColor);


			// Upload index buffer data.
			ComPtr<ID3D12Resource> intermediateIndexBuffer;
			UpdateBufferResource(commandList,
			                     &indexBuffer, &intermediateIndexBuffer,
			                     g_Indicies.size(), sizeof(WORD), g_Indicies.data());

			// Create index buffer view.
			indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
			indexBufferView.Format = DXGI_FORMAT_R16_UINT;
			indexBufferView.SizeInBytes = (g_Indicies.size());

			// Create the descriptor heap for the depth-stencil view.
			D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
			dsvHeapDesc.NumDescriptors = 1;
			dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			CheckComError(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DSVHeap)),
			              "Can't create desctiption");

			// Load the vertex shader.
			ComPtr<ID3DBlob> vertexShaderBlob;
			CheckComError(D3DReadFileToBlob(L"VertexShader.hlsl", &vertexShaderBlob), "Failed to load vertex shader");

			// Load the pixel shader.
			ComPtr<ID3DBlob> pixelShaderBlob;
			CheckComError(D3DReadFileToBlob(L"PixelShader.hlsl", &pixelShaderBlob), "Failed to load pixel shader");

			// Create the vertex input layout
			D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
				{
					"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
					D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
				},
				{
					"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
					D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
				},
			};

			// Create a root signature.
			D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
			if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
			{
				featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
			}

			// Allow input layout and deny unnecessary access to certain pipeline stages.
			D3D12_ROOT_SIGNATURE_FLAGS rootSignatureFlags =
				D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
				D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

			// A single 32-bit constant root parameter that is used by the vertex shader.
			CD3DX12_ROOT_PARAMETER1 rootParameters[1];
			rootParameters[0].InitAsConstants(sizeof(Matrix) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);

			CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDescription;
			rootSignatureDescription.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, rootSignatureFlags);

			// Serialize the root signature.
			ComPtr<ID3DBlob> rootSignatureBlob;
			ComPtr<ID3DBlob> errorBlob;
			CheckComError(D3DX12SerializeVersionedRootSignature(&rootSignatureDescription,
				              featureData.HighestVersion, &rootSignatureBlob, &errorBlob),
			              "Failed serialize root signature");
			// Create the root signature.
			CheckComError(device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
				              rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature)),
			              "Failed create root signature");

			struct PipelineStateStream
			{
				CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE pRootSignature;
				CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT InputLayout;
				CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY PrimitiveTopologyType;
				CD3DX12_PIPELINE_STATE_STREAM_VS VS;
				CD3DX12_PIPELINE_STATE_STREAM_PS PS;
				CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT DSVFormat;
				CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS RTVFormats;
			} pipelineStateStream;

			D3D12_RT_FORMAT_ARRAY rtvFormats = {};
			rtvFormats.NumRenderTargets = 1;
			rtvFormats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

			pipelineStateStream.pRootSignature = m_RootSignature.Get();
			pipelineStateStream.InputLayout = {inputLayout, _countof(inputLayout)};
			pipelineStateStream.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
			pipelineStateStream.VS = CD3DX12_SHADER_BYTECODE(vertexShaderBlob.Get());
			pipelineStateStream.PS = CD3DX12_SHADER_BYTECODE(pixelShaderBlob.Get());
			pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			pipelineStateStream.RTVFormats = rtvFormats;

			D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
				sizeof(PipelineStateStream), &pipelineStateStream
			};

			device->CreatePipelineState(&pipelineStateStreamDesc, __uuidof(ID3D12PipelineState), (void**)&m_PipelineState);
			/*CheckComError(device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_PipelineState)),
			              "Failed create Pipeline state");*/

			auto fenceValue = commandQueue->ExecuteCommandList(commandList);
			commandQueue->WaitForFenceValue(fenceValue);


			// Resize/Create the depth buffer.
			ResizeDepthBuffer(windowWidth, windowHeight);


			auto camTr = camera.GetTransform();
			camTr->IsUseOnlyParentPosition = true;

			camera.SetProjectionValues(90.0f, static_cast<float>(windowWidth) / static_cast<float>(windowHeight), 0.1f,
			                           1000.0f);
			camTr->SetPosition(0.0f, 12.5, 7.5f);


			return true;
		}

		void DXGraphics::TransitionResource(ComPtr<ID3D12GraphicsCommandList2> commandList,
		                                    ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES beforeState,
		                                    D3D12_RESOURCE_STATES afterState)
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				resource.Get(),
				beforeState, afterState);

			commandList->ResourceBarrier(1, &barrier);
		}

		void DXGraphics::ClearRTV(ComPtr<ID3D12GraphicsCommandList2> commandList, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
		                          float* clearColor)
		{
			commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
		}

		void DXGraphics::ClearDepth(ComPtr<ID3D12GraphicsCommandList2> commandList, D3D12_CPU_DESCRIPTOR_HANDLE dsv,
		                            FLOAT depth)
		{
			commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
		}

		void DXGraphics::UpdateBufferResource(ComPtr<ID3D12GraphicsCommandList2> commandList,
		                                      ID3D12Resource** pDestinationResource,
		                                      ID3D12Resource** pIntermediateResource, size_t numElements,
		                                      size_t elementSize, const void* bufferData, D3D12_RESOURCE_FLAGS flags)
		{
			size_t bufferSize = numElements * elementSize;

			// Create a committed resource for the GPU resource in a default heap.
			CheckComError(device->CreateCommittedResource(
				              &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				              D3D12_HEAP_FLAG_NONE,
				              &CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags),
				              D3D12_RESOURCE_STATE_COPY_DEST,
				              nullptr,
				              IID_PPV_ARGS(pDestinationResource)), "Failder Create a commited resources");

			// Create an committed resource for the upload.
			if (bufferData)
			{
				CheckComError(device->CreateCommittedResource(
					              &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
					              D3D12_HEAP_FLAG_NONE,
					              &CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
					              D3D12_RESOURCE_STATE_GENERIC_READ,
					              nullptr,
					              IID_PPV_ARGS(pIntermediateResource)), "Failed create resource for the upload");
				D3D12_SUBRESOURCE_DATA subresourceData = {};
				subresourceData.pData = bufferData;
				subresourceData.RowPitch = bufferSize;
				subresourceData.SlicePitch = subresourceData.RowPitch;

				UpdateSubresources(commandList.Get(),
				                   *pDestinationResource, *pIntermediateResource,
				                   0, 0, 1, &subresourceData);
			}
		}

		void DXGraphics::ResizeDepthBuffer(int width, int height)
		{
			Flush();

			width = std::max(1, width);
			height = std::max(1, height);


			// Resize screen dependent resources.
			// Create a depth buffer.
			D3D12_CLEAR_VALUE optimizedClearValue = {};
			optimizedClearValue.Format = DXGI_FORMAT_D32_FLOAT;
			optimizedClearValue.DepthStencil = {1.0f, 0};

			CheckComError(device->CreateCommittedResource(
				              &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				              D3D12_HEAP_FLAG_NONE,
				              &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, width, height,
					              1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
				              D3D12_RESOURCE_STATE_DEPTH_WRITE,
				              &optimizedClearValue,
				              IID_PPV_ARGS(&m_DepthBuffer)
			              ), "Can't create commit");

			// Update the depth-stencil view.
			D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
			dsv.Format = DXGI_FORMAT_D32_FLOAT;
			dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			dsv.Texture2D.MipSlice = 0;
			dsv.Flags = D3D12_DSV_FLAG_NONE;

			device->CreateDepthStencilView(m_DepthBuffer.Get(), &dsv,
			                               m_DSVHeap->GetCPUDescriptorHandleForHeapStart());
		}

		void DXGraphics::Flush()
		{
			m_DirectCommandQueue->Flush();
			m_ComputeCommandQueue->Flush();
			m_CopyCommandQueue->Flush();
		}

		void DXGraphics::UpdateRenderTargetViews()
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart());

			for (int i = 0; i < BufferCount; ++i)
			{
				ComPtr<ID3D12Resource> backBuffer;
				CheckComError(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)), "Failed get bacxk Buffer");

				device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

				backBufferRenderTarget[i] = backBuffer;

				rtvHandle.Offset(GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
			}
		}

		UINT DXGraphics::GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type)
		{
			return device->GetDescriptorHandleIncrementSize(type);
		}

		D3D12_CPU_DESCRIPTOR_HANDLE DXGraphics::GetCurrentRenderTargetView()
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart(),
			                                     bufferIndex,
			                                     GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
		}

		UINT DXGraphics::GetCurrentBackBufferIndex() const
		{
			return m_CurrentBackBufferIndex;
		}

		UINT DXGraphics::Present()
		{
			UINT syncInterval = isVsync ? 1 : 0;
			CheckComError(swapChain->Present(syncInterval, 0), "Failed swap chain present");
			m_CurrentBackBufferIndex = swapChain->GetCurrentBackBufferIndex();

			return m_CurrentBackBufferIndex;
		}
	}
}
