#pragma once
#include <Windows.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include "Camera.h"
#include "Model.h"
#include "CommandQueue.h"
#include "COMException.h"

namespace GameEngine
{
	namespace Graphics
	{
		using namespace Microsoft::WRL;
		
		class DXGraphics : public std::enable_shared_from_this<DXGraphics>
		{
		public:

			DXGraphics()
			{			}
			
			bool Initialize(HWND hwnd, int width, int height, bool vSync);
			void RenderFrame();
			~DXGraphics()
			{
				swapChain->SetFullscreenState(false, nullptr);

				// Close the object handle to the fence event.
				auto error = CloseHandle(fenceEvent);
				if (error == 0)
				{
				}

				//TODO какие-то не правильные у вас "умные" указатели, которые не могут память почистить за собой 
			}


			std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;

			Camera camera;

			std::vector<Model*> models;
			
		private:

			std::vector<VertexPosColor> g_Vertices = {
				{Vector3(-1.0f, -1.0f, -1.0f), Vector4(0.0f, 0.0f, 0.0f, 0.0f)}, // 0
				{Vector3(-1.0f, 1.0f, -1.0f), Vector4(0.0f, 1.0f, 0.0f, 0.0f)}, // 1
				{Vector3(1.0f, 1.0f, -1.0f), Vector4(1.0f, 1.0f, 0.0f, 0.0f)}, // 2
				{Vector3(1.0f, -1.0f, -1.0f), Vector4(1.0f, 0.0f, 0.0f, 0.0f)}, // 3
				{Vector3(-1.0f, -1.0f, 1.0f), Vector4(0.0f, 0.0f, 1.0f, 0.0f)}, // 4
				{Vector3(-1.0f, 1.0f, 1.0f), Vector4(0.0f, 1.0f, 1.0f, 0.0f)}, // 5
				{Vector3(1.0f, 1.0f, 1.0f), Vector4(1.0f, 1.0f, 1.0f, 0.0f)}, // 6
				{Vector3(1.0f, -1.0f, 1.0f), Vector4(1.0f, 0.0f, 1.0f, 0.0f)} // 7
			};

			std::vector<WORD> g_Indicies =
			{
				0, 1, 2, 0, 2, 3,
				4, 6, 5, 4, 7, 6,
				4, 5, 1, 4, 1, 0,
				3, 2, 6, 3, 6, 7,
				1, 5, 6, 1, 6, 2,
				4, 0, 3, 4, 3, 7
			};

			
			// Number of swapchain back buffers.
			static const UINT BufferCount = 2;

			
			bool isVsync;
			int videoCardMemory;
			char videoCardDescription[128];
			unsigned int bufferIndex;

			UINT m_CurrentBackBufferIndex;
			
			ComPtr<ID3D12Resource> backBufferRenderTarget[BufferCount];
			ComPtr<ID3D12Device2> device;
			ComPtr<ID3D12CommandAllocator> commandAllocator;
			ComPtr<ID3D12CommandQueue> d12commandQueue;
			
			
			ComPtr<ID3D12GraphicsCommandList> commandList;
			ComPtr<ID3D12DescriptorHeap> renderTargetViewHeap;		
			ComPtr<IDXGISwapChain4> swapChain;

			ComPtr<ID3D12Fence> fence;
			HANDLE fenceEvent;
			unsigned long long fenceValue;

			uint64_t fenceValues[BufferCount] = {};

			// Vertex buffer for the cube.
			ComPtr<ID3D12Resource> vertexBuffer;
			D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
			// Index buffer for the cube.
			ComPtr<ID3D12Resource> indexBuffer;
			D3D12_INDEX_BUFFER_VIEW indexBufferView;

			// Depth buffer.
			Microsoft::WRL::ComPtr<ID3D12Resource> m_DepthBuffer;
			// Descriptor heap for depth buffer.
			Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DSVHeap;

			// Root signature
			Microsoft::WRL::ComPtr<ID3D12RootSignature> m_RootSignature;

			// Pipeline state object.
			Microsoft::WRL::ComPtr<ID3D12PipelineState> m_PipelineState;

			D3D12_VIEWPORT m_Viewport;
			D3D12_RECT m_ScissorRect;

			std::shared_ptr<CommandQueue> m_DirectCommandQueue;
			std::shared_ptr<CommandQueue> m_ComputeCommandQueue;
			std::shared_ptr<CommandQueue> m_CopyCommandQueue;

			
			int windowWidth;
			int windowHeight;

			bool InitializeDirectX(HWND hwnd);
			bool InitializeShaders();
			bool InitializeScene();

			// Helper functions
			// Transition a resource
			void TransitionResource(ComPtr<ID3D12GraphicsCommandList2> commandList,
			                        ComPtr<ID3D12Resource> resource,
			                        D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState);

			// Clear a render target view.
			void ClearRTV(ComPtr<ID3D12GraphicsCommandList2> commandList,
			              D3D12_CPU_DESCRIPTOR_HANDLE rtv, FLOAT* clearColor);

			// Clear the depth of a depth-stencil view.
			void ClearDepth(ComPtr<ID3D12GraphicsCommandList2> commandList,
			                D3D12_CPU_DESCRIPTOR_HANDLE dsv, FLOAT depth = 1.0f);

			// Create a GPU buffer.
			void UpdateBufferResource(ComPtr<ID3D12GraphicsCommandList2> commandList,
			                          ID3D12Resource** pDestinationResource, ID3D12Resource** pIntermediateResource,
			                          size_t numElements, size_t elementSize, const void* bufferData,
			                          D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);

			// Resize the depth buffer to match the size of the client area.
			void ResizeDepthBuffer(int width, int height);

			void Flush();

			void UpdateRenderTargetViews();

			UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type);

			D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetView();

			UINT GetCurrentBackBufferIndex() const;

			UINT Present();

			Microsoft::WRL::ComPtr<ID3D12Resource> GetCurrentBackBuffer() const
			{
				return backBufferRenderTarget[m_CurrentBackBufferIndex];
			}
		};
	}
}

