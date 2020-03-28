#pragma once
#include <Windows.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include "Camera.h"
#include "Model.h"
#include "CommandQueue.h"


namespace GameEngine
{
	namespace Graphics
	{
		using namespace Microsoft::WRL;
		using namespace DirectX;
		class DXGraphics : public std::enable_shared_from_this<DXGraphics>
		{
		public:

			DXGraphics()
			{
			}

			bool Initialize(HWND hwnd, int width, int height, bool vSync);
			void RenderFrame();

			~DXGraphics()
			{
				
			}

			std::shared_ptr<CommandQueue> GetCommandQueue(
				D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;

			ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
				UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type) const;
			UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

			Camera camera;

			std::vector<Model*> models;
			bool m_TearingSupported;
			HWND m_hwnd;

			ComPtr<ID3D12Device2> GetDevice()
			{
				return m_d3d12Device;
			}


			XMMATRIX m_ModelMatrix;
			XMMATRIX m_ViewMatrix;
			XMMATRIX m_ProjectionMatrix;
			
		private:

			

			ComPtr<IDXGIAdapter4> GetAdapter(bool bUseWarp) const;
			ComPtr<ID3D12Device2> CreateDevice(const ComPtr<IDXGIAdapter4>& adapter) const;
			bool CheckTearingSupport() const;
			
			static const UINT BufferCount = 3;


			UINT GetCurrentBackBufferIndex() const;
			UINT Present();
			D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRenderTargetView() const;
			ComPtr<ID3D12Resource> GetCurrentBackBuffer() const;
			ComPtr<IDXGISwapChain4> CreateSwapChain();
			void UpdateRenderTargetViews();


			void Flush() const;
			static void TransitionResource(const ComPtr<ID3D12GraphicsCommandList2>& commandList,
			                               const ComPtr<ID3D12Resource>& resource,
				D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState);


			static void ClearRTV(const ComPtr<ID3D12GraphicsCommandList2>& commandList,
				D3D12_CPU_DESCRIPTOR_HANDLE rtv, FLOAT* clearColor);


			static void ClearDepth(const ComPtr<ID3D12GraphicsCommandList2>& commandList,
				D3D12_CPU_DESCRIPTOR_HANDLE dsv, FLOAT depth = 1.0f);


			void UpdateBufferResource(const ComPtr<ID3D12GraphicsCommandList2>& commandList,
				ID3D12Resource** pDestinationResource, ID3D12Resource** pIntermediateResource,
				size_t numElements, size_t elementSize, const void* bufferData,
				D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE);


			void ResizeDepthBuffer(int width, int height);

			uint64_t m_FenceValues[BufferCount] = {};


			ComPtr<ID3D12Resource> m_VertexBuffer;
			D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView;


			ComPtr<ID3D12Resource> m_IndexBuffer;
			D3D12_INDEX_BUFFER_VIEW m_IndexBufferView;

			ComPtr<ID3D12Resource> m_DepthBuffer;
			ComPtr<ID3D12DescriptorHeap> m_DSVHeap;

			ComPtr<ID3D12RootSignature> m_RootSignature;

			ID3D12PipelineState* m_PipelineState;

			D3D12_VIEWPORT m_Viewport;
			D3D12_RECT m_ScissorRect;

			float m_FoV;
			
			bool m_Fullscreen = false;
			bool isVsync;
			int videoCardMemory;
			char videoCardDescription[128];
			unsigned int bufferIndex;
			UINT m_CurrentBackBufferIndex;

			ComPtr<IDXGIAdapter4> m_dxgiAdapter;
			ComPtr<ID3D12Device2> m_d3d12Device;			

			std::shared_ptr<CommandQueue> m_DirectCommandQueue;
			std::shared_ptr<CommandQueue> m_ComputeCommandQueue;
			std::shared_ptr<CommandQueue> m_CopyCommandQueue;

			int windowWidth;
			int windowHeight;

			ComPtr<IDXGISwapChain4> m_dxgiSwapChain;
			ComPtr<ID3D12DescriptorHeap> m_d3d12RTVDescriptorHeap;
			ComPtr<ID3D12Resource> m_d3d12BackBuffers[BufferCount];

			UINT m_RTVDescriptorSize;
			RECT m_WindowRect;
			bool m_IsTearingSupported;

			bool InitializeDirectX(HWND hwnd);
			bool InitializeShaders();
			bool InitializeScene();
		
			


			
		};
	}
}
