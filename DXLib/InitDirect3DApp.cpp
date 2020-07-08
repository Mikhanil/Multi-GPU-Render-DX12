//#include "InitDirect3DApp.h"
//
//InitDirect3DApp::InitDirect3DApp(HINSTANCE hInstance)
//: D3DApp(hInstance) 
//{
//}
//
//InitDirect3DApp::~InitDirect3DApp()
//{
//}
//
//bool InitDirect3DApp::Initialize()
//{
//    if(!D3DApp::Initialize())
//		return false;
//		
//	return true;
//}
//
//void InitDirect3DApp::OnResize()
//{
//	D3DApp::OnResize();
//}
//
//void InitDirect3DApp::Update(const GameTimer& gt)
//{
//
//}
//
//void InitDirect3DApp::Draw(const GameTimer& gt)
//{
//    // Reuse the memory associated with command recording.
//    // We can only reset when the associated command lists have finished execution on the GPU.
//	ThrowIfFailed(directCommandListAlloc->Reset());
//
//	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
//    // Reusing the command list reuses memory.
//    ThrowIfFailed(commandList->Reset(directCommandListAlloc.Get(), nullptr));
//
//	// Indicate a state transition on the resource usage.
//	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
//		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));
//
//    // Set the viewport and scissor rect.  This needs to be reset whenever the command list is reset.
//    commandList->RSSetViewports(1, &screenViewport);
//    commandList->RSSetScissorRects(1, &scissorRect);
//
//    // Clear the back buffer and depth buffer.
//	commandList->ClearRenderTargetView(GetCurrentBackBufferView(), Colors::Blue, 0, nullptr);
//	commandList->ClearDepthStencilView(GetDepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
//	
//    // Specify the buffers we are going to render to.
//	commandList->OMSetRenderTargets(1, &GetCurrentBackBufferView(), true, &GetDepthStencilView());
//	
//    // Indicate a state transition on the resource usage.
//	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
//		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));
//
//    // Done recording commands.
//	ThrowIfFailed(commandList->Close());
// 
//    // Add the command list to the queue for execution.		
//	ID3D12CommandList* executedCommandsList[] = { commandList.Get() };
//	commandQueue->ExecuteCommandLists(_countof(executedCommandsList), executedCommandsList);
//	
//	// swap the back and front buffers
//	ThrowIfFailed(swapChain->Present(0, 0));
//	currentBackBuffer = (currentBackBuffer + 1) % swapChainBufferCount;
//
//	// Wait until frame commands are complete.  This waiting is inefficient and is
//	// done for simplicity.  Later we will show how to organize our rendering code
//	// so we do not have to wait per frame.
//	FlushCommandQueue();
//}
