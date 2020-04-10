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

			std::shared_ptr<CommandQueue> GetCommandQueue(D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT) const;
			ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(UINT numDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE type) const;			
			UINT GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE type) const;

			Camera camera;
			std::vector<Model*> models;
			

			ComPtr<ID3D12Device2> GetDevice()
			{
				return device;
			}


			
			
		private:

			static const UINT BufferCount = 3;
			
			HWND hwnd;
			
			ComPtr<IDXGIFactory5> factory;

			ComPtr<IDXGIFactory5> GetFactory();
			
			ComPtr<IDXGIAdapter4> adapter;
			
			ComPtr<IDXGIAdapter4> GetAdapter(bool bUseWarp);			
			
			ComPtr<ID3D12Device2> CreateDevice(const ComPtr<IDXGIAdapter4>& adapter) const;
			
			bool CheckTearingSupport();
			
			


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

			uint64_t FenceValues[BufferCount] = {};


			ComPtr<ID3D12Resource> VertexBuffer;
			D3D12_VERTEX_BUFFER_VIEW VertexBufferView;


			ComPtr<ID3D12Resource> IndexBuffer;
			D3D12_INDEX_BUFFER_VIEW IndexBufferView;

			ComPtr<ID3D12Resource> DepthBuffer;
			ComPtr<ID3D12DescriptorHeap> DSVHeap;

			ComPtr<ID3D12RootSignature> RootSignature;

			ID3D12PipelineState* PipelineState;

			D3D12_VIEWPORT Viewport;
			D3D12_RECT ScissorRect;

		
		
			bool IsVsync;

			UINT CurrentBackBufferIndex;
			
			ComPtr<ID3D12Device2> device;			

			std::shared_ptr<CommandQueue> DirectCommandQueue;
			std::shared_ptr<CommandQueue> ComputeCommandQueue;
			std::shared_ptr<CommandQueue> CopyCommandQueue;

			int windowWidth;
			int windowHeight;

			ComPtr<IDXGISwapChain4> swapChain;
			ComPtr<ID3D12DescriptorHeap> d3d12RTVDescriptorHeap;
			ComPtr<ID3D12Resource> d3d12BackBuffers[BufferCount];

			UINT RTVDescriptorSize;
			bool IsTearingSupported;

			bool InitializeDirectX();
			bool InitializeShaders();
			bool InitializeScene();
		
			


			
		};
	}
}
