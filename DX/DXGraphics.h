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

			DXGraphics()
			{
				pipelineState = nullptr;
			}
			
			bool Initialize(HWND hwnd, int width, int height);
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

			Camera camera;
			
		private:

			int videoCardMemory;
			char videoCardDescription[128];
			unsigned int bufferIndex;

			ComPtr<ID3D12Resource> backBufferRenderTarget[2];
			ComPtr<ID3D12Device2> device;
			ComPtr<ID3D12CommandAllocator> commandAllocator;
			ComPtr<ID3D12CommandQueue> commandQueue;
			
			//Почему не ComPtr? А без понятия, эта штука если через Com написать выываливается в ошибку доступа в Render
			ID3D12PipelineState* pipelineState;
			
			ComPtr<ID3D12GraphicsCommandList> commandList;
			ComPtr<ID3D12DescriptorHeap> renderTargetViewHeap;		
			ComPtr<IDXGISwapChain4> swapChain;

			ComPtr<ID3D12Fence> fence;
			HANDLE fenceEvent;
			unsigned long long fenceValue;
			
			int windowWidth;
			int windowHeight;

			bool InitializeDirectX(HWND hwnd);
			bool InitializeShaders();
			bool InitializeScene();
		};
	}
}

