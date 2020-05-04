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

	if(camera != nullptr)
	{
		camera->SetAspectRatio(AspectRatio());
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
	
	UpdateGameObjects(gt);
	UpdateGlobalCB(gt);
	
}

void ShapesApp::Draw(const GameTimer& gt)
{	
	auto frameAlloc = currentFrameResource->commandListAllocator;

	
	ThrowIfFailed(frameAlloc->Reset());


	ThrowIfFailed(commandList->Reset(frameAlloc.Get(), psos[PsoType::SkyBox]->GetPSO().Get()));
	
	

	
	commandList->RSSetViewports(1, &screenViewport);
	commandList->RSSetScissorRects(1, &scissorRect);

	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	commandList->ClearRenderTargetView(GetCurrentBackBufferView(), Colors::BlanchedAlmond, 0, nullptr);
	commandList->ClearDepthStencilView(GetDepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	commandList->OMSetRenderTargets(1, &GetCurrentBackBufferView(), true, &GetDepthStencilView());

	

	commandList->SetGraphicsRootSignature(rootSignature->GetRootSignature().Get());
		
	auto passCB = currentFrameResource->PassConstantBuffer->Resource();	
	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	commandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

	commandList->SetPipelineState(psos[PsoType::SkyBox]->GetPSO().Get());
	DrawGameObjects(commandList.Get(), typedGameObjects[(int)PsoType::SkyBox]);

	commandList->SetPipelineState(psos[PsoType::Opaque]->GetPSO().Get());
	DrawGameObjects(commandList.Get(), typedGameObjects[(int)PsoType::Opaque]);


	commandList->OMSetStencilRef(1);
	commandList->SetPipelineState(psos[PsoType::Mirror]->GetPSO().Get());
	DrawGameObjects(commandList.Get(), typedGameObjects[(int)PsoType::Mirror]);


	commandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress() + 1 * passCBByteSize);
	commandList->SetPipelineState(psos[PsoType::Reflection]->GetPSO().Get());
	DrawGameObjects(commandList.Get(), typedGameObjects[(int)PsoType::Reflection]);


	commandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
	commandList->OMSetStencilRef(0);


	commandList->SetPipelineState(psos[PsoType::Transparent]->GetPSO().Get());
	DrawGameObjects(commandList.Get(), typedGameObjects[(int)PsoType::Transparent]);


	commandList->SetPipelineState(psos[PsoType::Shadow]->GetPSO().Get());
	DrawGameObjects(commandList.Get(), typedGameObjects[(int)PsoType::Shadow]);
	
	// Indicate a state transition on the resource usage.
	commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(GetCurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ExecuteCommandList();

	
	ThrowIfFailed(swapChain->Present(0, 0));
	currBackBufferIndex = (currBackBufferIndex + 1) % swapChainBufferCount;

	currentFrameResource->FenceValue = ++currentFence;

	
	commandQueue->Signal(fence.Get(), currentFence);
}

void ShapesApp::UpdateGameObjects(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	//XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
	//XMVECTOR toMainLight = -XMLoadFloat3(&mainPassCB.Lights[0].Direction);
	//XMMATRIX S = XMMatrixShadow(shadowPlane, toMainLight);
	//XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 0.001f, 0.0f);
	//shadowSkull->GetTransform()->SetWorldMatrix(skull->GetTransform()->GetWorldMatrix() * S * shadowOffsetY);
	
	for (auto& e : gameObjects)
	{
		e->Update();		
	}
}

void ShapesApp::UpdateGlobalCB(const GameTimer& gt)
{
	const XMMATRIX view = XMLoadFloat4x4(&camera->GetViewMatrix());
	const XMMATRIX proj = XMLoadFloat4x4(&camera->GetProjectionMatrix());

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
	mainPassCB.EyePosW = camera->gameObject->GetTransform()->GetPosition();
	mainPassCB.RenderTargetSize = XMFLOAT2((float)clientWidth, (float)clientHeight);
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
	auto currentPassCB = currentFrameResource->PassConstantBuffer.get();
	currentPassCB->CopyData(0, mainPassCB);

	reflectedPassCB = mainPassCB;

	XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
	XMMATRIX R = XMMatrixReflect(mirrorPlane);


	for (int i = 0; i < MaxLights; ++i)
	{
		if (i < lights.size())
		{
			XMVECTOR lightDir = XMLoadFloat3(&mainPassCB.Lights[i].Direction);
			XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
			XMStoreFloat3(&reflectedPassCB.Lights[i].Direction, reflectedLightDir);
		}
		else { break; }
	}


	currentPassCB->CopyData(1, reflectedPassCB);
}

void ShapesApp::LoadTextures()
{	
	auto bricksTex = std::make_unique<Texture>("bricksTex", L"Data\\Textures\\bricks.dds");
	bricksTex->LoadTexture(dxDevice.Get(), commandQueue.Get());
	auto stoneTex = std::make_unique<Texture>("stoneTex", L"Data\\Textures\\stone.dds");
	stoneTex->LoadTexture(dxDevice.Get(), commandQueue.Get());
	auto tileTex = std::make_unique<Texture>("tileTex", L"Data\\Textures\\tile.dds");
	tileTex->LoadTexture(dxDevice.Get(), commandQueue.Get());
	auto fenceTex = std::make_unique<Texture>("fenceTex", L"Data\\Textures\\WireFence.dds");
	fenceTex->LoadTexture(dxDevice.Get(), commandQueue.Get());
	auto waterTex = std::make_unique<Texture>("waterTex", L"Data\\Textures\\water1.dds");
	waterTex->LoadTexture(dxDevice.Get(), commandQueue.Get());
	auto skyTex = std::make_unique<Texture>("skyTex", L"Data\\Textures\\skymap.dds");
	skyTex->LoadTexture(dxDevice.Get(), commandQueue.Get());
	
	textures[bricksTex->GetName()] = std::move(bricksTex);
	textures[stoneTex->GetName()] = std::move(stoneTex);
	textures[tileTex->GetName()] = std::move(tileTex);
	textures[fenceTex->GetName()] = std::move(fenceTex);
	textures[waterTex->GetName()] = std::move(waterTex);
	textures[skyTex->GetName()] = std::move(skyTex);
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
	shaders["AlphaDrop"] = std::move(std::make_unique<Shader>(L"Shaders\\Default.hlsl", ShaderType::PixelShader, alphaTestDefines, "PS", "ps_5_1"));
	shaders["OpaquePixel"] = std::move(std::make_unique<Shader>(L"Shaders\\Default.hlsl", ShaderType::PixelShader, defines, "PS", "ps_5_1"));
	shaders["SkyBoxVertex"] = std::move(std::make_unique<Shader>(L"Shaders\\SkyBoxShader.hlsl", ShaderType::VertexShader, defines, "SKYMAP_VS", "vs_5_1"));
	shaders["SkyBoxPixel"] = std::move(std::make_unique<Shader>(L"Shaders\\SkyBoxShader.hlsl", ShaderType::PixelShader, defines, "SKYMAP_PS", "ps_5_1"));

	for (auto && pair : shaders)
	{
		pair.second->LoadAndCompile();
	}

	
	inputLayout =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	
	rootSignature = std::make_unique<RootSignature>();

	CD3DX12_DESCRIPTOR_RANGE texParam[2];
	texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	
	rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);	
	rootSignature->AddConstantBufferParameter(0);
	rootSignature->AddConstantBufferParameter(1);
	rootSignature->AddConstantBufferParameter(2);

	rootSignature->Initialize(dxDevice.Get());
}

void ShapesApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData skySphere = geoGen.CreateSkySphere(10,10);

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT skySphererVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT skySphererIndexOffset = sphereIndexOffset + (UINT)cylinder.Indices32.size();

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

	SubmeshGeometry skySphererSubmeshs;
	skySphererSubmeshs.IndexCount = (UINT)skySphere.Indices32.size();
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

	for (size_t i =0; i < skySphere.Vertices.size(); ++i, ++k)
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

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "roomGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
	                                                    commandList.Get(), vertices.data(), vbByteSize,
	                                                    geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
	                                                   commandList.Get(), indices.data(), ibByteSize,
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

void ShapesApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>("bricks", psos[PsoType::Opaque].get());
	bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks0->Roughness = 0.1f;
	bricks0->SetDiffuseTexture(textures["bricksTex"].get());
	materials[bricks0->GetName()] = std::move(bricks0);

	
	auto stone0 = std::make_unique<Material>("stone0", psos[PsoType::Opaque].get());
	stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	stone0->Roughness = 0.3f;
	stone0->SetDiffuseTexture(textures["stoneTex"].get());
	materials[stone0->GetName()] = std::move(stone0);
	
	auto tile0 = std::make_unique<Material>("tile0", psos[PsoType::Opaque].get());
	tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	tile0->Roughness = 0.3f;
	tile0->SetDiffuseTexture(textures["tileTex"].get());
	materials[tile0->GetName()] = std::move(tile0);

	auto wirefence = std::make_unique<Material>( "wirefence", psos[ PsoType::AlphaDrop].get());
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	wirefence->Roughness = 0.25f;
	wirefence->SetDiffuseTexture(textures["fenceTex"].get());
	materials[wirefence->GetName()] = std::move(wirefence);
	
	auto water = std::make_unique<Material>("water", psos[PsoType::Transparent].get());
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;
	water->SetDiffuseTexture(textures["waterTex"].get());
	materials[water->GetName()] = std::move(water);

	auto icemirror = std::make_unique<Material>("mirror", psos[PsoType::Mirror].get());
	icemirror->SetDiffuseTexture(textures["waterTex"].get());
	icemirror->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
	icemirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	icemirror->Roughness = 0.5f;
	materials[icemirror->GetName()] = std::move(icemirror);
	

	auto shadowMat = std::make_unique<Material>("shadow", psos[PsoType::Shadow].get());
	shadowMat->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);
	shadowMat->SetDiffuseTexture(textures["bricksTex"].get());
	shadowMat->FresnelR0 = XMFLOAT3(0.001f, 0.001f, 0.001f);
	shadowMat->Roughness = 0.0f;
	materials[shadowMat->GetName()] = std::move(shadowMat);

	
	auto skyBox = std::make_unique<Material>("sky", psos[PsoType::SkyBox].get());
	skyBox->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skyBox->SetDiffuseTexture(textures["skyTex"].get());
	materials[skyBox->GetName()] = std::move(skyBox);

	for (auto && pair : materials)
	{
		pair.second->InitMaterial(dxDevice.Get());
	}
}

void ShapesApp::BuildGameObjects()
{
	auto camera = std::make_unique<GameObject>(dxDevice.Get(), "MainCamera");
	camera->GetTransform()->SetPosition({ -3.667396 , 3.027442 , -12.024042 });
	camera->GetTransform()->SetEulerRotate({ -0.100110 , -2.716100 , 0.000000 });
	camera->AddComponent(new Camera(AspectRatio()));
	camera->AddComponent(new CameraController());
	gameObjects.push_back(std::move(camera));

	auto skySphere = std::make_unique<GameObject>(dxDevice.Get(), "Sky");
	skySphere->GetTransform()->SetScale({ 500,500,500 });
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


	
	auto sun1 = std::make_unique<GameObject>(dxDevice.Get(), "Directional Light");
	auto light = new Light();	
	light->Direction({ 0.57735f, -0.57735f, 0.57735f });
	light->Strength({ 0.6f, 0.6f, 0.6f });
	sun1->AddComponent(light);
	


	auto floorRitem = std::make_unique<GameObject>(dxDevice.Get(), "Floor");
	renderer = new Renderer();
	renderer->Material = materials["bricks"].get();
	renderer->Mesh = meshes["roomGeo"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["floor"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["floor"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["floor"].BaseVertexLocation;
	floorRitem->AddComponent(renderer);
	typedGameObjects[(int)PsoType::Opaque].push_back(floorRitem.get());
	

	auto wallsRitem = std::make_unique<GameObject>(dxDevice.Get(), "Wall");
	renderer = new Renderer();
	renderer->Material = materials["bricks"].get();
	renderer->Mesh = meshes["roomGeo"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["wall"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["wall"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["wall"].BaseVertexLocation;
	wallsRitem->AddComponent(renderer);
	typedGameObjects[(int)PsoType::Opaque].push_back(wallsRitem.get());
	gameObjects.push_back(std::move(wallsRitem));

	auto skullRitem = std::make_unique<GameObject>(dxDevice.Get(), "Skull");
	skullRitem->GetTransform()->SetPosition({ 0,1,-3 });
	skullRitem->GetTransform()->SetScale({ 2,2,2 });
	renderer = new Renderer();
	renderer->Material = materials["bricks"].get();
	renderer->Mesh = meshes["shapeMesh"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["sphere"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["sphere"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["sphere"].BaseVertexLocation;
	skullRitem->AddComponent(renderer);
	skullRitem->AddComponent(new ObjectMover());
	skull = skullRitem.get();
	typedGameObjects[(int)PsoType::Opaque].push_back(skullRitem.get());
	gameObjects.push_back(std::move(skullRitem));

	// Reflected skull will have different world matrix, so it needs to be its own render item.
	auto reflectedSkullRitem = std::make_unique<GameObject>(dxDevice.Get(), "SkullReflect");	
	reflectedSkullRitem->AddComponent(new Reflected(skull->GetComponent<Renderer>()));
	typedGameObjects[(int)PsoType::Reflection].push_back(reflectedSkullRitem.get());
	gameObjects.push_back(std::move(reflectedSkullRitem));

	auto reflectedflorRitem = std::make_unique<GameObject>(dxDevice.Get(), "FloorReflect");
	reflectedflorRitem->AddComponent(new Reflected(floorRitem->GetComponent<Renderer>()));
	typedGameObjects[(int)PsoType::Reflection].push_back(reflectedflorRitem.get());
	gameObjects.push_back(std::move(reflectedflorRitem));
	gameObjects.push_back(std::move(floorRitem));

	// Shadowed skull will have different world matrix, so it needs to be its own render item.
	auto shadowedSkullRitem = std::make_unique<GameObject>(dxDevice.Get(), "Skull Shadow");
	renderer = new Shadow(skull->GetTransform(),sun1->GetComponent<Light>());
	renderer->Mesh = meshes["shapeMesh"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["sphere"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["sphere"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["sphere"].BaseVertexLocation;
	renderer->Material = materials["shadow"].get();
	shadowedSkullRitem->AddComponent(renderer);
	typedGameObjects[(int)PsoType::Shadow].push_back(shadowedSkullRitem.get());
	

	auto reflectedShadowRitem = std::make_unique<GameObject>(dxDevice.Get(), "ShadowReflect");
	reflectedShadowRitem->AddComponent(new Reflected(shadowedSkullRitem->GetComponent<Renderer>()));
	typedGameObjects[(int)PsoType::Reflection].push_back(reflectedShadowRitem.get());
	gameObjects.push_back(std::move(reflectedShadowRitem));
	gameObjects.push_back(std::move(shadowedSkullRitem));

	
	auto mirrorRitem = std::make_unique<GameObject>(dxDevice.Get(), "Mirror");
	renderer = new Renderer();
	renderer->Mesh = meshes["roomGeo"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["mirror"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["mirror"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["mirror"].BaseVertexLocation;
	renderer->Material = materials["mirror"].get();
	mirrorRitem->AddComponent(renderer);	
	typedGameObjects[(int)PsoType::Mirror].push_back(mirrorRitem.get());
	typedGameObjects[(int)PsoType::Transparent].push_back(mirrorRitem.get());
	gameObjects.push_back(std::move(mirrorRitem));	

	gameObjects.push_back(std::move(sun1));
	
	/*auto sun2 = std::make_unique<GameObject>(dxDevice.Get());
	light = new Light();
	light->Direction({ -0.57735f, -0.57735f, 0.57735f });
	light->Strength({ 0.3f, 0.3f, 0.3f });
	sun2->AddComponent(light);
	gameObjects.push_back(std::move(sun2));

	auto sun3 = std::make_unique<GameObject>(dxDevice.Get());
	light = new Light();
	light->Direction({ 0.0f, -0.707f, -0.707f });
	light->Strength({ 0.15f, 0.15f, 0.15f });
	sun3->AddComponent(light);
	gameObjects.push_back(std::move(sun3));
	
	
	auto man = std::make_unique<GameObject>(dxDevice.Get());
	XMStoreFloat4x4(&man->GetTransform()->TextureTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	auto modelRenderer = new ModelRenderer();
	modelRenderer->Material = materials["bricks0"].get();
	modelRenderer->AddModel(dxDevice.Get(), commandList.Get(), "Data\\Objects\\Nanosuit\\Nanosuit.obj");
	man->AddComponent(modelRenderer);
	gameObjects.push_back(std::move(man));

	

	
	auto box = std::make_unique<GameObject>(dxDevice.Get());
	box->GetTransform()->SetPosition(Vector3(0.0f, 0.25f, 0.0f));
	box->GetTransform()->SetScale(Vector3(5.0f, 5.0f, 5.0f));	
	XMStoreFloat4x4(&box->GetTransform()->TextureTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	renderer = new Renderer();
	renderer->Material = materials["water"].get();
	renderer->Mesh = meshes["shapeMesh"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["box"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["box"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["box"].BaseVertexLocation;
	box->AddComponent(renderer);
	gameObjects.push_back(std::move(box));

	
	
	auto grid = std::make_unique<GameObject>(dxDevice.Get());
	XMStoreFloat4x4(&grid->GetTransform()->TextureTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	renderer = new Renderer();
	renderer->Material = materials["tile0"].get();
	renderer->Mesh = meshes["shapeMesh"].get();
	renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	renderer->IndexCount = renderer->Mesh->Submeshs["grid"].IndexCount;
	renderer->StartIndexLocation = renderer->Mesh->Submeshs["grid"].StartIndexLocation;
	renderer->BaseVertexLocation = renderer->Mesh->Submeshs["grid"].BaseVertexLocation;
	grid->AddComponent(renderer);
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
		renderer->Material = materials["bricks0"].get();
		renderer->Mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->Mesh->Submeshs["cylinder"].IndexCount;
		renderer->StartIndexLocation = renderer->Mesh->Submeshs["cylinder"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->Mesh->Submeshs["cylinder"].BaseVertexLocation;
		leftCylRitem->AddComponent(renderer);
		
		rightCylRitem->GetTransform()->SetPosition(Vector3(-5.0f, 1.5f, -10.0f + i * 5.0f));
		XMStoreFloat4x4(&rightCylRitem->GetTransform()->TextureTransform, brickTexTransform);
		renderer = new Renderer();
		renderer->Material = materials["bricks0"].get();
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

		gameObjects.push_back(std::move(leftCylRitem));
		gameObjects.push_back(std::move(rightCylRitem));
		gameObjects.push_back(std::move(leftSphereRitem));
		gameObjects.push_back(std::move(rightSphereRitem));
	}


	*/
}

void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < globalCountFrameResources; ++i)
	{
		frameResources.push_back(std::make_unique<FrameResource>(dxDevice.Get(), 2, gameObjects.size(), materials.size()));
	}
}

void ShapesApp::BuildPSOs()
{
	auto opaquePSO = std::make_unique<PSO>();
	opaquePSO->SetInputLayout({ inputLayout.data(), (UINT)inputLayout.size() });
	opaquePSO->SetRootSignature(rootSignature->GetRootSignature().Get());
	opaquePSO->SetShader(shaders["StandardVertex"].get());
	opaquePSO->SetShader(shaders["OpaquePixel"].get());
	opaquePSO->SetRTVFormat(0, backBufferFormat);
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
	skyBoxDSS.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
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
	// We are not rendering backfacing polygons, so these settings do not matter.
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

	psos[opaquePSO->GetType()] = std::move(opaquePSO);
	psos[transperentPSO->GetType()] = std::move(transperentPSO);
	psos[shadowPSO->GetType()] = std::move(shadowPSO);
	psos[mirrorPSO->GetType()] = std::move(mirrorPSO);
	psos[reflectionPSO->GetType()] = std::move(reflectionPSO);
	psos[alphaDropPso->GetType()] = std::move(alphaDropPso);
	psos[skyBoxPSO->GetType()] = std::move(skyBoxPSO);

	for (auto & pso : psos)
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
	for (auto && item : gameObjects)
	{
		auto light = item->GetComponent<Light>();
		if(light != nullptr)
		{
			lights.push_back(light);
		}

		auto cam = item->GetComponent<Camera>();
		if(cam != nullptr)
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
