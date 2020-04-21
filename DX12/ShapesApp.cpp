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

	ThrowIfFailed(commandList->Reset(directCommandListAlloc.Get(), nullptr));
	LoadTextures();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildMaterials();
	BuildRenderItems();	
	BuildFrameResources();
	BuildPSOs();
	SortGOByMaterial();
	
	ExecuteCommandList();

	// Wait until initialization is complete.
	FlushCommandQueue();

	return true;
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	const XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
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

	UpdateCamera(gt);
	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);	
}

void ShapesApp::Draw(const GameTimer& gt)
{	
	auto frameAlloc = currentFrameResource->commandListAllocator;

	
	ThrowIfFailed(frameAlloc->Reset());


	ThrowIfFailed(commandList->Reset(frameAlloc.Get(), nullptr));
	
	

	
	commandList->RSSetViewports(1, &screenViewport);
	commandList->RSSetScissorRects(1, &scissorRect);

	// Indicate a state transition on the resource usage.
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	commandList->ClearRenderTargetView(GetCurrentBackBufferView(), Colors::BlanchedAlmond, 0, nullptr);
	commandList->ClearDepthStencilView(GetDepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	commandList->OMSetRenderTargets(1, &GetCurrentBackBufferView(), true, &GetDepthStencilView());

	//commandList->SetGraphicsRootSignature(rootSignature.Get());

	commandList->SetGraphicsRootSignature(typedRenderItems.begin()->second[0]->GetRenderer()->Material->GetRootSignature());
		
	auto passCB = currentFrameResource->PassConstantBuffer->Resource();
	commandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());


	
	for (auto it = typedRenderItems.begin(); it != typedRenderItems.end(); ++it)
	{
		commandList->SetPipelineState(it->second[0]->GetRenderer()->Material->GetPSO());
		/*if (isWireframe)
		{
			commandList->SetPipelineState(psos[MaterialType::Wireframe].Get());
		}
		else
		{
			commandList->SetPipelineState(psos[it->first].Get());
		}*/
		
		
		DrawRenderItems(commandList.Get(), it->second);
	}	
	
	
	// Indicate a state transition on the resource usage.
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ExecuteCommandList();

	// Swap the back and front buffers
	ThrowIfFailed(swapChain->Present(0, 0));
	currBackBufferIndex = (currBackBufferIndex + 1) % swapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	currentFrameResource->FenceValue = ++currentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	commandQueue->Signal(fence.Get(), currentFence);
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mainWindow);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		const float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		const float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		// Update angles based on input to orbit camera around box.
		mTheta += dx;
		mPhi += dy;

		// Restrict the angle mPhi.
		mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
	}
	else if ((btnState & MK_RBUTTON) != 0)
	{
		// Make each pixel correspond to 0.2 unit in the scene.
		const float dx = 0.05f * static_cast<float>(x - mLastMousePos.x);
		const float dy = 0.05f * static_cast<float>(y - mLastMousePos.y);

		// Update the camera radius based on input.
		mRadius += dx - dy;

		// Restrict the radius.
		mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardKeyUp(WPARAM key)
{
	if(key == VK_F1)
	{
		isWireframe = !isWireframe;
	}

	const float dt = timer.DeltaTime();
	const float value = 10;

	if (key == (VK_LEFT))
		mSunTheta -= value * dt;

	if (key == (VK_RIGHT) )
		mSunTheta += value * dt;

	if (key == (VK_UP) )
		mSunPhi -= value  * dt;

	if (key == (VK_DOWN) )
		mSunPhi += value * dt;

	mSunPhi = MathHelper::Clamp(mSunPhi, 0.1f, XM_PIDIV2);
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
	mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
	mEyePos.y = mRadius * cosf(mPhi);

	// Build the view matrix.
	const XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	const XMVECTOR target = XMVectorZero();
	const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	const XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
	for (auto& e : renderItems)
	{
		e->Update();		
	}
}



void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
	const XMMATRIX view = XMLoadFloat4x4(&mView);
	const XMMATRIX proj = XMLoadFloat4x4(&mProj);

	const XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	const XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	const XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	const XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mainPassCB.EyePosW = mEyePos;
	mainPassCB.RenderTargetSize = XMFLOAT2((float)clientWidth, (float)clientHeight);
	mainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / clientWidth, 1.0f / clientHeight);
	mainPassCB.NearZ = 1.0f;
	mainPassCB.FarZ = 1000.0f;
	mainPassCB.TotalTime = gt.TotalTime();
	mainPassCB.DeltaTime = gt.DeltaTime();
	mainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

	const XMVECTOR lightDir = -MathHelper::SphericalToCartesian(1.0f, mSunTheta, mSunPhi);

	XMStoreFloat3(&mainPassCB.Lights[0].Direction, lightDir);
	mainPassCB.Lights[0].Strength = { 1.0f, 1.0f, 0.9f };
	
	mainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	mainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mainPassCB.Lights[2].Strength = { 0.15f, 0.15f, 0.15f };

	auto currentPassCB = currentFrameResource->PassConstantBuffer.get();
	currentPassCB->CopyData(0, mainPassCB);
}


void ShapesApp::LoadTextures()
{	
	auto bricksTex = std::make_unique<Texture>("bricksTex", L"Textures\\bricks.dds");
	bricksTex->LoadTexture(dxDevice.Get(), commandQueue.Get());
	auto stoneTex = std::make_unique<Texture>("stoneTex", L"Textures\\stone.dds");
	stoneTex->LoadTexture(dxDevice.Get(), commandQueue.Get());
	auto tileTex = std::make_unique<Texture>("tileTex", L"Textures\\tile.dds");
	tileTex->LoadTexture(dxDevice.Get(), commandQueue.Get());
	auto fenceTex = std::make_unique<Texture>("fenceTex", L"Textures\\WireFence.dds");
	fenceTex->LoadTexture(dxDevice.Get(), commandQueue.Get());
	auto waterTex = std::make_unique<Texture>("waterTex", L"Textures\\water1.dds");
	waterTex->LoadTexture(dxDevice.Get(), commandQueue.Get());

	
	textures[bricksTex->GetName()] = std::move(bricksTex);
	textures[stoneTex->GetName()] = std::move(stoneTex);
	textures[tileTex->GetName()] = std::move(tileTex);
	textures[fenceTex->GetName()] = std::move(fenceTex);
	textures[waterTex->GetName()] = std::move(waterTex);
}


void ShapesApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO defines[] =
	{
		"FOG", "1",
		NULL, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};
	
	shaders["StandardVertex"] = std::move(std::make_unique<Shader>(L"Shaders\\Default.hlsl", ShaderType::VertexShader , nullptr, "VS", "vs_5_1"));
	shaders["StandardAlphaDrop"] = std::move(std::make_unique<Shader>(L"Shaders\\Default.hlsl", ShaderType::PixelShader, alphaTestDefines, "PS", "ps_5_1"));
	shaders["StandardPixel"] = std::move(std::make_unique<Shader>(L"Shaders\\Default.hlsl", ShaderType::PixelShader, defines, "PS", "ps_5_1"));

	for (auto && pair : shaders)
	{
		pair.second->LoadAndCompile();
	}

	
	inputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void ShapesApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	SubmeshGeometry boxSubmeshs;
	boxSubmeshs.IndexCount = (UINT)box.Indices32.size();
	boxSubmeshs.StartIndexLocation = boxIndexOffset;
	boxSubmeshs.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmeshs;
	gridSubmeshs.IndexCount = (UINT)grid.Indices32.size();
	gridSubmeshs.StartIndexLocation = gridIndexOffset;
	gridSubmeshs.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmeshs;
	sphereSubmeshs.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmeshs.StartIndexLocation = sphereIndexOffset;
	sphereSubmeshs.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmeshs;
	cylinderSubmeshs.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmeshs.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmeshs.BaseVertexLocation = cylinderVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size();

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

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeMesh";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
		commandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
		commandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->Submeshs["box"] = boxSubmeshs;
	geo->Submeshs["grid"] = gridSubmeshs;
	geo->Submeshs["sphere"] = sphereSubmeshs;
	geo->Submeshs["cylinder"] = cylinderSubmeshs;

	meshes[geo->Name] = std::move(geo);
}

void ShapesApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>(dxDevice.Get(),"bricks0", MaterialType::Opaque , backBufferFormat, depthStencilFormat, inputLayout, isM4xMsaa, m4xMsaaQuality);
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;
	bricks0->SetDiffuseTexture(textures["bricksTex"].get());
	bricks0->AddShader(shaders["StandardVertex"].get());
	bricks0->AddShader(shaders["StandardPixel"].get());
	materials["bricks0"] = std::move(bricks0);

	
	auto stone0 = std::make_unique<Material>(dxDevice.Get(), "stone0", MaterialType::Opaque, backBufferFormat, depthStencilFormat, inputLayout, isM4xMsaa, m4xMsaaQuality);
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;
	stone0->SetDiffuseTexture(textures["stoneTex"].get());
	stone0->AddShader(shaders["StandardVertex"].get());
	stone0->AddShader(shaders["StandardPixel"].get());
	materials["stone0"] = std::move(stone0);
	
	auto tile0 = std::make_unique<Material>(dxDevice.Get(), "tile0", MaterialType::Opaque, backBufferFormat, depthStencilFormat, inputLayout, isM4xMsaa, m4xMsaaQuality);
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.3f;
	tile0->SetDiffuseTexture(textures["tileTex"].get());
	tile0->AddShader(shaders["StandardVertex"].get());
	tile0->AddShader(shaders["StandardPixel"].get());
	materials["tile0"] = std::move(tile0);

	auto wirefence = std::make_unique<Material>(dxDevice.Get(), "wirefence", MaterialType::AlphaDrop, backBufferFormat, depthStencilFormat, inputLayout, isM4xMsaa, m4xMsaaQuality);
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	wirefence->Roughness = 0.25f;
	wirefence->SetDiffuseTexture(textures["fenceTex"].get());
	wirefence->AddShader(shaders["StandardVertex"].get());
	wirefence->AddShader(shaders["StandardAlphaDrop"].get());
	materials["wirefence"] = std::move(wirefence);
	
	auto water = std::make_unique<Material>(dxDevice.Get(), "water", MaterialType::Transparent, backBufferFormat, depthStencilFormat, inputLayout, isM4xMsaa, m4xMsaaQuality);
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;
	water->SetDiffuseTexture(textures["waterTex"].get());
	water->AddShader(shaders["StandardVertex"].get());
	water->AddShader(shaders["StandardPixel"].get());
	materials[water->GetName()] = std::move(water);
}

void ShapesApp::BuildRenderItems()
{
	auto box = std::make_unique<RenderItem>(dxDevice.Get());
	box->GetTransform()->SetPosition(Vector3(0.0f, 0.25f, 0.0f));
	box->GetTransform()->SetScale(Vector3(5.0f, 5.0f, 5.0f));	
	XMStoreFloat4x4(&box->GetTransform()->TextureTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	auto renderer = new Renderer(dxDevice.Get(), shaderTextureViewDescriptorHeap.Get());
	renderer->Material = materials["water"].get();
	renderer->Mesh = meshes["shapeMesh"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["box"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["box"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["box"].BaseVertexLocation;
	box->AddComponent(renderer);
	renderItems.push_back(std::move(box));

	
	auto grid = std::make_unique<RenderItem>(dxDevice.Get());
	XMStoreFloat4x4(&grid->GetTransform()->TextureTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	renderer = new Renderer(dxDevice.Get(), shaderTextureViewDescriptorHeap.Get());
	renderer->Material = materials["tile0"].get();
	renderer->Mesh = meshes["shapeMesh"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["grid"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["grid"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["grid"].BaseVertexLocation;
	grid->AddComponent(renderer);
	renderItems.push_back(std::move(grid));

	const XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	for (int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>(dxDevice.Get());
		auto rightCylRitem = std::make_unique<RenderItem>(dxDevice.Get());
		auto leftSphereRitem = std::make_unique<RenderItem>(dxDevice.Get());
		auto rightSphereRitem = std::make_unique<RenderItem>(dxDevice.Get());

		const XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		const XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		const XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		const XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		
		leftCylRitem->GetTransform()->SetPosition(Vector3(+5.0f, 1.5f, -10.0f + i * 5.0f));
		XMStoreFloat4x4(&leftCylRitem->GetTransform()->TextureTransform, brickTexTransform);
		renderer = new Renderer(dxDevice.Get(), shaderTextureViewDescriptorHeap.Get());
		renderer->Material = materials["bricks0"].get();
		renderer->Mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->Mesh->Submeshs["cylinder"].IndexCount;
		renderer->StartIndexLocation = renderer->Mesh->Submeshs["cylinder"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->Mesh->Submeshs["cylinder"].BaseVertexLocation;
		leftCylRitem->AddComponent(renderer);
		
		rightCylRitem->GetTransform()->SetPosition(Vector3(-5.0f, 1.5f, -10.0f + i * 5.0f));
		XMStoreFloat4x4(&rightCylRitem->GetTransform()->TextureTransform, brickTexTransform);
		renderer = new Renderer(dxDevice.Get(), shaderTextureViewDescriptorHeap.Get());
		renderer->Material = materials["bricks0"].get();
		renderer->Mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->Mesh->Submeshs["cylinder"].IndexCount;
		renderer->StartIndexLocation = renderer->Mesh->Submeshs["cylinder"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->Mesh->Submeshs["cylinder"].BaseVertexLocation;
		rightCylRitem->AddComponent(renderer);

		
		leftSphereRitem->GetTransform()->SetPosition(Vector3(-5.0f, 3.5f, -10.0f + i * 5.0f));
		leftSphereRitem->GetTransform()->TextureTransform = MathHelper::Identity4x4();
		renderer = new Renderer(dxDevice.Get(), shaderTextureViewDescriptorHeap.Get());
		renderer->Material = materials["wirefence"].get();
		renderer->Mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->Mesh->Submeshs["sphere"].IndexCount;
		renderer->StartIndexLocation = renderer->Mesh->Submeshs["sphere"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->Mesh->Submeshs["sphere"].BaseVertexLocation;
		leftSphereRitem->AddComponent(renderer);

		
		rightSphereRitem->GetTransform()->SetPosition(Vector3(+5.0f, 3.5f, -10.0f + i * 5.0f));
		rightSphereRitem->GetTransform()->TextureTransform = MathHelper::Identity4x4();
		renderer = new Renderer(dxDevice.Get(), shaderTextureViewDescriptorHeap.Get());
		renderer->Material = materials["wirefence"].get();
		renderer->Mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->Mesh->Submeshs["sphere"].IndexCount;
		renderer->StartIndexLocation = renderer->Mesh->Submeshs["sphere"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->Mesh->Submeshs["sphere"].BaseVertexLocation;
		rightSphereRitem->AddComponent(renderer);

		renderItems.push_back(std::move(leftCylRitem));
		renderItems.push_back(std::move(rightCylRitem));
		renderItems.push_back(std::move(leftSphereRitem));
		renderItems.push_back(std::move(rightSphereRitem));
	}
}

void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < globalCountFrameResources; ++i)
	{
		frameResources.push_back(std::make_unique<FrameResource>(dxDevice.Get(), 1, renderItems.size(), materials.size()));
	}
}

void ShapesApp::BuildPSOs()
{
	for (auto& material : materials)
	{		
		material.second->InitMaterial(dxDevice.Get());
	}	
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems) const
{	
	// For each render item...
	for (auto& ri : ritems)
	{
		ri->Draw(cmdList);		
	}
}

void ShapesApp::SortGOByMaterial()
{
	typedRenderItems.clear();
	for (auto && item : renderItems)
	{
		auto renderer = item->GetRenderer();
		if(renderer != nullptr)
		{
			typedRenderItems[renderer->Material->GetType()].push_back(item.get());
		}
	}
}
