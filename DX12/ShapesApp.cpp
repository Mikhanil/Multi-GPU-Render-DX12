#include "ShapesApp.h"
#include "Renderer.h"
#include "GameObject.h"
#include "ModelRenderer.h"
#include "Camera.h"
#include <ppl.h>
#include "CameraController.h"
#include "Reflected.h"
#include "ObjectMover.h"
#include "Shadow.h"
#include "pix3.h"

ShapesApp::ShapesApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
	mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	mSceneBounds.Radius = sqrtf(10.0f * 10.0f + 15.0f * 15.0f);
}

ShapesApp::~ShapesApp()
{
	if (dxDevice != nullptr)
		FlushCommandQueue();
}

Keyboard* ShapesApp::GetKeyboard()
{
	return &keyboard;
}

Mouse* ShapesApp::GetMouse()
{
	return &mouse;
}

Camera* ShapesApp::GetMainCamera() const
{
	return camera.get();
}

void ShapesApp::GeneratedMipMap()
{
	ThrowIfFailed(commandListDirect->Reset(directCommandListAlloc.Get(), nullptr));

	UINT requiredHeapSize = 0;

	std::vector<Texture*> generatedMipTextures;

	for (auto&& texture : textures)
	{
		texture.second->ClearTrack();

		/*ТОлько те что можно использовать как UAV*/
		if (texture.second->GetResource()->GetDesc().Flags != D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
			continue;

		if (texture.second->GetResource()->GetDesc().MipLevels > 1)
		{
			requiredHeapSize += texture.second->GetResource()->GetDesc().MipLevels - 1;
			generatedMipTextures.push_back(texture.second.get());
		}
	}

	if (requiredHeapSize == 0)
	{
		return;
	}

	auto genMipMapPSO = new GeneratedMipsPSO(dxDevice.Get());


	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 2 * requiredHeapSize;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ID3D12DescriptorHeap* descriptorHeap;
	dxDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap));
	UINT descriptorSize = dxDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	D3D12_SHADER_RESOURCE_VIEW_DESC srcTextureSRVDesc = {};
	srcTextureSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srcTextureSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;


	D3D12_UNORDERED_ACCESS_VIEW_DESC destTextureUAVDesc = {};
	destTextureUAVDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;


	commandListDirect->SetComputeRootSignature(genMipMapPSO->GetRootSignature().Get());
	commandListDirect->SetPipelineState(genMipMapPSO->GetPipelineState().Get());
	commandListDirect->SetDescriptorHeaps(1, &descriptorHeap);


	CD3DX12_CPU_DESCRIPTOR_HANDLE currentCPUHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart(), 0,
	                                               descriptorSize);

	CD3DX12_GPU_DESCRIPTOR_HANDLE currentGPUHandle(descriptorHeap->GetGPUDescriptorHandleForHeapStart(), 0,
	                                               descriptorSize);


	/*Почему-то логика взаимодействия с константным буфером как для отрисовки тут не работает*/
	//auto mipBuffer = genMipMapPSO->GetBuffer();


	for (auto& textur : generatedMipTextures)
	{
		auto texture = textur->GetResource();
		auto textureDesc = texture->GetDesc();


		commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(texture,
		                                                                            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		                                                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

		for (uint32_t TopMip = 0; TopMip < textureDesc.MipLevels - 1; TopMip++)
		{
			uint32_t dstWidth = max(textureDesc.Width >> (TopMip + 1), 1);
			uint32_t dstHeight = max(textureDesc.Height >> (TopMip + 1), 1);


			srcTextureSRVDesc.Format = textureDesc.Format;
			srcTextureSRVDesc.Texture2D.MipLevels = 1;
			srcTextureSRVDesc.Texture2D.MostDetailedMip = TopMip;
			dxDevice->CreateShaderResourceView(texture, &srcTextureSRVDesc, currentCPUHandle);
			currentCPUHandle.Offset(1, descriptorSize);


			destTextureUAVDesc.Format = textureDesc.Format;
			destTextureUAVDesc.Texture2D.MipSlice = TopMip + 1;
			dxDevice->CreateUnorderedAccessView(texture, nullptr, &destTextureUAVDesc, currentCPUHandle);
			currentCPUHandle.Offset(1, descriptorSize);

			//GenerateMipsCB mipData = {};			
			//mipData.TexelSize = Vector2{ (1.0f / dstWidth) ,(1.0f / dstHeight) };			
			//mipBuffer->CopyData(0, mipData);			
			//commandListDirect->SetComputeRootConstantBufferView(0, mipBuffer->Resource()->GetGPUVirtualAddress());

			Vector2 texelSize = Vector2{(1.0f / dstWidth), (1.0f / dstHeight)};
			commandListDirect->SetComputeRoot32BitConstants(0, 2, &texelSize, 0);


			commandListDirect->SetComputeRootDescriptorTable(1, currentGPUHandle);
			currentGPUHandle.Offset(1, descriptorSize);
			commandListDirect->SetComputeRootDescriptorTable(2, currentGPUHandle);
			currentGPUHandle.Offset(1, descriptorSize);

			commandListDirect->Dispatch(max(dstWidth / 8, 1u), max(dstHeight / 8, 1u), 1);

			commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(texture));
		}

		commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
			                                   texture, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			                                   D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}

	ExecuteCommandList();
	FlushCommandQueue();
}


bool ShapesApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	mShadowMap = std::make_unique<ShadowMap>(
		dxDevice.Get(), 2048, 2048);


	LoadTextures();
	GeneratedMipMap();


	ThrowIfFailed(commandListDirect->Reset(directCommandListAlloc.Get(), nullptr));
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildLandGeometry();
	BuildTreesGeometry();
	BuildRoomGeometry();
	BuildPSOs();
	BuildMaterials();
	BuildGameObjects();
	BuildFrameResources();
	SortGO();

	ExecuteCommandList();

	// Wait until initialization is complete.
	FlushCommandQueue();
	return true;
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();

	if (camera != nullptr)
	{
		camera->SetAspectRatio(AspectRatio());
	}
}

void ShapesApp::AnimatedMaterial(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = materials["water"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if (tu >= 1.0f)
		tu -= 1.0f;

	if (tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->SetDirty();
}

void ShapesApp::UpdateShadowTransform(const GameTimer& gt)
{
	// Only the first "main" light casts a shadow.
	Vector3 lightDir = mRotatedLightDirections[0];
	Vector3 lightPos = -2.0f * mSceneBounds.Radius * lightDir;
	Vector3 targetPos = mSceneBounds.Center;
	Vector3 lightUp = Vector3::Up;
	Matrix lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

	mLightPosW = lightPos;


	// Transform bounding sphere to light space.
	Vector3 sphereCenterLS = Vector3::Transform(targetPos, lightView);


	// Ortho frustum in light space encloses scene.
	float l = sphereCenterLS.x - mSceneBounds.Radius;
	float b = sphereCenterLS.y - mSceneBounds.Radius;
	float n = sphereCenterLS.z - mSceneBounds.Radius;
	float r = sphereCenterLS.x + mSceneBounds.Radius;
	float t = sphereCenterLS.y + mSceneBounds.Radius;
	float f = sphereCenterLS.z + mSceneBounds.Radius;

	mLightNearZ = n;
	mLightFarZ = f;
	Matrix lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	Matrix T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	Matrix S = lightView * lightProj * T;
	mLightView = lightView;
	mLightProj = lightProj;
	mShadowTransform = S;
}

void ShapesApp::UpdateShadowPassCB(const GameTimer& gt)
{
	auto view = mLightView;
	auto proj = mLightProj;

	auto viewProj = (view * proj);
	auto invView = view.Invert();
	auto invProj = proj.Invert();
	auto invViewProj = viewProj.Invert();

	UINT w = mShadowMap->Width();
	UINT h = mShadowMap->Height();

	mShadowPassCB.View = view.Transpose();
	mShadowPassCB.InvView = invView.Transpose();
	mShadowPassCB.Proj = proj.Transpose();
	mShadowPassCB.InvProj = invProj.Transpose();
	mShadowPassCB.ViewProj = viewProj.Transpose();
	mShadowPassCB.InvViewProj = invViewProj.Transpose();
	mShadowPassCB.EyePosW = mLightPosW;
	mShadowPassCB.RenderTargetSize = Vector2(static_cast<float>(w), static_cast<float>(h));
	mShadowPassCB.InvRenderTargetSize = Vector2(1.0f / w, 1.0f / h);
	mShadowPassCB.NearZ = mLightNearZ;
	mShadowPassCB.FarZ = mLightFarZ;

	auto currPassCB = currentFrameResource->PassConstantBuffer.get();
	currPassCB->CopyData(1, mShadowPassCB);
}

void ShapesApp::Update(const GameTimer& gt)
{
	// Cycle through the circular frame resource array.
	currentFrameResourceIndex = (currentFrameResourceIndex + 1) % globalCountFrameResources;
	currentFrameResource = frameResources[currentFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (currentFrameResource->FenceValue != 0 && currentFrameResource->FenceValue > fence->GetCompletedValue())
	{
		const HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(fence->SetEventOnCompletion(currentFrameResource->FenceValue, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	mLightRotationAngle += 0.1f * gt.DeltaTime();

	Matrix R = Matrix::CreateRotationY(mLightRotationAngle);
	for (int i = 0; i < 3; ++i)
	{
		auto lightDir = mBaseLightDirections[i];
		lightDir = Vector3::TransformNormal(lightDir, R);
		mRotatedLightDirections[i] = lightDir;
	}


	AnimatedMaterial(gt);
	UpdateGameObjects(gt);
	UpdateMaterial(gt);
	UpdateShadowTransform(gt);
	UpdateGlobalCB(gt);
	UpdateShadowPassCB(gt);
}

void ShapesApp::UpdateMaterial(const GameTimer& gt)
{
	auto currentMaterialBuffer = currentFrameResource->MaterialBuffer.get();

	for (auto&& material : materials)
	{
		material.second->Update();
		auto constantData = material.second->GetMaterialConstantData();
		currentMaterialBuffer->CopyData(material.second->GetIndex(), constantData);
	}
}

void ShapesApp::RenderUI()
{
	// Acquire our wrapped render target resource for the current back buffer.
	d3d11On12Device->AcquireWrappedResources(wrappedBackBuffers[currBackBufferIndex].GetAddressOf(), 1);

	// Render text directly to the back buffer.
	d2dContext->SetTarget(d2dRenderTargets[currBackBufferIndex].Get());
	d2dContext->BeginDraw();
	d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
	d2dContext->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
	d2dBrush->SetColor(D2D1::ColorF(D2D1::ColorF::WhiteSmoke));
	d2dContext->DrawTextW(fpsStr.c_str(), fpsStr.length(), d2dTextFormat.Get(), &fpsRect, d2dBrush.Get());
	ThrowIfFailed(d2dContext->EndDraw());

	// Release our wrapped render target resource. Releasing 
	// transitions the back buffer resource to the state specified
	// as the OutState when the wrapped resource was created.
	d3d11On12Device->ReleaseWrappedResources(wrappedBackBuffers[currBackBufferIndex].GetAddressOf(), 1);

	// Flush to submit the 11 command list to the shared command queue.
	d3d11DeviceContext->Flush();
}

void ShapesApp::DrawSceneToShadowMap()
{
	commandListDirect->RSSetViewports(1, &mShadowMap->Viewport());
	commandListDirect->RSSetScissorRects(1, &mShadowMap->ScissorRect());

	commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
	                                                                            D3D12_RESOURCE_STATE_GENERIC_READ,
	                                                                            D3D12_RESOURCE_STATE_DEPTH_WRITE));

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	commandListDirect->ClearDepthStencilView(mShadowMap->Dsv(),
	                                         D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	commandListDirect->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());

	// Bind the pass constant buffer for the shadow map pass.
	auto passCB = currentFrameResource->PassConstantBuffer->Resource();
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;
	commandListDirect->SetGraphicsRootConstantBufferView(StandardShaderSlot::CameraData, passCBAddress);

	commandListDirect->SetPipelineState(psos[PsoType::Shadow]->GetPSO().Get());

	DrawGameObjects(commandListDirect.Get(), typedGameObjects[PsoType::Opaque]);

	commandListDirect->SetPipelineState(psos[PsoType::AlphaDrop]->GetPSO().Get());
	DrawGameObjects(commandListDirect.Get(), typedGameObjects[PsoType::AlphaDrop]);


	commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
	                                                                            D3D12_RESOURCE_STATE_DEPTH_WRITE,
	                                                                            D3D12_RESOURCE_STATE_GENERIC_READ));
}

void ShapesApp::Draw(const GameTimer& gt)
{
	if (isResizing) return;

	PIXBeginEvent(commandQueueDirect.Get(), 0, L"Render 3D");

	auto frameAlloc = currentFrameResource->commandListAllocator;


	ThrowIfFailed(frameAlloc->Reset());


	ThrowIfFailed(commandListDirect->Reset(frameAlloc.Get(), psos[PsoType::SkyBox]->GetPSO().Get()));

	ID3D12DescriptorHeap* descriptorHeaps[] = {textureSRVHeap.Get()};
	commandListDirect->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	commandListDirect->SetGraphicsRootSignature(rootSignature->GetRootSignature().Get());


	auto matBuffer = currentFrameResource->MaterialBuffer->Resource();
	commandListDirect->SetGraphicsRootShaderResourceView(StandardShaderSlot::MaterialData,
	                                                     matBuffer->GetGPUVirtualAddress());
	

	/*Подключаем все текстуры*/
	commandListDirect->SetGraphicsRootDescriptorTable(StandardShaderSlot::DiffuseTexture,textureSRVHeap->GetGPUDescriptorHandleForHeapStart());


	DrawSceneToShadowMap();

	commandListDirect->SetGraphicsRootDescriptorTable(StandardShaderSlot::ShadowMap, mShadowMap->Srv());

	commandListDirect->RSSetViewports(1, &screenViewport);
	commandListDirect->RSSetScissorRects(1, &scissorRect);

	commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
	                                                                            D3D12_RESOURCE_STATE_PRESENT,
	                                                                            D3D12_RESOURCE_STATE_RENDER_TARGET));

	commandListDirect->ClearRenderTargetView(GetCurrentBackBufferView(), Colors::BlanchedAlmond, 0, nullptr);
	commandListDirect->ClearDepthStencilView(GetDepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
	                                         1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	commandListDirect->OMSetRenderTargets(1, &GetCurrentBackBufferView(), true, &GetDepthStencilView());


	auto passCB = currentFrameResource->PassConstantBuffer->Resource();
	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	commandListDirect->
		SetGraphicsRootConstantBufferView(StandardShaderSlot::CameraData, passCB->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE shadowMapTexDesriptor(textureSRVHeap->GetGPUDescriptorHandleForHeapStart());
	shadowMapTexDesriptor.Offset(mShadowMapHeapIndex, cbvSrvUavDescriptorSize);
	commandListDirect->SetGraphicsRootDescriptorTable(StandardShaderSlot::ShadowMap, shadowMapTexDesriptor);


	/*Даем отрисовать скайсферу с ее текстурными ресурсами*/
	commandListDirect->SetPipelineState(psos[PsoType::SkyBox]->GetPSO().Get());
	DrawGameObjects(commandListDirect.Get(), typedGameObjects[static_cast<int>(PsoType::SkyBox)]);

	commandListDirect->SetPipelineState(psos[PsoType::Opaque]->GetPSO().Get());
	DrawGameObjects(commandListDirect.Get(), typedGameObjects[static_cast<int>(PsoType::Opaque)]);

	commandListDirect->SetPipelineState(psos[PsoType::AlphaDrop]->GetPSO().Get());
	DrawGameObjects(commandListDirect.Get(), typedGameObjects[static_cast<int>(PsoType::AlphaDrop)]);

	commandListDirect->SetPipelineState(psos[PsoType::Transparent]->GetPSO().Get());
	DrawGameObjects(commandListDirect.Get(), typedGameObjects[static_cast<int>(PsoType::Transparent)]);


	/*Если рисуем UI то не нужно для текущего backBuffer переводить состояние
	 * потому что после вызова d3d11DeviceContext->Flush() он сам его переведет
	 */
	commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
	                                                                            D3D12_RESOURCE_STATE_RENDER_TARGET,
	                                                                            D3D12_RESOURCE_STATE_PRESENT));

	ExecuteCommandList();

	PIXEndEvent(commandQueueDirect.Get());

	/*PIXBeginEvent(commandQueueDirect.Get(), 0, L"Render UI");
	RenderUI();
	PIXEndEvent(commandQueueDirect.Get());*/

	ThrowIfFailed(swapChain->Present(0, 0));
	currBackBufferIndex = (currBackBufferIndex + 1) % swapChainBufferCount;

	currentFrameResource->FenceValue = ++currentFence;
	commandQueueDirect->Signal(fence.Get(), currentFence);
}

void ShapesApp::UpdateGameObjects(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	for (auto& e : gameObjects)
	{
		e->Update();
	}
}

void ShapesApp::UpdateGlobalCB(const GameTimer& gt)
{
	auto view = camera->GetViewMatrix();
	auto proj = camera->GetProjectionMatrix();

	auto viewProj = (view * proj);
	auto invView = view.Invert();
	auto invProj = proj.Invert();
	auto invViewProj = viewProj.Invert();
	auto shadowTransform = mShadowTransform;

	mainPassCB.View = view.Transpose();
	mainPassCB.InvView = invView.Transpose();
	mainPassCB.Proj = proj.Transpose();
	mainPassCB.InvProj = invProj.Transpose();
	mainPassCB.ViewProj = viewProj.Transpose();
	mainPassCB.InvViewProj = invViewProj.Transpose();
	mainPassCB.ShadowTransform = shadowTransform.Transpose();
	mainPassCB.EyePosW = camera->gameObject->GetTransform()->GetPosition();
	mainPassCB.RenderTargetSize = XMFLOAT2(static_cast<float>(clientWidth), static_cast<float>(clientHeight));
	mainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / clientWidth, 1.0f / clientHeight);
	mainPassCB.NearZ = 1.0f;
	mainPassCB.FarZ = 1000.0f;
	mainPassCB.TotalTime = gt.TotalTime();
	mainPassCB.DeltaTime = gt.DeltaTime();
	mainPassCB.AmbientLight = {0.25f, 0.25f, 0.35f, 1.0f};

	for (int i = 0; i < MaxLights; ++i)
	{
		if (i < lights.size())
		{
			mainPassCB.Lights[i] = lights[i]->GetData();
		}
		else
		{
			break;
		}
	}

	mainPassCB.Lights[0].Direction = mRotatedLightDirections[0];
	mainPassCB.Lights[0].Strength = {0.9f, 0.8f, 0.7f};
	mainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
	mainPassCB.Lights[1].Strength = {0.4f, 0.4f, 0.4f};
	mainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
	mainPassCB.Lights[2].Strength = {0.2f, 0.2f, 0.2f};


	auto currentPassCB = currentFrameResource->PassConstantBuffer.get();
	currentPassCB->CopyData(0, mainPassCB);

	//reflectedPassCB = mainPassCB;

	//XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
	//XMMATRIX R = XMMatrixReflect(mirrorPlane);


	//for (int i = 0; i < MaxLights; ++i)
	//{
	//	if (i < lights.size())
	//	{
	//		XMVECTOR lightDir = XMLoadFloat3(&mainPassCB.Lights[i].Direction);
	//		XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
	//		XMStoreFloat3(&reflectedPassCB.Lights[i].Direction, reflectedLightDir);
	//	}
	//	else { break; }
	//}


	//currentPassCB->CopyData(1, reflectedPassCB);
}

void ShapesApp::CreateRtvAndDsvDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = swapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(dxDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(renderTargetViewHeap.GetAddressOf())));


	//+1 для карты теней
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 2;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(dxDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(depthStencilViewHeap.GetAddressOf())));
}

void ShapesApp::LoadTextures()
{
	ThrowIfFailed(commandListDirect->Reset(directCommandListAlloc.Get(), nullptr));

	auto bricksTex = std::make_unique<Texture>("bricksTex", L"Data\\Textures\\bricks2.dds");
	bricksTex->LoadTexture(dxDevice.Get(), commandQueueDirect.Get(), commandListDirect.Get());
	textures[bricksTex->GetName()] = std::move(bricksTex);

	auto stoneTex = std::make_unique<Texture>("stoneTex", L"Data\\Textures\\stone.dds");
	stoneTex->LoadTexture(dxDevice.Get(), commandQueueDirect.Get(), commandListDirect.Get());
	textures[stoneTex->GetName()] = std::move(stoneTex);

	auto tileTex = std::make_unique<Texture>("tileTex", L"Data\\Textures\\tile.dds");
	tileTex->LoadTexture(dxDevice.Get(), commandQueueDirect.Get(), commandListDirect.Get());
	textures[tileTex->GetName()] = std::move(tileTex);

	auto fenceTex = std::make_unique<Texture>("fenceTex", L"Data\\Textures\\WireFence.dds");
	fenceTex->LoadTexture(dxDevice.Get(), commandQueueDirect.Get(), commandListDirect.Get());
	textures[fenceTex->GetName()] = std::move(fenceTex);

	auto waterTex = std::make_unique<Texture>("waterTex", L"Data\\Textures\\water1.dds");
	waterTex->LoadTexture(dxDevice.Get(), commandQueueDirect.Get(), commandListDirect.Get());
	textures[waterTex->GetName()] = std::move(waterTex);

	auto skyTex = std::make_unique<Texture>("skyTex", L"Data\\Textures\\skymap.dds");
	skyTex->LoadTexture(dxDevice.Get(), commandQueueDirect.Get(), commandListDirect.Get());
	textures[skyTex->GetName()] = std::move(skyTex);

	auto grassTex = std::make_unique<Texture>("grassTex", L"Data\\Textures\\grass.dds");
	grassTex->LoadTexture(dxDevice.Get(), commandQueueDirect.Get(), commandListDirect.Get());
	textures[grassTex->GetName()] = std::move(grassTex);

	auto treeArrayTex = std::make_unique<Texture>("treeArrayTex", L"Data\\Textures\\treeArray2.dds");
	treeArrayTex->LoadTexture(dxDevice.Get(), commandQueueDirect.Get(), commandListDirect.Get());
	textures[treeArrayTex->GetName()] = std::move(treeArrayTex);

	auto seamless = std::make_unique<Texture>("seamless", L"Data\\Textures\\seamless_grass.jpg");
	seamless->LoadTexture(dxDevice.Get(), commandQueueDirect.Get(), commandListDirect.Get());
	textures[seamless->GetName()] = std::move(seamless);


	std::vector<std::string> texNormalNames =
	{
		"bricksNormalMap",
		"tileNormalMap",
		"defaultNormalMap"
	};

	std::vector<std::wstring> texNormalFilenames =
	{
		L"Data\\Textures\\bricks2_nmap.dds",
		L"Data\\Textures\\tile_nmap.dds",
		L"Data\\Textures\\default_nmap.dds"
	};

	for (int i = 0; i < texNormalNames.size(); ++i)
	{
		auto texture = std::make_unique<Texture>(texNormalNames[i], texNormalFilenames[i], TextureUsage::Normalmap);
		texture->LoadTexture(dxDevice.Get(), commandQueueDirect.Get(), commandListDirect.Get());
		textures[texture->GetName()] = std::move(texture);
	}


	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = textures.size() + 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(dxDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&textureSRVHeap)));

	mShadowMapHeapIndex = textures.size();


	auto srvCpuStart = textureSRVHeap->GetCPUDescriptorHandleForHeapStart();
	auto srvGpuStart = textureSRVHeap->GetGPUDescriptorHandleForHeapStart();
	auto dsvCpuStart = depthStencilViewHeap->GetCPUDescriptorHandleForHeapStart();


	mShadowMap->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(srvCpuStart, mShadowMapHeapIndex, cbvSrvUavDescriptorSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(srvGpuStart, mShadowMapHeapIndex, cbvSrvUavDescriptorSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvCpuStart, 1, dsvDescriptorSize));


	ExecuteCommandList();
	FlushCommandQueue();
}

void ShapesApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO defines[] =
	{
		"FOG", "1",
		nullptr, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"FOG", "1",
		"ALPHA_TEST", "1",
		nullptr, NULL
	};

	shaders["StandardVertex"] = std::move(
		std::make_unique<Shader>(L"Shaders\\Default.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
	shaders["AlphaDrop"] = std::move(
		std::make_unique<Shader>(L"Shaders\\Default.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));
	shaders["shadowVS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\Shadows.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
	shaders["shadowOpaquePS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\Shadows.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));
	shaders["OpaquePixel"] = std::move(
		std::make_unique<Shader>(L"Shaders\\Default.hlsl", PixelShader, defines, "PS", "ps_5_1"));
	shaders["SkyBoxVertex"] = std::move(
		std::make_unique<Shader>(L"Shaders\\SkyBoxShader.hlsl", VertexShader, defines, "SKYMAP_VS", "vs_5_1"));
	shaders["SkyBoxPixel"] = std::move(
		std::make_unique<Shader>(L"Shaders\\SkyBoxShader.hlsl", PixelShader, defines, "SKYMAP_PS", "ps_5_1"));

	shaders["treeSpriteVS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\TreeSprite.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
	shaders["treeSpriteGS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\TreeSprite.hlsl", GeometryShader, nullptr, "GS", "gs_5_1"));
	shaders["treeSpritePS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\TreeSprite.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));


	for (auto&& pair : shaders)
	{
		pair.second->LoadAndCompile();
	}


	defaultInputLayout =
	{
		{
			"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			"TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
	};

	treeSpriteInputLayout =
	{
		{
			"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
		{
			"SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT,
			D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0
		},
	};


	rootSignature = std::make_unique<RootSignature>();

	CD3DX12_DESCRIPTOR_RANGE texParam[3];
	texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
	texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);
	texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, textures.size(), 2, 0);


	rootSignature->AddConstantBufferParameter(0);
	rootSignature->AddConstantBufferParameter(1);
	rootSignature->AddShaderResourceView(0, 1);
	rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_ALL);
	rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_ALL);
	rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_ALL);

	rootSignature->Initialize(dxDevice.Get());
}

void ShapesApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData skySphere = geoGen.CreateSkySphere(10, 10);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = static_cast<UINT>(box.Vertices.size());
	UINT sphereVertexOffset = gridVertexOffset + static_cast<UINT>(grid.Vertices.size());
	UINT cylinderVertexOffset = sphereVertexOffset + static_cast<UINT>(sphere.Vertices.size());
	UINT skySphererVertexOffset = cylinderVertexOffset + static_cast<UINT>(cylinder.Vertices.size());

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = static_cast<UINT>(box.Indices32.size());
	UINT sphereIndexOffset = gridIndexOffset + static_cast<UINT>(grid.Indices32.size());
	UINT cylinderIndexOffset = sphereIndexOffset + static_cast<UINT>(sphere.Indices32.size());
	UINT skySphererIndexOffset = sphereIndexOffset + static_cast<UINT>(cylinder.Indices32.size());

	SubmeshGeometry boxSubmeshs;
	boxSubmeshs.IndexCount = static_cast<UINT>(box.Indices32.size());
	boxSubmeshs.StartIndexLocation = boxIndexOffset;
	boxSubmeshs.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmeshs;
	gridSubmeshs.IndexCount = static_cast<UINT>(grid.Indices32.size());
	gridSubmeshs.StartIndexLocation = gridIndexOffset;
	gridSubmeshs.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmeshs;
	sphereSubmeshs.IndexCount = static_cast<UINT>(sphere.Indices32.size());
	sphereSubmeshs.StartIndexLocation = sphereIndexOffset;
	sphereSubmeshs.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmeshs;
	cylinderSubmeshs.IndexCount = static_cast<UINT>(cylinder.Indices32.size());
	cylinderSubmeshs.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmeshs.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry skySphererSubmeshs;
	skySphererSubmeshs.IndexCount = static_cast<UINT>(skySphere.Indices32.size());
	skySphererSubmeshs.StartIndexLocation = skySphererIndexOffset;
	skySphererSubmeshs.BaseVertexLocation = skySphererVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() + skySphere.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].position = box.Vertices[i].Position;
		vertices[k].normal = box.Vertices[i].Normal;
		vertices[k].texCord = box.Vertices[i].TexCord;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].position = grid.Vertices[i].Position;
		vertices[k].normal = grid.Vertices[i].Normal;
		vertices[k].texCord = grid.Vertices[i].TexCord;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].position = sphere.Vertices[i].Position;
		vertices[k].normal = sphere.Vertices[i].Normal;
		vertices[k].texCord = sphere.Vertices[i].TexCord;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].position = cylinder.Vertices[i].Position;
		vertices[k].normal = cylinder.Vertices[i].Normal;
		vertices[k].texCord = cylinder.Vertices[i].TexCord;
	}

	for (size_t i = 0; i < skySphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].position = skySphere.Vertices[i].Position;
		vertices[k].normal = skySphere.Vertices[i].Normal;
		vertices[k].texCord = skySphere.Vertices[i].TexCord;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(skySphere.GetIndices16()), std::end(skySphere.GetIndices16()));

	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeMesh";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
	                                                    commandListDirect.Get(), vertices.data(), vbByteSize,
	                                                    geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
	                                                   commandListDirect.Get(), indices.data(), ibByteSize,
	                                                   geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->Submeshs["box"] = boxSubmeshs;
	geo->Submeshs["grid"] = gridSubmeshs;
	geo->Submeshs["sphere"] = sphereSubmeshs;
	geo->Submeshs["cylinder"] = cylinderSubmeshs;
	geo->Submeshs["sky"] = skySphererSubmeshs;

	meshes[geo->Name] = std::move(geo);
}

void ShapesApp::BuildRoomGeometry()
{
	// Create and specify geometry.  For this sample we draw a floor
	// and a wall with a mirror on it.  We put the floor, wall, and
	// mirror geometry in one vertex buffer.
	//
	//   |--------------|
	//   |              |
	//   |----|----|----|
	//   |Wall|Mirr|Wall|
	//   |    | or |    |
	//   /--------------/
	//  /   Floor      /
	// /--------------/

	std::array<Vertex, 20> vertices =
	{
		// Floor: Observe we tile texture coordinates.
		Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f), // 0 
		Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
		Vertex(7.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
		Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f),

		// Wall: Observe we tile texture coordinates, and that we
		// leave a gap in the middle for the mirror.
		Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 4
		Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
		Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),

		Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 8 
		Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f),
		Vertex(7.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 2.0f),

		Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 12
		Vertex(-3.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 0.0f),
		Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 1.0f),

		// Mirror
		Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 16
		Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
		Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
		Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)
	};

	std::array<std::int16_t, 30> indices =
	{
		// Floor
		0, 1, 2,
		0, 2, 3,

		// Walls
		4, 5, 6,
		4, 6, 7,

		8, 9, 10,
		8, 10, 11,

		12, 13, 14,
		12, 14, 15,

		// Mirror
		16, 17, 18,
		16, 18, 19
	};

	SubmeshGeometry floorSubmesh;
	floorSubmesh.IndexCount = 6;
	floorSubmesh.StartIndexLocation = 0;
	floorSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry wallSubmesh;
	wallSubmesh.IndexCount = 18;
	wallSubmesh.StartIndexLocation = 6;
	wallSubmesh.BaseVertexLocation = 0;

	SubmeshGeometry mirrorSubmesh;
	mirrorSubmesh.IndexCount = 6;
	mirrorSubmesh.StartIndexLocation = 24;
	mirrorSubmesh.BaseVertexLocation = 0;

	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "roomGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
	                                                    commandListDirect.Get(), vertices.data(), vbByteSize,
	                                                    geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
	                                                   commandListDirect.Get(), indices.data(), ibByteSize,
	                                                   geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->Submeshs["floor"] = floorSubmesh;
	geo->Submeshs["wall"] = wallSubmesh;
	geo->Submeshs["mirror"] = mirrorSubmesh;

	meshes[geo->Name] = std::move(geo);
}

float GetHillsHeight(float x, float z)
{
	return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 GetHillsNormal(float x, float z)
{
	// n = (-df/dx, 1, -df/dz)
	XMFLOAT3 n(
		-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
		1.0f,
		-0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}

void ShapesApp::BuildLandGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);

	//
	// Extract the vertex elements we are interested and apply the height function to
	// each vertex.  In addition, color the vertices based on their height so we have
	// sandy looking beaches, grassy low hills, and snow mountain peaks.
	//

	std::vector<Vertex> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		auto& p = grid.Vertices[i].Position;
		vertices[i].position = p;
		vertices[i].position.y = GetHillsHeight(p.x, p.z);
		vertices[i].normal = GetHillsNormal(p.x, p.z);
		vertices[i].texCord = grid.Vertices[i].TexCord;
	}

	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
	                                                    commandListDirect.Get(), vertices.data(), vbByteSize,
	                                                    geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
	                                                   commandListDirect.Get(), indices.data(), ibByteSize,
	                                                   geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->Submeshs["grid"] = submesh;

	meshes["landGeo"] = std::move(geo);
}

void ShapesApp::BuildTreesGeometry()
{
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	static const int treeCount = 16;
	std::vector<TreeSpriteVertex> vertices;
	std::vector<uint16_t> indices;

	for (UINT i = 0; i < treeCount; ++i)
	{
		float x = MathHelper::RandF(-45.0f, 45.0f);
		float z = MathHelper::RandF(-45.0f, 45.0f);
		float y = 0;

		TreeSpriteVertex vertex{XMFLOAT3(x, y, z), XMFLOAT2(20.0f, 20.0f)};

		vertices.push_back(vertex);

		indices.push_back(i);
	}


	const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
	                                                    commandListDirect.Get(), vertices.data(), vbByteSize,
	                                                    geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
	                                                   commandListDirect.Get(), indices.data(), ibByteSize,
	                                                   geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = static_cast<UINT>(indices.size());
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->Submeshs["points"] = submesh;

	meshes["treeSpritesGeo"] = std::move(geo);
}

void ShapesApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>("bricks", psos[PsoType::Opaque].get());
	bricks0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	bricks0->Roughness = 0.3f;
	bricks0->SetDiffuseTexture(textures["bricksTex"].get());
	bricks0->SetNormalMap(textures["bricksNormalMap"].get());
	materials[bricks0->GetName()] = std::move(bricks0);

	auto seamless = std::make_unique<Material>("seamless", psos[PsoType::Opaque].get());
	seamless->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	seamless->Roughness = 0.1f;
	seamless->SetDiffuseTexture(textures["seamless"].get());
	seamless->SetNormalMap(textures["defaultNormalMap"].get());
	materials[seamless->GetName()] = std::move(seamless);


	auto stone0 = std::make_unique<Material>("stone0", psos[PsoType::Opaque].get());
	stone0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	stone0->Roughness = 0.1f;
	stone0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
	stone0->SetDiffuseTexture(textures["stoneTex"].get());
	stone0->SetNormalMap(textures["defaultNormalMap"].get());
	materials[stone0->GetName()] = std::move(stone0);

	auto tile0 = std::make_unique<Material>("tile0", psos[PsoType::Opaque].get());
	tile0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
	tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
	tile0->Roughness = 0.1f;
	tile0->SetDiffuseTexture(textures["tileTex"].get());
	tile0->SetNormalMap(textures["tileNormalMap"].get());
	materials[tile0->GetName()] = std::move(tile0);

	auto wirefence = std::make_unique<Material>("wirefence", psos[PsoType::AlphaDrop].get());
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	wirefence->Roughness = 0.25f;
	wirefence->SetDiffuseTexture(textures["fenceTex"].get());
	wirefence->SetNormalMap(textures["defaultNormalMap"].get());
	materials[wirefence->GetName()] = std::move(wirefence);

	auto water = std::make_unique<Material>("water", psos[PsoType::Transparent].get());
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;
	water->SetDiffuseTexture(textures["waterTex"].get());
	water->SetNormalMap(textures["defaultNormalMap"].get());
	materials[water->GetName()] = std::move(water);

	auto icemirror = std::make_unique<Material>("mirror", psos[PsoType::Mirror].get());
	icemirror->SetDiffuseTexture(textures["waterTex"].get());
	icemirror->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
	icemirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	icemirror->Roughness = 0.5f;
	icemirror->SetNormalMap(textures["defaultNormalMap"].get());
	materials[icemirror->GetName()] = std::move(icemirror);


	auto shadowMat = std::make_unique<Material>("shadow", psos[PsoType::Shadow].get());
	shadowMat->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);
	shadowMat->SetDiffuseTexture(textures["bricksTex"].get());
	shadowMat->FresnelR0 = XMFLOAT3(0.001f, 0.001f, 0.001f);
	shadowMat->Roughness = 0.0f;
	shadowMat->SetNormalMap(textures["defaultNormalMap"].get());
	materials[shadowMat->GetName()] = std::move(shadowMat);


	auto skyBox = std::make_unique<Material>("sky", psos[PsoType::SkyBox].get());
	skyBox->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skyBox->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	skyBox->Roughness = 1.0f;
	skyBox->SetDiffuseTexture(textures["skyTex"].get());
	skyBox->SetNormalMap(textures["defaultNormalMap"].get());
	materials[skyBox->GetName()] = std::move(skyBox);


	auto treeSprites = std::make_unique<Material>("treeSprites", psos[PsoType::AlphaSprites].get());
	treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	treeSprites->Roughness = 0.125f;
	treeSprites->SetDiffuseTexture(textures["treeArrayTex"].get());
	treeSprites->SetNormalMap(textures["defaultNormalMap"].get());
	materials[treeSprites->GetName()] = std::move(treeSprites);

	for (auto&& pair : materials)
	{
		pair.second->InitMaterial(dxDevice.Get(), textureSRVHeap.Get());
	}
}

void ShapesApp::BuildGameObjects()
{
	auto camera = std::make_unique<GameObject>(dxDevice.Get(), "MainCamera");
	camera->GetTransform()->SetPosition({-3.667396, 3.027442, -12.024042});
	camera->GetTransform()->SetEulerRotate({-0.100110, -2.716100, 0.000000});
	camera->AddComponent(new Camera(AspectRatio()));
	camera->AddComponent(new CameraController());
	gameObjects.push_back(std::move(camera));

	auto skySphere = std::make_unique<GameObject>(dxDevice.Get(), "Sky");
	skySphere->GetTransform()->SetScale({500, 500, 500});
	auto renderer = new Renderer();
	renderer->Material = materials["sky"].get();
	renderer->Mesh = meshes["shapeMesh"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["sphere"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["sphere"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["sphere"].BaseVertexLocation;
	skySphere->AddComponent(renderer);
	typedGameObjects[PsoType::SkyBox].push_back(skySphere.get());
	gameObjects.push_back(std::move(skySphere));

	/*auto treeSpritesRitem = std::make_unique<GameObject>(dxDevice.Get(), "Trees");
	renderer = new Renderer();
	renderer->Material = materials["treeSprites"].get();
	renderer->Mesh = meshes["treeSpritesGeo"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["points"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["points"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["points"].BaseVertexLocation;
	treeSpritesRitem->AddComponent(renderer);
	typedGameObjects[(int)PsoType::AlphaSprites].push_back(treeSpritesRitem.get());
	gameObjects.push_back(std::move(treeSpritesRitem));*/

	auto sun1 = std::make_unique<GameObject>(dxDevice.Get(), "Directional Light");
	auto light = new Light(Directional);
	light->Direction({0.57735f, -0.57735f, 0.57735f});
	light->Strength({0.8f, 0.8f, 0.8f});
	sun1->AddComponent(light);
	gameObjects.push_back(std::move(sun1));


	auto sphere = std::make_unique<GameObject>(dxDevice.Get(), "Skull");
	sphere->GetTransform()->SetPosition(Vector3{0, 1, -3} + Vector3::Backward);
	sphere->GetTransform()->SetScale({2, 2, 2});
	renderer = new Renderer();
	renderer->Material = materials["bricks"].get();
	renderer->Mesh = meshes["shapeMesh"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["sphere"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["sphere"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["sphere"].BaseVertexLocation;
	sphere->AddComponent(renderer);
	sphere->AddComponent(new ObjectMover());
	player = sphere.get();
	typedGameObjects[static_cast<int>(PsoType::Opaque)].push_back(sphere.get());
	gameObjects.push_back(std::move(sphere));


	auto sun2 = std::make_unique<GameObject>(dxDevice.Get());
	light = new Light();
	light->Direction({-0.57735f, -0.57735f, 0.57735f});
	light->Strength({0.4f, 0.4f, 0.4f});
	sun2->AddComponent(light);
	gameObjects.push_back(std::move(sun2));

	auto sun3 = std::make_unique<GameObject>(dxDevice.Get());
	light = new Light();
	light->Direction({0.0f, -0.707f, -0.707f});
	light->Strength({0.2f, 0.2f, 0.2f});
	sun3->AddComponent(light);
	gameObjects.push_back(std::move(sun3));


	auto man = std::make_unique<GameObject>(dxDevice.Get());
	man->GetTransform()->SetPosition(Vector3::Forward * 12);
	XMStoreFloat4x4(&man->GetTransform()->TextureTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	auto modelRenderer = new ModelRenderer();
	modelRenderer->Material = materials["seamless"].get();
	modelRenderer->AddModel(dxDevice.Get(), commandListDirect.Get(), "Data\\Objects\\Nanosuit\\Nanosuit.obj");
	man->AddComponent(modelRenderer);
	typedGameObjects[PsoType::Opaque].push_back(man.get());
	gameObjects.push_back(std::move(man));


	auto box = std::make_unique<GameObject>(dxDevice.Get());
	box->GetTransform()->SetScale(Vector3(5.0f, 5.0f, 5.0f));
	box->GetTransform()->SetPosition(Vector3(0.0f, 0.25f, 0.0f) + (Vector3::Forward * 2.4));
	XMStoreFloat4x4(&box->GetTransform()->TextureTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	renderer = new Renderer();
	renderer->Material = materials["water"].get();
	renderer->Mesh = meshes["shapeMesh"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["box"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["box"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["box"].BaseVertexLocation;
	box->AddComponent(renderer);
	typedGameObjects[PsoType::Transparent].push_back(box.get());
	gameObjects.push_back(std::move(box));


	auto grid = std::make_unique<GameObject>(dxDevice.Get());
	renderer = new Renderer();
	renderer->Material = materials["tile0"].get();
	renderer->Mesh = meshes["shapeMesh"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["grid"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["grid"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["grid"].BaseVertexLocation;
	XMStoreFloat4x4(&renderer->Material->MatTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	grid->AddComponent(renderer);
	typedGameObjects[PsoType::Opaque].push_back(grid.get());
	gameObjects.push_back(std::move(grid));

	const XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	for (int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<GameObject>(dxDevice.Get());
		auto rightCylRitem = std::make_unique<GameObject>(dxDevice.Get());
		auto leftSphereRitem = std::make_unique<GameObject>(dxDevice.Get());
		auto rightSphereRitem = std::make_unique<GameObject>(dxDevice.Get());

		leftCylRitem->GetTransform()->SetPosition(Vector3(+5.0f, 1.5f, -10.0f + i * 5.0f));
		XMStoreFloat4x4(&leftCylRitem->GetTransform()->TextureTransform, brickTexTransform);
		renderer = new Renderer();
		renderer->Material = materials["bricks"].get();
		renderer->Mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->Mesh->Submeshs["cylinder"].IndexCount;
		renderer->StartIndexLocation = renderer->Mesh->Submeshs["cylinder"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->Mesh->Submeshs["cylinder"].BaseVertexLocation;
		leftCylRitem->AddComponent(renderer);

		rightCylRitem->GetTransform()->SetPosition(Vector3(-5.0f, 1.5f, -10.0f + i * 5.0f));
		XMStoreFloat4x4(&rightCylRitem->GetTransform()->TextureTransform, brickTexTransform);
		renderer = new Renderer();
		renderer->Material = materials["bricks"].get();
		renderer->Mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->Mesh->Submeshs["cylinder"].IndexCount;
		renderer->StartIndexLocation = renderer->Mesh->Submeshs["cylinder"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->Mesh->Submeshs["cylinder"].BaseVertexLocation;
		rightCylRitem->AddComponent(renderer);


		leftSphereRitem->GetTransform()->SetPosition(Vector3(-5.0f, 3.5f, -10.0f + i * 5.0f));
		leftSphereRitem->GetTransform()->TextureTransform = MathHelper::Identity4x4();
		renderer = new Renderer();
		renderer->Material = materials["wirefence"].get();
		renderer->Mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->Mesh->Submeshs["sphere"].IndexCount;
		renderer->StartIndexLocation = renderer->Mesh->Submeshs["sphere"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->Mesh->Submeshs["sphere"].BaseVertexLocation;
		leftSphereRitem->AddComponent(renderer);


		rightSphereRitem->GetTransform()->SetPosition(Vector3(+5.0f, 3.5f, -10.0f + i * 5.0f));
		rightSphereRitem->GetTransform()->TextureTransform = MathHelper::Identity4x4();
		renderer = new Renderer();
		renderer->Material = materials["wirefence"].get();
		renderer->Mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->Mesh->Submeshs["sphere"].IndexCount;
		renderer->StartIndexLocation = renderer->Mesh->Submeshs["sphere"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->Mesh->Submeshs["sphere"].BaseVertexLocation;
		rightSphereRitem->AddComponent(renderer);

		typedGameObjects[PsoType::Opaque].push_back(leftCylRitem.get());
		typedGameObjects[PsoType::Opaque].push_back(rightCylRitem.get());
		typedGameObjects[PsoType::AlphaDrop].push_back(leftSphereRitem.get());
		typedGameObjects[PsoType::AlphaDrop].push_back(rightSphereRitem.get());

		gameObjects.push_back(std::move(leftCylRitem));
		gameObjects.push_back(std::move(rightCylRitem));
		gameObjects.push_back(std::move(leftSphereRitem));
		gameObjects.push_back(std::move(rightSphereRitem));
	}
}

void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < globalCountFrameResources; ++i)
	{
		frameResources.push_back(
			std::make_unique<FrameResource>(dxDevice.Get(), 3, gameObjects.size(), materials.size()));
	}
}

void ShapesApp::BuildPSOs()
{
	auto opaquePSO = std::make_unique<PSO>();
	opaquePSO->SetInputLayout({defaultInputLayout.data(), static_cast<UINT>(defaultInputLayout.size())});
	opaquePSO->SetRootSignature(rootSignature->GetRootSignature().Get());
	opaquePSO->SetShader(shaders["StandardVertex"].get());
	opaquePSO->SetShader(shaders["OpaquePixel"].get());
	opaquePSO->SetRTVFormat(0, GetSRGBFormat(backBufferFormat));
	opaquePSO->SetSampleCount(isM4xMsaa ? 4 : 1);
	opaquePSO->SetSampleQuality(isM4xMsaa ? (m4xMsaaQuality - 1) : 0);
	opaquePSO->SetDSVFormat(depthStencilFormat);

	auto alphaDropPso = std::make_unique<PSO>(PsoType::AlphaDrop);
	alphaDropPso->SetPsoDesc(opaquePSO->GetPsoDescription());
	D3D12_RASTERIZER_DESC alphaDropRaster = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	alphaDropRaster.CullMode = D3D12_CULL_MODE_NONE;
	alphaDropPso->SetRasterizationState(alphaDropRaster);
	alphaDropPso->SetShader(shaders["AlphaDrop"].get());


	auto skyBoxPSO = std::make_unique<PSO>(PsoType::SkyBox);
	skyBoxPSO->SetPsoDesc(opaquePSO->GetPsoDescription());
	D3D12_DEPTH_STENCIL_DESC skyBoxDSS = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	skyBoxDSS.DepthEnable = true;
	skyBoxDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	// Make sure the depth function is LESS_EQUAL and not just LESS.  
	// Otherwise, the normalized depth values at z = 1 (NDC) will 
	// fail the depth test if the depth buffer was cleared to 1.
	skyBoxDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyBoxPSO->SetDepthStencilState(skyBoxDSS);
	D3D12_RASTERIZER_DESC skyBoxRaster = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	skyBoxRaster.CullMode = D3D12_CULL_MODE_NONE;
	skyBoxPSO->SetRasterizationState(skyBoxRaster);
	skyBoxPSO->SetShader(shaders["SkyBoxVertex"].get());
	skyBoxPSO->SetShader(shaders["SkyBoxPixel"].get());


	auto transperentPSO = std::make_unique<PSO>(PsoType::Transparent);
	transperentPSO->SetPsoDesc(opaquePSO->GetPsoDescription());
	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	transperentPSO->SetRenderTargetBlendState(0, transparencyBlendDesc);


	auto mirrorPSO = std::make_unique<PSO>(PsoType::Mirror);

	CD3DX12_BLEND_DESC mirrorBlendState(D3D12_DEFAULT);
	mirrorBlendState.RenderTarget[0].RenderTargetWriteMask = 0;

	D3D12_DEPTH_STENCIL_DESC mirrorDSS = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	mirrorDSS.DepthEnable = true;
	mirrorDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	mirrorDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	mirrorDSS.StencilEnable = true;
	mirrorDSS.StencilReadMask = 0xff;
	mirrorDSS.StencilWriteMask = 0xff;
	mirrorDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	mirrorDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	mirrorDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
	mirrorDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

	mirrorPSO->SetPsoDesc(opaquePSO->GetPsoDescription());
	mirrorPSO->SetBlendState(mirrorBlendState);
	mirrorPSO->SetDepthStencilState(mirrorDSS);

	auto reflectionPSO = std::make_unique<PSO>(PsoType::Reflection);

	D3D12_DEPTH_STENCIL_DESC reflectionsDSS = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	reflectionsDSS.DepthEnable = true;
	reflectionsDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	reflectionsDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	reflectionsDSS.StencilEnable = true;
	reflectionsDSS.StencilReadMask = 0xff;
	reflectionsDSS.StencilWriteMask = 0xff;
	reflectionsDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	// We are not rendering backfacing polygons, so these settings do not matter.
	reflectionsDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
	reflectionsDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	auto reflectionRasterState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	reflectionRasterState.CullMode = D3D12_CULL_MODE_BACK;
	reflectionRasterState.FrontCounterClockwise = true;

	reflectionPSO->SetPsoDesc(opaquePSO->GetPsoDescription());
	reflectionPSO->SetDepthStencilState(reflectionsDSS);
	reflectionPSO->SetRasterizationState(reflectionRasterState);

	auto shadowPSO = std::make_unique<PSO>(PsoType::Shadow);

	// We are going to draw shadows with transparency, so base it off the transparency description.
	D3D12_DEPTH_STENCIL_DESC shadowDSS = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	shadowDSS.DepthEnable = true;
	shadowDSS.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	shadowDSS.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	shadowDSS.StencilEnable = true;
	shadowDSS.StencilReadMask = 0xff;
	shadowDSS.StencilWriteMask = 0xff;
	shadowDSS.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSS.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
	// We are not rendering backfacing polygons, so these settings do not matter.
	shadowDSS.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
	shadowDSS.BackFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
	shadowDSS.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

	shadowPSO->SetPsoDesc(transperentPSO->GetPsoDescription());
	shadowPSO->SetDepthStencilState(shadowDSS);


	auto treeSprite = std::make_unique<PSO>(PsoType::AlphaSprites);
	treeSprite->SetPsoDesc(opaquePSO->GetPsoDescription());
	treeSprite->SetShader(shaders["treeSpriteVS"].get());
	treeSprite->SetShader(shaders["treeSpriteGS"].get());
	treeSprite->SetShader(shaders["treeSpritePS"].get());
	treeSprite->SetInputLayout({treeSpriteInputLayout.data(), static_cast<UINT>(treeSpriteInputLayout.size())});
	treeSprite->SetPrimitiveType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
	auto treeSpriteRasterState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	reflectionRasterState.CullMode = D3D12_CULL_MODE_NONE;
	treeSprite->SetRasterizationState(treeSpriteRasterState);

	auto shadowMapPSO = std::make_unique<PSO>(PsoType::Shadow);
	shadowMapPSO->SetPsoDesc(opaquePSO->GetPsoDescription());
	auto shadowMapRasterState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	shadowMapRasterState.DepthBias = 100000;
	shadowMapRasterState.DepthBiasClamp = 0.0f;
	shadowMapRasterState.SlopeScaledDepthBias = 1.0f;
	shadowMapPSO->SetRasterizationState(shadowMapRasterState);
	shadowMapPSO->SetShader(shaders["shadowVS"].get());
	shadowMapPSO->SetShader(shaders["shadowOpaquePS"].get());
	shadowMapPSO->SetRTVFormat(0, DXGI_FORMAT_UNKNOWN);
	shadowMapPSO->SetRenderTargetsCount(0);


	psos[opaquePSO->GetType()] = std::move(opaquePSO);
	psos[transperentPSO->GetType()] = std::move(transperentPSO);
	psos[shadowPSO->GetType()] = std::move(shadowPSO);
	psos[mirrorPSO->GetType()] = std::move(mirrorPSO);
	psos[reflectionPSO->GetType()] = std::move(reflectionPSO);
	psos[alphaDropPso->GetType()] = std::move(alphaDropPso);
	psos[skyBoxPSO->GetType()] = std::move(skyBoxPSO);
	psos[treeSprite->GetType()] = std::move(treeSprite);
	psos[shadowMapPSO->GetType()] = std::move(shadowMapPSO);

	for (auto& pso : psos)
	{
		pso.second->Initialize(dxDevice.Get());
	}
}

void ShapesApp::DrawGameObjects(ID3D12GraphicsCommandList* cmdList, const std::vector<GameObject*>& ritems)
{
	// For each render item...
	for (auto& ri : ritems)
	{
		ri->Draw(cmdList);
	}
}

void ShapesApp::SortGO()
{
	for (auto&& item : gameObjects)
	{
		auto light = item->GetComponent<Light>();
		if (light != nullptr)
		{
			lights.push_back(light);
		}

		auto cam = item->GetComponent<Camera>();
		if (cam != nullptr)
		{
			camera = std::unique_ptr<Camera>(cam);
		}
	}

	std::sort(lights.begin(), lights.end(), [](Light const* a, Light const* b) { return a->Type() < b->Type(); });
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> ShapesApp::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressW
		0.0f, // mipLODBias
		8); // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressW
		0.0f, // mipLODBias
		8); // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC cubesTexSampler(
		6, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressW
		0.0f, // mipLODBias
		8, // maxAnisotropy
		D3D12_COMPARISON_FUNC_NEVER);


	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp, cubesTexSampler
	};
}
