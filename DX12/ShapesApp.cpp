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
	mSceneBounds.Radius = sqrtf(15 * 15.0f + 15.0f * 15.0f);
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

		/*“ќлько те что можно использовать как UAV*/
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


	/*ѕочему-то логика взаимодействи€ с константным буфером как дл€ отрисовки тут не работает*/
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

	ThrowIfFailed(commandListDirect->Reset(directCommandListAlloc.Get(), nullptr));
	
	
	mShadowMap = std::make_unique<ShadowMap>(
		dxDevice.Get(), 2048, 2048);

	mSsao = std::make_unique<Ssao>(
		dxDevice.Get(),
		commandListDirect.Get(),
		clientWidth, clientHeight);

	LoadTextures();
	
	ExecuteCommandList();
	FlushCommandQueue();
	
	GeneratedMipMap();


	ThrowIfFailed(commandListDirect->Reset(directCommandListAlloc.Get(), nullptr));
	BuildTexturesHeap();
	BuildShadersAndInputLayout();
	BuildRootSignature();
	BuildSsaoRootSignature();
	BuildShapeGeometry();
	BuildLandGeometry();
	BuildTreesGeometry();
	BuildRoomGeometry();
	BuildPSOs();
	BuildMaterials();
	BuildGameObjects();
	BuildFrameResources();
	SortGO();


	mSsao->SetPSOs(psos[PsoType::Ssao]->GetPSO().Get(), psos[PsoType::SsaoBlur]->GetPSO().Get());
	
	ExecuteCommandList();

	// Wait until initialization is complete.
	FlushCommandQueue();
	return true;
}

void ShapesApp::CreateRtvAndDsvDescriptorHeaps()
{
	// Add +1 for screen normal map, +2 for ambient maps.
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = swapChainBufferCount + 3;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	ThrowIfFailed(dxDevice->CreateDescriptorHeap(
		&rtvHeapDesc, IID_PPV_ARGS(renderTargetViewHeap.GetAddressOf())));


	//+1 дл€ карты теней
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1 + 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	ThrowIfFailed(dxDevice->CreateDescriptorHeap(
		&dsvHeapDesc, IID_PPV_ARGS(depthStencilViewHeap.GetAddressOf())));
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();

	if (camera != nullptr)
	{
		camera->SetAspectRatio(AspectRatio());
	}

	if (mSsao != nullptr)
	{
		mSsao->OnResize(clientWidth, clientHeight);

		// Resources changed, so need to rebuild descriptors.
		mSsao->RebuildDescriptors(depthStencilBuffer.Get());
	}
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
	UpdateMainPassCB(gt);
	UpdateShadowPassCB(gt);
	UpdateSsaoCB(gt);
}


void ShapesApp::Draw(const GameTimer& gt)
{
	if (isResizing) return;

	

	auto frameAlloc = currentFrameResource->commandListAllocator;


	ThrowIfFailed(frameAlloc->Reset());


	ThrowIfFailed(commandListDirect->Reset(frameAlloc.Get(), psos[PsoType::Opaque]->GetPSO().Get()));

	ID3D12DescriptorHeap* descriptorHeaps[] = { srvTextureHeap.Get() };
	commandListDirect->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	commandListDirect->SetGraphicsRootSignature(rootSignature->GetRootSignature().Get());

	PIXBeginEvent(commandQueueDirect.Get(), 0, L"Prepare Render 3D");
	/*Bind all materials*/
	auto matBuffer = currentFrameResource->MaterialBuffer->Resource();
	commandListDirect->SetGraphicsRootShaderResourceView(StandardShaderSlot::MaterialData, matBuffer->GetGPUVirtualAddress());

	/*Bind all Diffuse Textures*/
	commandListDirect->SetGraphicsRootDescriptorTable(StandardShaderSlot::TexturesMap, srvTextureHeap->GetGPUDescriptorHandleForHeapStart());
	PIXEndEvent(commandQueueDirect.Get());

	

	PIXBeginEvent(commandQueueDirect.Get(), 0, L"Render 3D");

	PIXBeginEvent(commandQueueDirect.Get(), 0, L"Shadow Map Pass");				
	DrawSceneToShadowMap();
	PIXEndEvent(commandQueueDirect.Get());

	PIXBeginEvent(commandQueueDirect.Get(), 0, L"Normal and Depth Pass");		
	DrawNormalsAndDepth();
	PIXEndEvent(commandQueueDirect.Get());

	PIXBeginEvent(commandQueueDirect.Get(), 0, L"Compute SSAO");
	if (computeSsao)
	{		
		commandListDirect->SetGraphicsRootSignature(ssaoRootSignature.Get());
		mSsao->ComputeSsao(commandListDirect.Get(), currentFrameResource, 3);		
	}
	else
	{
		mSsao->ClearAmbiantMap(commandListDirect.Get());
	}
	PIXEndEvent(commandQueueDirect.Get());

	PIXBeginEvent(commandQueueDirect.Get(), 0, L"Main Pass");
		
	commandListDirect->SetGraphicsRootSignature(rootSignature->GetRootSignature().Get());	
	
	commandListDirect->RSSetViewports(1, &screenViewport);
	commandListDirect->RSSetScissorRects(1, &scissorRect);

	commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT,
		D3D12_RESOURCE_STATE_RENDER_TARGET));

	commandListDirect->ClearRenderTargetView(GetCurrentBackBufferView(), Colors::BlanchedAlmond, 0, nullptr);

	/*commandListDirect->ClearDepthStencilView(GetDepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f, 0, 0, nullptr);*/


	commandListDirect->OMSetRenderTargets(1, &GetCurrentBackBufferView(), true, &GetDepthStencilView());


	auto passCB = currentFrameResource->PassConstantBuffer->Resource();

	commandListDirect->
		SetGraphicsRootConstantBufferView(StandardShaderSlot::CameraData, passCB->GetGPUVirtualAddress());

	commandListDirect->SetGraphicsRootDescriptorTable(StandardShaderSlot::ShadowMap, mShadowMap->Srv());
	commandListDirect->SetGraphicsRootDescriptorTable(StandardShaderSlot::SsaoMap, mSsao->AmbientMapSrv());

	/*Bind all Diffuse Textures*/
	commandListDirect->SetGraphicsRootDescriptorTable(StandardShaderSlot::TexturesMap, srvTextureHeap->GetGPUDescriptorHandleForHeapStart());
	

	commandListDirect->SetPipelineState(psos[PsoType::SkyBox]->GetPSO().Get());
	DrawGameObjects(commandListDirect.Get(), typedGameObjects[static_cast<int>(PsoType::SkyBox)]);

	commandListDirect->SetPipelineState(psos[PsoType::Opaque]->GetPSO().Get());
	DrawGameObjects(commandListDirect.Get(), typedGameObjects[static_cast<int>(PsoType::Opaque)]);

	commandListDirect->SetPipelineState(psos[PsoType::OpaqueAlphaDrop]->GetPSO().Get());
	DrawGameObjects(commandListDirect.Get(), typedGameObjects[static_cast<int>(PsoType::OpaqueAlphaDrop)]);

	commandListDirect->SetPipelineState(psos[PsoType::Transparent]->GetPSO().Get());
	DrawGameObjects(commandListDirect.Get(), typedGameObjects[static_cast<int>(PsoType::Transparent)]);

	if(ShowAmbiantMap)
	{
		commandListDirect->SetPipelineState(psos[PsoType::Debug]->GetPSO().Get());
		DrawGameObjects(commandListDirect.Get(), typedGameObjects[static_cast<int>(PsoType::Debug)]);
	}
	


	/*≈сли рисуем UI то не нужно дл€ текущего backBuffer переводить состо€ние
	 * потому что после вызова d3d11DeviceContext->Flush() он сам его переведет
	 */
	commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT));

	ExecuteCommandList();

	PIXEndEvent(commandQueueDirect.Get());
	PIXEndEvent(commandQueueDirect.Get());

	/*PIXBeginEvent(commandQueueDirect.Get(), 0, L"Render UI");
	RenderUI();
	PIXEndEvent(commandQueueDirect.Get());*/

	ThrowIfFailed(swapChain->Present(0, 0));
	currBackBufferIndex = (currBackBufferIndex + 1) % swapChainBufferCount;

	currentFrameResource->FenceValue = ++currentFence;
	commandQueueDirect->Signal(fence.Get(), currentFence);
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


void ShapesApp::UpdateGameObjects(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	for (auto& e : gameObjects)
	{
		e->Update();
	}
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

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
	auto view = camera->GetViewMatrix();
	auto proj = camera->GetProjectionMatrix();

	auto viewProj = (view * proj);
	auto invView = view.Invert();
	auto invProj = proj.Invert();
	auto invViewProj = viewProj.Invert();
	auto shadowTransform = mShadowTransform;

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);
	Matrix viewProjTex = XMMatrixMultiply(viewProj, T);

	mainPassCB.View = view.Transpose();
	mainPassCB.InvView = invView.Transpose();
	mainPassCB.Proj = proj.Transpose();
	mainPassCB.InvProj = invProj.Transpose();
	mainPassCB.ViewProj = viewProj.Transpose();
	mainPassCB.InvViewProj = invViewProj.Transpose();
	mainPassCB.ViewProjTex = viewProjTex.Transpose();
	mainPassCB.ShadowTransform = shadowTransform.Transpose();
	mainPassCB.EyePosW = camera->gameObject->GetTransform()->GetPosition();
	mainPassCB.RenderTargetSize = XMFLOAT2(static_cast<float>(clientWidth), static_cast<float>(clientHeight));
	mainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / clientWidth, 1.0f / clientHeight);
	mainPassCB.NearZ = 1.0f;
	mainPassCB.FarZ = 1000.0f;
	mainPassCB.TotalTime = gt.TotalTime();
	mainPassCB.DeltaTime = gt.DeltaTime();
	mainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

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
	mainPassCB.Lights[0].Strength = { 0.9f, 0.8f, 0.7f };
	mainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
	mainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
	mainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };


	auto currentPassCB = currentFrameResource->PassConstantBuffer.get();
	currentPassCB->CopyData(0, mainPassCB);

}

void ShapesApp::UpdateSsaoCB(const GameTimer& gt)
{
	SsaoConstants ssaoCB;

	auto P = camera->GetProjectionMatrix();

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	ssaoCB.Proj = mainPassCB.Proj;
	ssaoCB.InvProj = mainPassCB.InvProj;
	XMStoreFloat4x4(&ssaoCB.ProjTex, XMMatrixTranspose(P * T));

	mSsao->GetOffsetVectors(ssaoCB.OffsetVectors);

	auto blurWeights = mSsao->CalcGaussWeights(2.5f);
	ssaoCB.BlurWeights[0] = XMFLOAT4(&blurWeights[0]);
	ssaoCB.BlurWeights[1] = XMFLOAT4(&blurWeights[4]);
	ssaoCB.BlurWeights[2] = XMFLOAT4(&blurWeights[8]);

	ssaoCB.InvRenderTargetSize = XMFLOAT2(1.0f / mSsao->SsaoMapWidth(), 1.0f / mSsao->SsaoMapHeight());

	// Coordinates given in view space.
	ssaoCB.OcclusionRadius = 0.5f;
	ssaoCB.OcclusionFadeStart = 0.2f;
	ssaoCB.OcclusionFadeEnd = 1.0f;
	ssaoCB.SurfaceEpsilon = 0.05f;

	auto currSsaoCB = currentFrameResource->SsaoConstantBuffer.get();
	currSsaoCB->CopyData(0, ssaoCB);
}

void ShapesApp::LoadTextures()
{
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
}

void ShapesApp::BuildRootSignature()
{
	rootSignature = std::make_unique<RootSignature>();

	CD3DX12_DESCRIPTOR_RANGE texParam[4];
	texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0); //SkyMap
	texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0); //ShadowMap
	texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0); //SsaoMap
	texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, textures.size(), 3, 0);


	rootSignature->AddConstantBufferParameter(0);
	rootSignature->AddConstantBufferParameter(1);
	rootSignature->AddShaderResourceView(0, 1);
	rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_ALL);
	rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_ALL);
	rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_ALL);
	rootSignature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_ALL);

	rootSignature->Initialize(dxDevice.Get());
}

void ShapesApp::BuildSsaoRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable0;
	texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

	CD3DX12_DESCRIPTOR_RANGE texTable1;
	texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

	// Root parameter can be a table, root descriptor or root constants.
	CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsConstantBufferView(0);
	slotRootParameter[1].InitAsConstants(1, 1);
	slotRootParameter[2].InitAsDescriptorTable(1, &texTable0, D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable1, D3D12_SHADER_VISIBILITY_PIXEL);

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC depthMapSam(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_BORDER, // addressW
		0.0f,
		0,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	std::array<CD3DX12_STATIC_SAMPLER_DESC, 4> staticSamplers =
	{
		pointClamp, linearClamp, depthMapSam, linearWrap
	};

	// A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		static_cast<UINT>(staticSamplers.size()), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	// create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		OutputDebugStringA(static_cast<char*>(errorBlob->GetBufferPointer()));
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(dxDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(ssaoRootSignature.GetAddressOf())));
}


void ShapesApp::BuildTexturesHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = textures.size() + 1 + 5; 
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(dxDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&srvTextureHeap)));

	mShadowMapHeapIndex = textures.size();
	mSsaoHeapIndexStart = mShadowMapHeapIndex + 1;
	mSsaoAmbientMapIndex = mSsaoHeapIndexStart + 3;


	mShadowMap->BuildDescriptors(
		GetCpuSrv(mShadowMapHeapIndex),
		GetGpuSrv(mShadowMapHeapIndex),
		GetDsv(1));

	mSsao->BuildDescriptors(
		depthStencilBuffer.Get(),
		GetCpuSrv(mSsaoHeapIndexStart),
		GetGpuSrv(mSsaoHeapIndexStart),
		GetRtv(swapChainBufferCount),
		cbvSrvUavDescriptorSize,
		rtvDescriptorSize);
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
	shaders["shadowOpaqueDropPS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\Shadows.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));
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


	shaders["drawNormalsVS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\DrawNormals.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
	shaders["drawNormalsPS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\DrawNormals.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));
;
	shaders["drawNormalsAlphaDropPS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\DrawNormals.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));

	
	shaders["ssaoVS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\Ssao.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
	shaders["ssaoPS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\Ssao.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

	shaders["ssaoBlurVS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\SsaoBlur.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
	shaders["ssaoBlurPS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\SsaoBlur.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

	shaders["ssaoDebugVS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\SsaoDebug.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
	shaders["ssaoDebugPS"] = std::move(
		std::make_unique<Shader>(L"Shaders\\SsaoDebug.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

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


	
}


void ShapesApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData skySphere = geoGen.CreateSkySphere(10, 10);
	GeometryGenerator::MeshData quad = geoGen.CreateQuad(0.0f, 0.0f, 1.0f, 1.0f, 0.0f);

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
	UINT quadVertexOffset = skySphererVertexOffset + static_cast<UINT>(skySphere.Vertices.size());

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = static_cast<UINT>(box.Indices32.size());
	UINT sphereIndexOffset = gridIndexOffset + static_cast<UINT>(grid.Indices32.size());
	UINT cylinderIndexOffset = sphereIndexOffset + static_cast<UINT>(sphere.Indices32.size());
	UINT skySphererIndexOffset = cylinderIndexOffset + static_cast<UINT>(cylinder.Indices32.size());
	UINT quadIndexOffset = skySphererIndexOffset + static_cast<UINT>(skySphere.Indices32.size());

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

	SubmeshGeometry quadSubmesh;
	quadSubmesh.IndexCount = static_cast<UINT>(quad.Indices32.size());
	quadSubmesh.StartIndexLocation = quadIndexOffset;
	quadSubmesh.BaseVertexLocation = quadVertexOffset;
	
	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() + skySphere.Vertices.size() + quad.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k] = box.Vertices[i];
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k] = grid.Vertices[i];
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k] = sphere.Vertices[i];
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k] = cylinder.Vertices[i];
	}

	for (size_t i = 0; i < skySphere.Vertices.size(); ++i, ++k)
	{
		vertices[k] = skySphere.Vertices[i];
	}

	for (int i = 0; i < quad.Vertices.size(); ++i, ++k)
	{
		vertices[k] = quad.Vertices[i];
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(skySphere.GetIndices16()), std::end(skySphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(quad.GetIndices16()), std::end(quad.GetIndices16()));

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
	geo->Submeshs["quad"] = quadSubmesh;

	meshes[geo->Name] = std::move(geo);
}


void ShapesApp::BuildRoomGeometry()
{

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
		vertices[i] = grid.Vertices[i];
		vertices[i].Position.y = GetHillsHeight(vertices[i].Position.x, vertices[i].Position.z);
		vertices[i].Normal = GetHillsNormal(vertices[i].Position.x, vertices[i].Position.z);
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

		TreeSpriteVertex vertex{ XMFLOAT3(x, y, z), XMFLOAT2(20.0f, 20.0f) };

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

void ShapesApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;

	ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	basePsoDesc.InputLayout = { defaultInputLayout.data(), static_cast<UINT>(defaultInputLayout.size()) };
	basePsoDesc.pRootSignature = rootSignature->GetRootSignature().Get();
	basePsoDesc.VS = shaders["StandardVertex"]->GetShaderResource();
	basePsoDesc.PS = shaders["OpaquePixel"]->GetShaderResource();
	basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	basePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	basePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	basePsoDesc.SampleMask = UINT_MAX;
	basePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	basePsoDesc.NumRenderTargets = 1;
	basePsoDesc.RTVFormats[0] = GetSRGBFormat(backBufferFormat);
	basePsoDesc.SampleDesc.Count = isM4xMsaa ? 4 : 1;
	basePsoDesc.SampleDesc.Quality = isM4xMsaa ? (m4xMsaaQuality - 1) : 0;
	basePsoDesc.DSVFormat = depthStencilFormat;

	auto depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	auto rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	
	
	
	auto opaquePSO = std::make_unique<PSO>();
	opaquePSO->SetPsoDesc(basePsoDesc);
	depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	opaquePSO->SetDepthStencilState(depthStencilDesc);

	auto alphaDropPso = std::make_unique<PSO>(PsoType::OpaqueAlphaDrop);
	alphaDropPso->SetPsoDesc(opaquePSO->GetPsoDescription());
	alphaDropPso->SetShader(shaders["AlphaDrop"].get());
	rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;	
	alphaDropPso->SetRasterizationState(rasterizedDesc);

	
	auto shadowMapPSO = std::make_unique<PSO>(PsoType::ShadowMapOpaque);
	shadowMapPSO->SetPsoDesc(basePsoDesc);
	shadowMapPSO->SetShader(shaders["shadowVS"].get());
	shadowMapPSO->SetShader(shaders["shadowOpaquePS"].get());
	shadowMapPSO->SetRTVFormat(0, DXGI_FORMAT_UNKNOWN);
	shadowMapPSO->SetRenderTargetsCount(0);
	
	rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rasterizedDesc.DepthBias = 100000;
	rasterizedDesc.DepthBiasClamp = 0.0f;
	rasterizedDesc.SlopeScaledDepthBias = 1.0f;
	shadowMapPSO->SetRasterizationState(rasterizedDesc);

	
	auto shadowMapDropPSO = std::make_unique<PSO>(PsoType::ShadowMapOpaqueDrop);
	shadowMapDropPSO->SetPsoDesc(shadowMapPSO->GetPsoDescription());
	shadowMapDropPSO->SetShader(shaders["shadowOpaqueDropPS"].get());
	

	auto drawNormalsPso = std::make_unique<PSO>(PsoType::DrawNormalsOpaque);
	drawNormalsPso->SetPsoDesc(basePsoDesc);
	drawNormalsPso->SetShader(shaders["drawNormalsVS"].get());
	drawNormalsPso->SetShader(shaders["drawNormalsPS"].get());
	drawNormalsPso->SetRTVFormat(0, Ssao::NormalMapFormat);
	drawNormalsPso->SetSampleCount(1);
	drawNormalsPso->SetSampleQuality(0);
	drawNormalsPso->SetDSVFormat(depthStencilFormat);

	auto drawNormalsDropPso = std::make_unique<PSO>(PsoType::DrawNormalsOpaqueDrop);
	drawNormalsDropPso->SetPsoDesc(drawNormalsPso->GetPsoDescription());
	drawNormalsDropPso->SetShader(shaders["drawNormalsAlphaDropPS"].get());
	rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
	drawNormalsDropPso->SetRasterizationState(rasterizedDesc);

	auto ssaoPSO = std::make_unique<PSO>(PsoType::Ssao);
	ssaoPSO->SetPsoDesc(basePsoDesc);	
	ssaoPSO->SetInputLayout({ nullptr,0 });
	ssaoPSO->SetRootSignature(ssaoRootSignature.Get());
	ssaoPSO->SetShader(shaders["ssaoVS"].get());
	ssaoPSO->SetShader(shaders["ssaoPS"].get());
	ssaoPSO->SetRTVFormat(0, Ssao::AmbientMapFormat);
	ssaoPSO->SetSampleCount(1);
	ssaoPSO->SetSampleQuality(0);
	ssaoPSO->SetDSVFormat(DXGI_FORMAT_UNKNOWN);
	depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	depthStencilDesc.DepthEnable = false;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	ssaoPSO->SetDepthStencilState(depthStencilDesc);

	auto ssaoBlurPSO = std::make_unique<PSO>(PsoType::SsaoBlur);
	ssaoBlurPSO->SetPsoDesc(ssaoPSO->GetPsoDescription());
	ssaoBlurPSO->SetShader(shaders["ssaoBlurVS"].get());
	ssaoBlurPSO->SetShader(shaders["ssaoBlurPS"].get());

	auto skyBoxPSO = std::make_unique<PSO>(PsoType::SkyBox);
	skyBoxPSO->SetPsoDesc(basePsoDesc);
	skyBoxPSO->SetShader(shaders["SkyBoxVertex"].get());
	skyBoxPSO->SetShader(shaders["SkyBoxPixel"].get());
	
	depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	skyBoxPSO->SetDepthStencilState(depthStencilDesc);	
	rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
	skyBoxPSO->SetRasterizationState(rasterizedDesc);


	auto transperentPSO = std::make_unique<PSO>(PsoType::Transparent);
	transperentPSO->SetPsoDesc(basePsoDesc);
	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc = {};
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
	
	


	auto treeSprite = std::make_unique<PSO>(PsoType::AlphaSprites);
	treeSprite->SetPsoDesc(basePsoDesc);
	treeSprite->SetShader(shaders["treeSpriteVS"].get());
	treeSprite->SetShader(shaders["treeSpriteGS"].get());
	treeSprite->SetShader(shaders["treeSpritePS"].get());
	treeSprite->SetInputLayout({ treeSpriteInputLayout.data(), static_cast<UINT>(treeSpriteInputLayout.size()) });
	treeSprite->SetPrimitiveType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
	rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
	treeSprite->SetRasterizationState(rasterizedDesc);

	auto debugPso = std::make_unique<PSO>(PsoType::Debug);
	debugPso->SetPsoDesc(basePsoDesc);
	debugPso->SetShader(shaders["ssaoDebugVS"].get());
	debugPso->SetShader(shaders["ssaoDebugPS"].get());


	psos[opaquePSO->GetType()] = std::move(opaquePSO);
	psos[transperentPSO->GetType()] = std::move(transperentPSO);
	psos[alphaDropPso->GetType()] = std::move(alphaDropPso);
	psos[skyBoxPSO->GetType()] = std::move(skyBoxPSO);
	psos[treeSprite->GetType()] = std::move(treeSprite);
	psos[shadowMapPSO->GetType()] = std::move(shadowMapPSO);
	psos[shadowMapDropPSO->GetType()] = std::move(shadowMapDropPSO);
	psos[drawNormalsPso->GetType()] = std::move(drawNormalsPso);
	psos[drawNormalsDropPso->GetType()] = std::move(drawNormalsDropPso);
	psos[ssaoPSO->GetType()] = std::move(ssaoPSO);
	psos[ssaoBlurPSO->GetType()] = std::move(ssaoBlurPSO);
	psos[debugPso->GetType()] = std::move(debugPso);

	for (auto& pso : psos)
	{
		pso.second->Initialize(dxDevice.Get());
	}
}

void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < globalCountFrameResources; ++i)
	{
		frameResources.push_back(
			std::make_unique<FrameResource>(dxDevice.Get(), 2, gameObjects.size(), materials.size()));
	}
}

void ShapesApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>("bricks", psos[PsoType::Opaque].get());
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
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

	auto wirefence = std::make_unique<Material>("wirefence", psos[PsoType::OpaqueAlphaDrop].get());
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
		pair.second->InitMaterial(dxDevice.Get(), srvTextureHeap.Get());
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

	auto quadRitem = std::make_unique<GameObject>(dxDevice.Get(), "Quad");
	renderer = new Renderer();	
	renderer->Material = materials["bricks"].get();
	renderer->Mesh = meshes["shapeMesh"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["quad"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["quad"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["quad"].BaseVertexLocation;
	quadRitem->AddComponent(renderer);
	typedGameObjects[PsoType::Debug].push_back(quadRitem.get());
	gameObjects.push_back(std::move(quadRitem));

	
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
		typedGameObjects[PsoType::OpaqueAlphaDrop].push_back(leftSphereRitem.get());
		typedGameObjects[PsoType::OpaqueAlphaDrop].push_back(rightSphereRitem.get());

		gameObjects.push_back(std::move(leftCylRitem));
		gameObjects.push_back(std::move(rightCylRitem));
		gameObjects.push_back(std::move(leftSphereRitem));
		gameObjects.push_back(std::move(rightSphereRitem));
	}
}

void ShapesApp::DrawUI()
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

void ShapesApp::DrawGameObjects(ID3D12GraphicsCommandList* cmdList, const std::vector<GameObject*>& ritems)
{
	// For each render item...
	for (auto& ri : ritems)
	{
		ri->Draw(cmdList);
	}
}

void ShapesApp::DrawSceneToShadowMap()
{
	commandListDirect->RSSetViewports(1, &mShadowMap->Viewport());
	commandListDirect->RSSetScissorRects(1, &mShadowMap->ScissorRect());

	commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_STATE_DEPTH_WRITE));
		

	commandListDirect->ClearDepthStencilView(mShadowMap->Dsv(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	commandListDirect->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
	// Bind the pass constant buffer for the shadow map pass.
	auto passCB = currentFrameResource->PassConstantBuffer->Resource();
	D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;
	commandListDirect->SetGraphicsRootConstantBufferView(StandardShaderSlot::CameraData, passCBAddress);

	commandListDirect->SetPipelineState(psos[PsoType::ShadowMapOpaque]->GetPSO().Get());

	DrawGameObjects(commandListDirect.Get(), typedGameObjects[PsoType::Opaque]);

	commandListDirect->SetPipelineState(psos[PsoType::ShadowMapOpaqueDrop]->GetPSO().Get());
	DrawGameObjects(commandListDirect.Get(), typedGameObjects[PsoType::OpaqueAlphaDrop]);


	commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		D3D12_RESOURCE_STATE_GENERIC_READ));
}

void ShapesApp::DrawNormalsAndDepth()
{
	commandListDirect->RSSetViewports(1, &screenViewport);
	commandListDirect->RSSetScissorRects(1, &scissorRect);

	auto normalMap = mSsao->NormalMap();
	auto normalMapRtv = mSsao->NormalMapRtv();

	// Change to RENDER_TARGET.
	commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the screen normal map and depth buffer.
	float clearValue[] = { 0.0f, 0.0f, 1.0f, 0.0f };
	commandListDirect->ClearRenderTargetView(normalMapRtv, clearValue, 0, nullptr);
	commandListDirect->ClearDepthStencilView(GetDepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0,
		0, nullptr);

	// Specify the buffers we are going to render to.
	commandListDirect->OMSetRenderTargets(1, &normalMapRtv, true, &GetDepthStencilView());

	// Bind the constant buffer for this pass.
	auto passCB = currentFrameResource->PassConstantBuffer->Resource();
	commandListDirect->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	commandListDirect->SetPipelineState(psos[PsoType::DrawNormalsOpaque]->GetPSO().Get());

	DrawGameObjects(commandListDirect.Get(), typedGameObjects[PsoType::Opaque]);

	commandListDirect->SetPipelineState(psos[PsoType::DrawNormalsOpaqueDrop]->GetPSO().Get());
	DrawGameObjects(commandListDirect.Get(), typedGameObjects[PsoType::OpaqueAlphaDrop]);

	// Change back to GENERIC_READ so we can read the texture in a shader.
	commandListDirect->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_GENERIC_READ));
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

CD3DX12_CPU_DESCRIPTOR_HANDLE ShapesApp::GetCpuSrv(int index) const
{
	auto srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(srvTextureHeap->GetCPUDescriptorHandleForHeapStart());
	srv.Offset(index, cbvSrvUavDescriptorSize);
	return srv;
}

CD3DX12_GPU_DESCRIPTOR_HANDLE ShapesApp::GetGpuSrv(int index) const
{
	auto srv = CD3DX12_GPU_DESCRIPTOR_HANDLE(srvTextureHeap->GetGPUDescriptorHandleForHeapStart());
	srv.Offset(index, cbvSrvUavDescriptorSize);
	return srv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ShapesApp::GetDsv(int index) const
{
	auto dsv = CD3DX12_CPU_DESCRIPTOR_HANDLE(depthStencilViewHeap->GetCPUDescriptorHandleForHeapStart());
	dsv.Offset(index, dsvDescriptorSize);
	return dsv;
}

CD3DX12_CPU_DESCRIPTOR_HANDLE ShapesApp::GetRtv(int index) const
{
	auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(renderTargetViewHeap->GetCPUDescriptorHandleForHeapStart());
	rtv.Offset(index, rtvDescriptorSize);
	return rtv;
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
