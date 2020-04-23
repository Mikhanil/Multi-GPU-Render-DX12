#include "ShapesApp.h"
#include "ResourceUploadBatch.h"
#include "DDSTextureLoader.h"


ShapesApp::ShapesApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
	if (dxDevice != nullptr)
		FlushCommandQueue();
}

bool ShapesApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;	
		
	BuildFrameResources();
	
	return true;
}


void ShapesApp::Update(const GameTimer& gt)
{
	currentFrameResourceIndex = (currentFrameResourceIndex + 1) % globalCountFrameResources;
	currentFrameResource = frameResources[currentFrameResourceIndex].get();
	
	if (currentFrameResource->FenceValue != 0 && currentFrameResource->FenceValue > fence->GetCompletedValue())
	{
		const HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(fence->SetEventOnCompletion(currentFrameResource->FenceValue, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

}

void ShapesApp::Draw(const GameTimer& gt)
{	
	auto frameAlloc = currentFrameResource->commandListAllocator;
	
	ThrowIfFailed(frameAlloc->Reset());

	ThrowIfFailed(commandList->Reset(frameAlloc.Get(), nullptr));	
	
	commandList->RSSetViewports(1, &screenViewport);
	commandList->RSSetScissorRects(1, &scissorRect);

	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	
	commandList->ClearRenderTargetView(GetCurrentBackBufferView(), DirectX::Colors::BlanchedAlmond, 0, nullptr);
	commandList->ClearDepthStencilView(GetDepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	commandList->OMSetRenderTargets(1, &GetCurrentBackBufferView(), true, &GetDepthStencilView());	
	
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ExecuteCommandList();

	ThrowIfFailed(swapChain->Present(0, 0));
	currBackBufferIndex = (currBackBufferIndex + 1) % swapChainBufferCount;

	currentFrameResource->FenceValue = ++currentFence;
	
	commandQueue->Signal(fence.Get(), currentFence);

}

void ShapesApp::BuildFrameResources()
{
	//по смыслу, создаем 3 комманд алокатора и итерируемся по ним в update и draw
	for (int i = 0; i < globalCountFrameResources; ++i)
	{
		frameResources.push_back(std::make_unique<FrameResource>(dxDevice.Get()));
	}
}

