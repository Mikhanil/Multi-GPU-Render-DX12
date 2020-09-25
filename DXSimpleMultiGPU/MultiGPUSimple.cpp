#include "MultiGPUSimple.h"
#include "GCommandList.h"
#include "GCommandQueue.h"
#include "Window.h"
#include "GCrossAdapterResource.h"
#include "GDeviceFactory.h"

MultiGPUSimple::MultiGPUSimple(HINSTANCE hInstance): D3DApp(hInstance)
{
}

bool MultiGPUSimple::Initialize()
{
	auto firstDevice = GDeviceFactory::GetDevice(GraphicAdapterPrimary);
	auto otherDevice = GDeviceFactory::GetDevice(GraphicAdapterSecond);

	if (otherDevice->IsCrossAdapterTextureSupported())
	{
		primeDevice = otherDevice;
		secondDevice = firstDevice;
	}
	else
	{		
		primeDevice = firstDevice;
		secondDevice = otherDevice;
	}

	primeRTV = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, globalCountFrameResources);
	sharedRTV = secondDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, globalCountFrameResources);
	
	if (!D3DApp::Initialize())
		return false;

	
	
	

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = GetSRGBFormat(backBufferFormat);
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	
	for (int i = 0; i < globalCountFrameResources; ++i)
	{
		frameResources.push_back(
			std::make_unique<FrameResource>(primeDevice, 1, 1, 1));

		auto backBufferDesc = MainWindow->GetBackBuffer(i).GetD3D12ResourceDesc();

		crossAdapterBackBuffers.push_back(std::make_unique<GCrossAdapterResource>(backBufferDesc, primeDevice, secondDevice, L"Shared back buffer", D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

		if (primeDevice->IsCrossAdapterTextureSupported())
		{
			crossAdapterBackBuffers[i]->GetPrimeResource().CreateRenderTargetView(&rtvDesc, &primeRTV, i);						
			primeDeviceBackBuffers.push_back(crossAdapterBackBuffers[i]->GetPrimeResource());
		}
		else
		{
			auto primeBB = GTexture(primeDevice, MainWindow->GetBackBuffer(i).GetD3D12ResourceDesc(), L"Prime device Back Buffer" + std::to_wstring(i), TextureUsage::RenderTarget);
			primeBB.CreateRenderTargetView(&rtvDesc, &primeRTV, i);
			primeDeviceBackBuffers.push_back(std::move(primeBB));
		}
	}

	primeDevice->SharedFence(primeFence, secondDevice, sharedFence, sharedFenceValue);
	
	
	return true;
}

void MultiGPUSimple::Update(const GameTimer& gt)
{
	const auto commandQueue = secondDevice->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);

	currentFrameResourceIndex = (currentFrameResourceIndex + 1) % globalCountFrameResources;
	currentFrameResource = frameResources[currentFrameResourceIndex].get();

	if (currentFrameResource->FenceValue != 0 && !commandQueue->IsFinish(currentFrameResource->FenceValue))
	{
		commandQueue->WaitForFenceValue(currentFrameResource->FenceValue);
	}	
}

void MultiGPUSimple::Draw(const GameTimer& gt)
{
	if (isResizing) return;

	auto currentBackBufferIndex = MainWindow->GetCurrentBackBufferIndex();
	auto primeDeviceRenderingQueue = primeDevice->GetCommandQueue();
	auto primeDeviceCopyQueue = primeDevice->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);
	auto secondDeviceRenderingQueue = secondDevice->GetCommandQueue();

	auto cmdList = secondDeviceRenderingQueue->GetCommandList();

	cmdList->SetViewports(&primeViewport, 1);
	cmdList->SetScissorRects(&secondRect, 1);
	cmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmdList->FlushResourceBarriers();
	cmdList->ClearRenderTarget(&sharedRTV, MainWindow->GetCurrentBackBufferIndex(),
	                           DirectX::Colors::Red, &secondRect, 1);
	cmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->FlushResourceBarriers();

	const auto secondSceneRenderFence = secondDeviceRenderingQueue->ExecuteCommandList(cmdList);


	cmdList = primeDeviceRenderingQueue->GetCommandList();
	cmdList->SetViewports(&primeViewport, 1);
	cmdList->SetScissorRects(&primeRect, 1);
	cmdList->TransitionBarrier(primeDeviceBackBuffers[currentBackBufferIndex], D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmdList->FlushResourceBarriers();
	cmdList->ClearRenderTarget(&primeRTV, currentBackBufferIndex, DirectX::Colors::Yellow, &primeRect, 1);
	cmdList->TransitionBarrier(crossAdapterBackBuffers[currentBackBufferIndex]->GetPrimeResource(),
	                           D3D12_RESOURCE_STATE_COMMON);
	cmdList->TransitionBarrier(primeDeviceBackBuffers[currentBackBufferIndex], D3D12_RESOURCE_STATE_COMMON);
	cmdList->FlushResourceBarriers();	
	primeDeviceRenderingQueue->ExecuteCommandList(cmdList);
	
	cmdList = primeDeviceCopyQueue->GetCommandList();
	cmdList->TransitionBarrier(crossAdapterBackBuffers[currentBackBufferIndex]->GetPrimeResource(),
		D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->TransitionBarrier(primeDeviceBackBuffers[currentBackBufferIndex], D3D12_RESOURCE_STATE_COPY_SOURCE);
	cmdList->FlushResourceBarriers();
	cmdList->CopyTextureRegion(crossAdapterBackBuffers[currentBackBufferIndex]->GetPrimeResource(), 0, 0, 0, primeDeviceBackBuffers[currentBackBufferIndex], &copyRegionBox);
	cmdList->TransitionBarrier(crossAdapterBackBuffers[currentBackBufferIndex]->GetPrimeResource(),
		D3D12_RESOURCE_STATE_COMMON);
	cmdList->TransitionBarrier(primeDeviceBackBuffers[currentBackBufferIndex], D3D12_RESOURCE_STATE_COMMON);
	cmdList->FlushResourceBarriers();

	primeDeviceCopyQueue->Wait(*primeDeviceRenderingQueue.get());
	const auto primeDeviceCopyEndFenceValue = primeDeviceCopyQueue->ExecuteCommandList(cmdList);
	primeDeviceCopyQueue->Signal(primeFence, primeDeviceCopyEndFenceValue);

	cmdList = secondDeviceRenderingQueue->GetCommandList();
	cmdList->TransitionBarrier(crossAdapterBackBuffers[currentBackBufferIndex]->GetSharedResource(),
		D3D12_RESOURCE_STATE_COPY_SOURCE);
	cmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->FlushResourceBarriers();
	cmdList->CopyTextureRegion(MainWindow->GetCurrentBackBuffer(), 0, 0, 0, crossAdapterBackBuffers[currentBackBufferIndex]->GetSharedResource(), &copyRegionBox);
	cmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
	cmdList->TransitionBarrier(crossAdapterBackBuffers[currentBackBufferIndex]->GetSharedResource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->FlushResourceBarriers();

	secondDeviceRenderingQueue->WaitForFenceValue(secondSceneRenderFence);
	secondDeviceRenderingQueue->Wait(sharedFence, primeDeviceCopyEndFenceValue);
	currentFrameResource->FenceValue = secondDeviceRenderingQueue->ExecuteCommandList(cmdList);
	
	
	backBufferIndex = MainWindow->Present();
}

void MultiGPUSimple::OnResize()
{
	D3DApp::OnResize();

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = GetSRGBFormat(backBufferFormat);
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	for (int i = 0; i < globalCountFrameResources; ++i)
	{
		MainWindow->GetBackBuffer(i).CreateRenderTargetView(&rtvDesc, &sharedRTV, i);
	}

	
	primeViewport.Height = static_cast<float>(MainWindow->GetClientHeight());
	primeViewport.Width = static_cast<float>(MainWindow->GetClientWidth());
	primeViewport.MinDepth = 0.0f;
	primeViewport.MaxDepth = 1.0f;
	primeViewport.TopLeftX = 0;
	primeViewport.TopLeftY = 0;	
		
	fullrect = D3D12_RECT{ 0,0, MainWindow->GetClientWidth(), MainWindow->GetClientHeight() };
	primeRect = D3D12_RECT{ 0,0, MainWindow->GetClientWidth() / 2, MainWindow->GetClientHeight() };
	copyRegionBox = CD3DX12_BOX(primeRect.left, primeRect.top, primeRect.right, primeRect.bottom);
	
	secondRect = D3D12_RECT{ MainWindow->GetClientWidth() / 2,0, MainWindow->GetClientWidth() , MainWindow->GetClientHeight() };
}

bool MultiGPUSimple::InitMainWindow()
{
	MainWindow = CreateRenderWindow(secondDevice, mainWindowCaption, 1920, 1080, false);

	return true;
}
