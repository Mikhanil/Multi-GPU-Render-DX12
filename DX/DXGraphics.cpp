#include "DXGraphics.h"
#include "ErrorLogger.h"
#include "DXAdapterReader.h"
#include "CommandQueue.h"
#include <d3dcompiler.h>

#include <algorithm> 
#include "d3dx12.h"

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
		using namespace Logger;
		using namespace Graphics;


		static std::vector<VertexPosColor> g_Vertices{
			{Vector3(-1.0f, -1.0f, -1.0f), Vector4(0.0f, 0.0f, 0.0f, 0.0f)}, // 0
			{Vector3(-1.0f, 1.0f, -1.0f), Vector4(0.0f, 1.0f, 0.0f, 0.0f)}, // 1
			{Vector3(1.0f, 1.0f, -1.0f), Vector4(1.0f, 1.0f, 0.0f, 0.0f)}, // 2
			{Vector3(1.0f, -1.0f, -1.0f), Vector4(1.0f, 0.0f, 0.0f, 0.0f)}, // 3
			{Vector3(-1.0f, -1.0f, 1.0f), Vector4(0.0f, 0.0f, 1.0f, 0.0f)}, // 4
			{Vector3(-1.0f, 1.0f, 1.0f), Vector4(0.0f, 1.0f, 1.0f, 0.0f)}, // 5
			{Vector3(1.0f, 1.0f, 1.0f), Vector4(1.0f, 1.0f, 1.0f, 0.0f)}, // 6
			{Vector3(1.0f, -1.0f, 1.0f), Vector4(1.0f, 0.0f, 1.0f, 0.0f)} // 7
		};

		static std::vector<WORD> g_Indicies
		{
			0, 1, 2, 0, 2, 3,
			4, 6, 5, 4, 7, 6,
			4, 5, 1, 4, 1, 0,
			3, 2, 6, 3, 6, 7,
			1, 5, 6, 1, 6, 2,
			4, 0, 3, 4, 3, 7
		};

		// Clamp a value between a min and max range.
		template <typename T>
		constexpr const T& clamp(const T& val, const T& min, const T& max)
		{
			return val < min ? min : val > max ? max : val;
		}


		bool DXGraphics::Initialize(HWND hwnd, int width, int height, bool vSync)
		{
			this->isVsync = vSync;
			this->windowWidth = width;
			this->windowHeight = height;
			m_hwnd = hwnd;

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
			auto commandQueue = GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
			auto commandList = commandQueue->GetCommandList();

			UINT currentBackBufferIndex = GetCurrentBackBufferIndex();
			auto backBuffer = GetCurrentBackBuffer();
			auto rtv = GetCurrentRenderTargetView();
			auto dsv = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();

			// Clear the render targets.
			{
				TransitionResource(commandList, backBuffer,
				                   D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);

				FLOAT clearColor[] = {0, 0, 1, 1.0f};

				ClearRTV(commandList, rtv, clearColor);
				ClearDepth(commandList, dsv);
			}

			commandList->SetPipelineState(m_PipelineState);
			commandList->SetGraphicsRootSignature(m_RootSignature.Get());

			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			commandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
			commandList->IASetIndexBuffer(&m_IndexBufferView);

			commandList->RSSetViewports(1, &m_Viewport);
			commandList->RSSetScissorRects(1, &m_ScissorRect);

			commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

			auto model = models[0];
			auto tr = model->GetTransform();

			// Update the MVP matrix
			Matrix mvpMatrix = tr->GetWorldMatrix() * camera.GetViewMatrix() * camera.GetProjectionMatrix();

			commandList->SetGraphicsRoot32BitConstants(0, sizeof(Matrix) / 4, &mvpMatrix, 0);

			commandList->DrawIndexedInstanced(g_Indicies.size(), 1, 0, 0, 0);

			// Present
			{
				TransitionResource(commandList, backBuffer,
				                   D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

				m_FenceValues[currentBackBufferIndex] = commandQueue->ExecuteCommandList(commandList);

				currentBackBufferIndex = Present();

				commandQueue->WaitForFenceValue(m_FenceValues[currentBackBufferIndex]);
			}
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
				CheckComErrorFull(ERROR, "Invalid command queue type.");
			}

			return commandQueue;
		}

		ComPtr<ID3D12DescriptorHeap> DXGraphics::CreateDescriptorHeap(UINT numDescriptors,
		                                                              D3D12_DESCRIPTOR_HEAP_TYPE type) const
		{
			D3D12_DESCRIPTOR_HEAP_DESC desc = {};
			desc.Type = type;
			desc.NumDescriptors = numDescriptors;
			desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			desc.NodeMask = 0;

			ComPtr<ID3D12DescriptorHeap> descriptorHeap;
			CheckComError(m_d3d12Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

			return descriptorHeap;
		}

		UINT DXGraphics::GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const
		{
			return m_d3d12Device->GetDescriptorHandleIncrementSize(type);
		}

		bool DXGraphics::InitializeDirectX(HWND hwnd)
		{
			// Check for DirectX Math library support.
			if (!XMVerifyCPUSupport())
			{
				MessageBoxA(hwnd, "Failed to verify DirectX Math library support.", "Error", MB_OK | MB_ICONERROR);
				return false;
			}


			SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
			m_dxgiAdapter = GetAdapter(false);

			if (m_dxgiAdapter)
			{
				m_d3d12Device = CreateDevice(m_dxgiAdapter);
			}

			if (m_d3d12Device)
			{
				auto device = GetDevice();
				m_DirectCommandQueue = std::make_shared<CommandQueue>(device, D3D12_COMMAND_LIST_TYPE_DIRECT);
				m_ComputeCommandQueue = std::make_shared<CommandQueue>(device, D3D12_COMMAND_LIST_TYPE_COMPUTE);
				m_CopyCommandQueue = std::make_shared<CommandQueue>(device, D3D12_COMMAND_LIST_TYPE_COPY);
				m_TearingSupported = CheckTearingSupport();
			}


			m_dxgiSwapChain = CreateSwapChain();
			m_d3d12RTVDescriptorHeap = CreateDescriptorHeap(BufferCount, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
			m_RTVDescriptorSize = GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

			UpdateRenderTargetViews();


			m_ScissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
			m_Viewport = (CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<float>(windowWidth),
			                               static_cast<float>(windowHeight)));

			return true;
		}

		bool DXGraphics::InitializeShaders()
		{
			auto device = GetDevice();
			auto commandQueue = GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
			auto commandList = commandQueue->GetCommandList();

			// Upload vertex buffer data.
			ComPtr<ID3D12Resource> intermediateVertexBuffer;
			UpdateBufferResource(commandList,
			                     &m_VertexBuffer, &intermediateVertexBuffer,
			                     g_Vertices.size(), sizeof(VertexPosColor), g_Vertices.data());

			// Create the vertex buffer view.
			m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
			m_VertexBufferView.SizeInBytes = (g_Vertices.size() * sizeof(VertexPosColor));
			m_VertexBufferView.StrideInBytes = sizeof(VertexPosColor);

			// Upload index buffer data.
			ComPtr<ID3D12Resource> intermediateIndexBuffer;
			UpdateBufferResource(commandList,
			                     &m_IndexBuffer, &intermediateIndexBuffer,
			                     (g_Indicies.size()), sizeof(WORD), g_Indicies.data());

			// Create index buffer view.
			m_IndexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
			m_IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
			m_IndexBufferView.SizeInBytes = sizeof(WORD) * g_Indicies.size();

			// Create the descriptor heap for the depth-stencil view.
			D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
			dsvHeapDesc.NumDescriptors = 1;
			dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			CheckComError(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DSVHeap)));


			ComPtr<ID3DBlob> vertexShader, pixelShader, errorBlob;
			{
				// Compile the shaders
				HRESULT hr = D3DCompileFromFile(L"VertexShader.hlsl", nullptr, nullptr, "main", "vs_5_1",
				                                D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0,
				                                vertexShader.GetAddressOf(), errorBlob.GetAddressOf());
				if (!SUCCEEDED(hr))
				{
					const char* errMessage = (const char*)errorBlob.Get()->GetBufferPointer();
					printf("%s", errMessage);
				}
				hr = D3DCompileFromFile(L"PixelShader.hlsl", nullptr, nullptr, "main", "ps_5_1",
				                        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, pixelShader.GetAddressOf(),
				                        errorBlob.GetAddressOf());
				if (!SUCCEEDED(hr))
				{
					const char* errMessage = (const char*)errorBlob.Get()->GetBufferPointer();
					printf("%s", errMessage);
				}
			}


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
			CheckComError(D3DX12SerializeVersionedRootSignature(&rootSignatureDescription,
				featureData.HighestVersion, &rootSignatureBlob, &errorBlob));
			// Create the root signature.
			CheckComError(device->CreateRootSignature(0, rootSignatureBlob->GetBufferPointer(),
				rootSignatureBlob->GetBufferSize(), IID_PPV_ARGS(&m_RootSignature)));


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
			pipelineStateStream.VS = {
				reinterpret_cast<UINT8*>(vertexShader->GetBufferPointer()), vertexShader->GetBufferSize()
			};
			pipelineStateStream.PS = {
				reinterpret_cast<UINT8*>(pixelShader->GetBufferPointer()), pixelShader->GetBufferSize()
			};
			pipelineStateStream.DSVFormat = DXGI_FORMAT_D32_FLOAT;
			pipelineStateStream.RTVFormats = rtvFormats;

			D3D12_PIPELINE_STATE_STREAM_DESC pipelineStateStreamDesc = {
				sizeof(PipelineStateStream), &pipelineStateStream
			};


			CheckComError(device->CreatePipelineState(&pipelineStateStreamDesc, IID_PPV_ARGS(&m_PipelineState)));

			auto fenceValue = commandQueue->ExecuteCommandList(commandList);
			commandQueue->WaitForFenceValue(fenceValue);


			// Resize/Create the depth buffer.
			ResizeDepthBuffer(windowWidth, windowHeight);

			return true;
		}

		bool DXGraphics::InitializeScene()
		{
			auto model = new Model();
			if (model->Initialize(g_Vertices, g_Indicies))
			{
				models.push_back(model);
			}


			auto camTr = camera.GetTransform();
			camTr->IsUseOnlyParentPosition = true;

			camera.SetProjectionValues(45.0f, static_cast<float>(windowWidth) / static_cast<float>(windowHeight), 0.1f,
			                           1000.0f);
			camTr->SetPosition(0.0f, 0, 10);


			return true;
		}


		ComPtr<IDXGIAdapter4> DXGraphics::GetAdapter(bool bUseWarp) const
		{
			ComPtr<IDXGIFactory4> dxgiFactory;
			UINT createFactoryFlags = 0;
#if defined(_DEBUG)
			createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

			CheckComError(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory)));

			ComPtr<IDXGIAdapter1> dxgiAdapter1;
			ComPtr<IDXGIAdapter4> dxgiAdapter4;

			if (bUseWarp)
			{
				CheckComError(dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&dxgiAdapter1)));
				CheckComError(dxgiAdapter1.As(&dxgiAdapter4));
			}
			else
			{
				SIZE_T maxDedicatedVideoMemory = 0;
				for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &dxgiAdapter1) != DXGI_ERROR_NOT_FOUND; ++i)
				{
					DXGI_ADAPTER_DESC1 dxgiAdapterDesc1;
					dxgiAdapter1->GetDesc1(&dxgiAdapterDesc1);

					if ((dxgiAdapterDesc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
						SUCCEEDED(D3D12CreateDevice(dxgiAdapter1.Get(),
							D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) &&
						dxgiAdapterDesc1.DedicatedVideoMemory > maxDedicatedVideoMemory)
					{
						maxDedicatedVideoMemory = dxgiAdapterDesc1.DedicatedVideoMemory;
						CheckComError(dxgiAdapter1.As(&dxgiAdapter4));
					}
				}
			}

			return dxgiAdapter4;
		}

		ComPtr<ID3D12Device2> DXGraphics::CreateDevice(const ComPtr<IDXGIAdapter4>& adapter) const
		{
			ComPtr<ID3D12Device2> d3d12Device2;
			CheckComError(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12Device2)));

			// Enable debug messages in debug mode.
#if defined(_DEBUG)
			ComPtr<ID3D12InfoQueue> pInfoQueue;
			if (SUCCEEDED(d3d12Device2.As(&pInfoQueue)))
			{
				pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
				pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
				pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

				// Suppress whole categories of messages
				//D3D12_MESSAGE_CATEGORY Categories[] = {};

				// Suppress messages based on their severity level
				D3D12_MESSAGE_SEVERITY Severities[] =
				{
					D3D12_MESSAGE_SEVERITY_INFO
				};

				// Suppress individual messages by their ID
				D3D12_MESSAGE_ID DenyIds[] = {
					D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
					// I'm really not sure how to avoid this message.
					D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
					// This warning occurs when using capture frame while graphics debugging.
					D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE,
					// This warning occurs when using capture frame while graphics debugging.
				};

				D3D12_INFO_QUEUE_FILTER NewFilter = {};
				NewFilter.DenyList.NumSeverities = _countof(Severities);
				NewFilter.DenyList.pSeverityList = Severities;
				NewFilter.DenyList.NumIDs = _countof(DenyIds);
				NewFilter.DenyList.pIDList = DenyIds;

				CheckComError(pInfoQueue->PushStorageFilter(&NewFilter));
			}
#endif

			return d3d12Device2;
		}

		bool DXGraphics::CheckTearingSupport() const
		{
			BOOL allowTearing = FALSE;

			// Rather than create the DXGI 1.5 factory interface directly, we create the
			// DXGI 1.4 interface and query for the 1.5 interface. This is to enable the 
			// graphics debugging tools which will not support the 1.5 factory interface 
			// until a future update.
			ComPtr<IDXGIFactory4> factory4;
			if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4))))
			{
				ComPtr<IDXGIFactory5> factory5;
				if (SUCCEEDED(factory4.As(&factory5)))
				{
					factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING,
					                              &allowTearing, sizeof(allowTearing));
				}
			}

			return allowTearing == TRUE;
		}

		void DXGraphics::TransitionResource(const ComPtr<ID3D12GraphicsCommandList2>& commandList,
		                                    const ComPtr<ID3D12Resource>& resource,
		                                    const D3D12_RESOURCE_STATES beforeState,
		                                    const D3D12_RESOURCE_STATES afterState)
		{
			CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
				resource.Get(),
				beforeState, afterState);

			commandList->ResourceBarrier(1, &barrier);
		}

		void DXGraphics::ClearRTV(const ComPtr<ID3D12GraphicsCommandList2>& commandList,
		                          const D3D12_CPU_DESCRIPTOR_HANDLE rtv,
		                          float* clearColor)
		{
			commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
		}

		void DXGraphics::ClearDepth(const ComPtr<ID3D12GraphicsCommandList2>& commandList,
		                            const D3D12_CPU_DESCRIPTOR_HANDLE dsv,
		                            const FLOAT depth)
		{
			commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, depth, 0, 0, nullptr);
		}

		void DXGraphics::UpdateBufferResource(const ComPtr<ID3D12GraphicsCommandList2>& commandList,
		                                      ID3D12Resource** pDestinationResource,
		                                      ID3D12Resource** pIntermediateResource, const size_t numElements,
		                                      const size_t elementSize, const void* bufferData,
		                                      const D3D12_RESOURCE_FLAGS flags)
		{
			auto device = GetDevice();

			const size_t bufferSize = numElements * elementSize;

			// Create a committed resource for the GPU resource in a default heap.
			CheckComError(device->CreateCommittedResource(
				&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
				D3D12_HEAP_FLAG_NONE,
				&CD3DX12_RESOURCE_DESC::Buffer(bufferSize, flags),
				D3D12_RESOURCE_STATE_COPY_DEST,
				nullptr,
				IID_PPV_ARGS(pDestinationResource)));

			// Create an committed resource for the upload.
			if (bufferData)
			{
				CheckComError(device->CreateCommittedResource(
					&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
					D3D12_HEAP_FLAG_NONE,
					&CD3DX12_RESOURCE_DESC::Buffer(bufferSize),
					D3D12_RESOURCE_STATE_GENERIC_READ,
					nullptr,
					IID_PPV_ARGS(pIntermediateResource)));

				D3D12_SUBRESOURCE_DATA subresourceData;
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

			auto device = GetDevice();

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
			));

			// Update the depth-stencil view.
			D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
			dsv.Format = DXGI_FORMAT_D32_FLOAT;
			dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			dsv.Texture2D.MipSlice = 0;
			dsv.Flags = D3D12_DSV_FLAG_NONE;

			device->CreateDepthStencilView(m_DepthBuffer.Get(), &dsv,
			                               m_DSVHeap->GetCPUDescriptorHandleForHeapStart());
		}

		void DXGraphics::Flush() const
		{
			m_DirectCommandQueue->Flush();
			m_ComputeCommandQueue->Flush();
			m_CopyCommandQueue->Flush();
		}

		void DXGraphics::UpdateRenderTargetViews()
		{
			CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_d3d12RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

			for (int i = 0; i < BufferCount; ++i)
			{
				ComPtr<ID3D12Resource> backBuffer;
				CheckComError(m_dxgiSwapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));

				m_d3d12Device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);

				m_d3d12BackBuffers[i] = backBuffer;

				rtvHandle.Offset(m_RTVDescriptorSize);
			}
		}


		UINT DXGraphics::GetCurrentBackBufferIndex() const
		{
			return m_CurrentBackBufferIndex;
		}

		UINT DXGraphics::Present()
		{
			const UINT syncInterval = isVsync ? 1 : 0;
			const UINT presentFlags = m_IsTearingSupported && !isVsync ? DXGI_PRESENT_ALLOW_TEARING : 0;
			CheckComError(m_dxgiSwapChain->Present(syncInterval, presentFlags));
			m_CurrentBackBufferIndex = m_dxgiSwapChain->GetCurrentBackBufferIndex();

			return m_CurrentBackBufferIndex;
		}

		D3D12_CPU_DESCRIPTOR_HANDLE DXGraphics::GetCurrentRenderTargetView() const
		{
			return CD3DX12_CPU_DESCRIPTOR_HANDLE(m_d3d12RTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
			                                     m_CurrentBackBufferIndex, m_RTVDescriptorSize);
		}

		ComPtr<ID3D12Resource> DXGraphics::GetCurrentBackBuffer() const
		{
			return m_d3d12BackBuffers[m_CurrentBackBufferIndex];
		}

		ComPtr<IDXGISwapChain4> DXGraphics::CreateSwapChain()
		{
			ComPtr<IDXGISwapChain4> dxgiSwapChain4;
			ComPtr<IDXGIFactory4> dxgiFactory4;
			UINT createFactoryFlags = 0;
#if defined(_DEBUG)
			createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
#endif

			CheckComError(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&dxgiFactory4)));

			DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
			swapChainDesc.Width = windowWidth;
			swapChainDesc.Height = windowHeight;
			swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			swapChainDesc.Stereo = FALSE;
			swapChainDesc.SampleDesc = {1, 0};
			swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.BufferCount = BufferCount;
			swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
			swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
			// It is recommended to always allow tearing if tearing support is available.
			swapChainDesc.Flags = m_IsTearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
			ID3D12CommandQueue* pCommandQueue = GetCommandQueue()->GetD3D12CommandQueue().Get();

			ComPtr<IDXGISwapChain1> swapChain1;
			CheckComError(dxgiFactory4->CreateSwapChainForHwnd(
				pCommandQueue,
				m_hwnd,
				&swapChainDesc,
				nullptr,
				nullptr,
				&swapChain1));

			// Disable the Alt+Enter fullscreen toggle feature. Switching to fullscreen
			// will be handled manually.
			CheckComError(dxgiFactory4->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER));

			CheckComError(swapChain1.As(&dxgiSwapChain4));

			m_CurrentBackBufferIndex = dxgiSwapChain4->GetCurrentBackBufferIndex();

			return dxgiSwapChain4;
		}
	}
}
