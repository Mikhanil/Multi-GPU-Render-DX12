#pragma once
#include <Windows.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <dxgi1_5.h>
#include <dxgi1_6.h>
#include "Camera.h"

namespace GameEngine
{
	namespace Graphics
	{
		using namespace Microsoft::WRL;
		
		class DXGraphics
		{
		public:
			bool Initialize(HWND hwnd, int width, int height);
			void RenderFrame();
			~DXGraphics()
			{
				swapChain.ReleaseAndGetAddressOf();
				factory.ReleaseAndGetAddressOf();
				descHeap.ReleaseAndGetAddressOf();
				commandList.ReleaseAndGetAddressOf();
				pipelineState.ReleaseAndGetAddressOf();
				commandQueue.ReleaseAndGetAddressOf();
				commandAllocator.ReleaseAndGetAddressOf();
				device.ReleaseAndGetAddressOf();
			}

			Camera camera;
			
		private:

			// The number of swap chain back buffers.
			const static uint8_t g_NumFrames = 3;

			ComPtr<ID3D12Resource> g_BackBuffers[g_NumFrames];
			
			ComPtr<ID3D12Device2> device;
			ComPtr<ID3D12CommandAllocator> commandAllocator;
			ComPtr<ID3D12CommandQueue> commandQueue;
			ComPtr<ID3D12PipelineState> pipelineState;
			ComPtr<ID3D12GraphicsCommandList> commandList;
			ComPtr<ID3D12DescriptorHeap> descHeap;
			ComPtr<IDXGIFactory6> factory;			
			ComPtr<IDXGISwapChain4> swapChain;
			
			int windowWidth;
			int windowHeight;

			bool InitializeDirectX(HWND hwnd);
			bool InitializeShaders();
			bool InitializeScene();
		};
	}
}

