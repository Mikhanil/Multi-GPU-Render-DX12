#include "MultiAdapter.h"
#include "GCommandList.h"
#include "Window.h"
#include "GTexture.h"

bool MultiAdapter::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	for (int i = 0; i < globalCountFrameResources; ++i)
	{
		frameResources.push_back(
			std::make_unique<FrameResource>(GDevice::GetDevice(), 1, 1, 1));
	}

	auto primeDevice = GDevice::GetDevice(GraphicAdapterPrimary);
	auto secondDevice = GDevice::GetDevice(GraphicsAdapterSecond);
	
	
	return true;
}

void MultiAdapter::Update(const GameTimer& gt)
{
	const auto commandQueue = GDevice::GetDevice(GraphicAdapterPrimary)->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);

	currentFrameResourceIndex = (currentFrameResourceIndex + 1) % globalCountFrameResources;
	currentFrameResource = frameResources[currentFrameResourceIndex].get();

	if (currentFrameResource->FenceValue != 0 && !commandQueue->IsFinish(currentFrameResource->FenceValue))
	{
		commandQueue->WaitForFenceValue(currentFrameResource->FenceValue);
	}	
}

void MultiAdapter::Draw(const GameTimer& gt)
{
	if (isResizing) return;

	auto primeDevice = GDevice::GetDevice(GraphicAdapterPrimary);
	
	auto graphicsQueue = primeDevice->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
	graphicsQueue->StartPixEvent(L"Rendering");		
	auto cmdList = graphicsQueue->GetCommandList();


	cmdList->SetViewports(&primeViewport, 1);
	cmdList->SetScissorRects(&primeRect, 1);

	cmdList->TransitionBarrier((MainWindow->GetCurrentBackBuffer()), D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmdList->FlushResourceBarriers();

	cmdList->ClearRenderTarget(MainWindow->GetRTVMemory(), MainWindow->GetCurrentBackBufferIndex(), DirectX::Colors::Yellow, &primeRect, 1);

	cmdList->SetScissorRects(&secondRect, 1);

	cmdList->ClearRenderTarget(MainWindow->GetRTVMemory(), MainWindow->GetCurrentBackBufferIndex(), DirectX::Colors::Red, &secondRect, 1);
	
	cmdList->TransitionBarrier((MainWindow->GetCurrentBackBuffer()), D3D12_RESOURCE_STATE_PRESENT);
	cmdList->FlushResourceBarriers();

	currentFrameResource->FenceValue = graphicsQueue->ExecuteCommandList(cmdList);
	graphicsQueue->EndPixEvent();
	
	
	backBufferIndex = MainWindow->Present();
}

void MultiAdapter::OnResize()
{
	D3DApp::OnResize();

	primeViewport = MainWindow->GetViewPort();
	primeRect = MainWindow->GetRect();

	primeRect = D3D12_RECT{ 0,0, MainWindow->GetClientWidth() / 2, MainWindow->GetClientHeight() };

	secondRect = D3D12_RECT{ MainWindow->GetClientWidth() / 2,0, MainWindow->GetClientWidth() , MainWindow->GetClientHeight() };
}
