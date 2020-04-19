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

	ThrowIfFailed(dxDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, 
		IID_PPV_ARGS(commandListAllocator.GetAddressOf())));


	
	LoadTextures();
	BuildRootSignature();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildMaterials();
	BuildRenderItems();	
	BuildFrameResources();
	BuildPSOs();
	
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
	return;
	
	UpdateCamera(gt);

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

	
	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
	auto frameAlloc1 = commandListAllocator;
	
	ThrowIfFailed(frameAlloc1->Reset());

	ThrowIfFailed(commandList->Reset(frameAlloc1.Get(), psos[PSOType::Wireframe].Get()));

	ExecuteCommandList();
	
	return;
	
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

	commandList->SetGraphicsRootSignature(rootSignature.Get());

	ID3D12DescriptorHeap* descriptorHeaps[] = { shaderTextureViewDescriptorHeap.Get() };
	commandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);	

	auto passCB = currentFrameResource->PassConstantBuffer->Resource();
	commandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());	

	for (auto it = typedRenderItems.begin(); it != typedRenderItems.end(); ++it)
	{
		if (isWireframe)
		{
			commandList->SetPipelineState(psos[PSOType::Wireframe].Get());
		}
		else
		{
			commandList->SetPipelineState(psos[it->first].Get());
		}
		
		
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
	auto currObjectCB = currentFrameResource->ObjectConstantBuffer.get();
	for (auto& e : renderItems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			const XMMATRIX world = XMLoadFloat4x4(&e->World);
			const XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TextureTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ConstantBufferIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void ShapesApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currentMaterialCB = currentFrameResource->MaterialConstantBuffer.get();
	for (auto& e : materials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* material = e.second.get();
		if (material->NumFramesDirty > 0)
		{
			const XMMATRIX matTransform = XMLoadFloat4x4(&material->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = material->DiffuseAlbedo;
			matConstants.FresnelR0 = material->FresnelR0;
			matConstants.Roughness = material->Roughness;
			XMStoreFloat4x4(&matConstants.MaterialTransform, XMMatrixTranspose(matTransform));

			currentMaterialCB->CopyData(material->ConstantBufferIndex, matConstants);

			// Next FrameResource need to be updated too.
			material->NumFramesDirty--;
		}
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
	ResourceUploadBatch upload(dxDevice.Get());
	upload.Begin();
	
	auto bricksTex = std::make_unique<Texture>();
	bricksTex->Name = "bricksTex";
	bricksTex->Filename = L"Textures\\bricks.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile(dxDevice.Get(),
		upload, bricksTex->Filename.c_str(),bricksTex->Resource.GetAddressOf()));

	// Upload the resources to the GPU.
	auto finish = upload.End(commandQueue.Get());

	// Wait for the upload thread to terminate
	finish.wait();

	upload.Begin();
	
	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"Textures\\stone.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile(dxDevice.Get(), upload, stoneTex->Filename.c_str(),
		stoneTex->Resource.GetAddressOf()));

	finish = upload.End(commandQueue.Get());

	finish.wait();

	upload.Begin();
	auto tileTex = std::make_unique<Texture>();
	tileTex->Name = "tileTex";
	tileTex->Filename = L"Textures\\tile.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile(dxDevice.Get(),
		upload, tileTex->Filename.c_str(), tileTex->Resource.GetAddressOf()));

	finish = upload.End(commandQueue.Get());

	finish.wait();

	upload.Begin();
	auto fenceTex = std::make_unique<Texture>();
	fenceTex->Name = "fenceTex";
	fenceTex->Filename = L"Textures\\WireFence.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile(dxDevice.Get(),
		upload, fenceTex->Filename.c_str(), fenceTex->Resource.GetAddressOf()));

	finish = upload.End(commandQueue.Get());

	finish.wait();
	
	upload.Begin();
	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"Textures\\water1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile(dxDevice.Get(),
		upload, waterTex->Filename.c_str(), waterTex->Resource.GetAddressOf()));

	finish = upload.End(commandQueue.Get());

	finish.wait();

	
	textures[bricksTex->Name] = std::move(bricksTex);
	textures[stoneTex->Name] = std::move(stoneTex);
	textures[tileTex->Name] = std::move(tileTex);
	textures[fenceTex->Name] = std::move(fenceTex);
	textures[waterTex->Name] = std::move(waterTex);
}

void ShapesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable[2];
	texTable[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
		1,  // number of descriptors
		0); // register t0
	texTable[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
	
	 
	CD3DX12_ROOT_PARAMETER slotRootParameter[5];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable[0], D3D12_SHADER_VISIBILITY_PIXEL);
	slotRootParameter[1].InitAsConstantBufferView(0);
	slotRootParameter[2].InitAsConstantBufferView(1);
	slotRootParameter[3].InitAsConstantBufferView(2);
	slotRootParameter[4].InitAsDescriptorTable(1, &texTable[1], D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GetStaticSamplers();

	
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(_countof(slotRootParameter), slotRootParameter,
		staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	const HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(dxDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(rootSignature.GetAddressOf())));
}

void ShapesApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = textures.size();
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(dxDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&shaderTextureViewDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(shaderTextureViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	
	for (auto it = textures.begin(); it != textures.end(); ++it)
	{
		srvDesc.Format = it->second->Resource->GetDesc().Format;
		srvDesc.Texture2D.MipLevels = it->second->Resource->GetDesc().MipLevels;
		dxDevice->CreateShaderResourceView(it->second->Resource.Get(), &srvDesc, hDescriptor);
		hDescriptor.Offset(1, cbvSrvUavDescriptorSize);
	}	
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
	
	shaders[ShaderType::StandartVS] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	shaders[ShaderType::AlphaDropPS] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_1");
	shaders[ShaderType::OpaquePS] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_1");

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
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->ConstantBufferIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->ConstantBufferIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;

	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->ConstantBufferIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.3f;


	auto wirefence = std::make_unique<Material>();
	wirefence->Name = "wirefence";
	wirefence->ConstantBufferIndex = 3;
	wirefence->DiffuseSrvHeapIndex = 3;
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	wirefence->Roughness = 0.25f;

	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->ConstantBufferIndex = 4;
	water->DiffuseSrvHeapIndex = 4;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;
	
	materials["bricks0"] = std::move(bricks0);
	materials["stone0"] = std::move(stone0);
	materials["tile0"] = std::move(tile0);
	materials["wirefence"] = std::move(wirefence);
	materials[water->Name] = std::move(water);
}

void ShapesApp::BuildRenderItems()
{
	auto box = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&box->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
	XMStoreFloat4x4(&box->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	box->ConstantBufferIndex = 0;
	box->Material = materials["water"].get();
	box->Mesh = meshes["shapeMesh"].get();
	box->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	box->IndexCount = box->Mesh->Submeshs["box"].IndexCount;
	box->StartIndexLocation = box->Mesh->Submeshs["box"].StartIndexLocation;
	box->BaseVertexLocation = box->Mesh->Submeshs["box"].BaseVertexLocation;

	typedRenderItems[PSOType::Transparent].push_back(box.get());	
	renderItems.push_back(std::move(box));

	auto grid = std::make_unique<RenderItem>();
	grid->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&grid->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	grid->ConstantBufferIndex = 1;
	grid->Material = materials["tile0"].get();
	grid->Mesh = meshes["shapeMesh"].get();
	grid->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	grid->IndexCount = grid->Mesh->Submeshs["grid"].IndexCount;
	grid->StartIndexLocation = grid->Mesh->Submeshs["grid"].StartIndexLocation;
	grid->BaseVertexLocation = grid->Mesh->Submeshs["grid"].BaseVertexLocation;
	typedRenderItems[PSOType::Opaque].push_back(grid.get());
	renderItems.push_back(std::move(grid));

	const XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	UINT objCBIndex = 2;
	for (int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		const XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		const XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		const XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		const XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ConstantBufferIndex = objCBIndex++;
		leftCylRitem->Material = materials["bricks0"].get();
		leftCylRitem->Mesh = meshes["shapeMesh"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Mesh->Submeshs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Mesh->Submeshs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Mesh->Submeshs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ConstantBufferIndex = objCBIndex++;
		rightCylRitem->Material = materials["bricks0"].get();
		rightCylRitem->Mesh = meshes["shapeMesh"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Mesh->Submeshs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Mesh->Submeshs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Mesh->Submeshs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->TexTransform = MathHelper::Identity4x4();
		leftSphereRitem->ConstantBufferIndex = objCBIndex++;
		leftSphereRitem->Material = materials["wirefence"].get();
		leftSphereRitem->Mesh = meshes["shapeMesh"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Mesh->Submeshs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Mesh->Submeshs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Mesh->Submeshs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ConstantBufferIndex = objCBIndex++;
		rightSphereRitem->Material = materials["wirefence"].get();
		rightSphereRitem->Mesh = meshes["shapeMesh"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Mesh->Submeshs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Mesh->Submeshs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Mesh->Submeshs["sphere"].BaseVertexLocation;

		typedRenderItems[PSOType::Opaque].push_back((leftCylRitem.get()));
		typedRenderItems[PSOType::Opaque].push_back((rightCylRitem.get()));
		typedRenderItems[PSOType::AlphaDrop].push_back((leftSphereRitem.get()));
		typedRenderItems[PSOType::AlphaDrop].push_back((rightSphereRitem.get()));
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
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	opaquePsoDesc.InputLayout = { inputLayout.data(), (UINT)inputLayout.size() };
	opaquePsoDesc.pRootSignature = rootSignature.Get();
	opaquePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(shaders[ShaderType::StandartVS]->GetBufferPointer()),
		shaders[ShaderType::StandartVS]->GetBufferSize()
	};
	opaquePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(shaders[ShaderType::OpaquePS]->GetBufferPointer()),
		shaders[ShaderType::OpaquePS]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = backBufferFormat;
	opaquePsoDesc.SampleDesc.Count = isM4xMsaa ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = isM4xMsaa ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = depthStencilFormat;
	ThrowIfFailed(dxDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&psos[PSOType::Opaque])));


	//
	// PSO for opaque wireframe objects.
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframePsoDesc = opaquePsoDesc;
	wireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(dxDevice->CreateGraphicsPipelineState(&wireframePsoDesc, IID_PPV_ARGS(&psos[Wireframe])));

	
	//
	// PSO for transparent objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

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

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(dxDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&psos[PSOType::Transparent])));


	//
	// PSO for alpha tested objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
	alphaTestedPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(shaders[ShaderType::AlphaDropPS]->GetBufferPointer()),
		shaders[ShaderType::AlphaDropPS]->GetBufferSize()
	};
	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(dxDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&psos[PSOType::AlphaDrop])));
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems) const
{
	const UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	const UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = currentFrameResource->ObjectConstantBuffer->Resource();
	auto matCB = currentFrameResource->MaterialConstantBuffer->Resource();	
	
	// For each render item...
	for (auto& ri : ritems)
	{
		cmdList->IASetVertexBuffers(0, 1, &ri->Mesh->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Mesh->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(shaderTextureViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		//CD3DX12_GPU_DESCRIPTOR_HANDLE tex1(shaderTextureViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Material->DiffuseSrvHeapIndex, cbvSrvUavDescriptorSize);
		//tex1.Offset(2, cbvSrvUavDescriptorSize);
		
		const D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ConstantBufferIndex * objCBByteSize;
		const D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Material->ConstantBufferIndex * matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
		//cmdList->SetGraphicsRootDescriptorTable(4, tex1);
		cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}


std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> ShapesApp::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}
