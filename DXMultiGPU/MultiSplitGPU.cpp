#include "MultiSplitGPU.h"
#include "GCommandList.h"
#include "GCommandQueue.h"
#include "Window.h"
#include "GCrossAdapterResource.h"
#include "GDeviceFactory.h"
#include "GModel.h"
#include "Material.h"
#include "Transform.h"

MultiSplitGPU::MultiSplitGPU(HINSTANCE hInstance): D3DApp(hInstance)
{
	mSceneBounds.Center = Vector3(0.0f, 0.0f, 0.0f);
	mSceneBounds.Radius = 175;
	for (int i = 0; i < PsoType::Count; ++i)
	{
		typedGameObjects.push_back(MemoryAllocator::CreateVector<std::shared_ptr<GameObject>>());		
	}
}

void MultiSplitGPU::InitDevices()
{
	devices.resize(GraphicAdapterCount);
	
	const auto firstDevice = GDeviceFactory::GetDevice(GraphicAdapterPrimary);
	const auto otherDevice = GDeviceFactory::GetDevice(GraphicAdapterSecond);

	if (otherDevice->IsCrossAdapterTextureSupported())
	{
		devices[GraphicAdapterPrimary] = otherDevice;
		devices[GraphicAdapterSecond] = firstDevice;		
	}
	else
	{
		devices[GraphicAdapterPrimary] = firstDevice;
		devices[GraphicAdapterSecond] = otherDevice;
	}

	for (auto&& device : devices)
	{
		assets.push_back(AssetsLoader(device));
	}
}



void MultiSplitGPU::InitFrameResource()
{
	for (int i = 0; i < globalCountFrameResources; ++i)
	{
		frameResources.push_back(std::make_unique<SplitFrameResource>(devices.data(), devices.size(), 2, 1));

		auto backBufferDesc = MainWindow->GetBackBuffer(i).GetD3D12ResourceDesc();

		frameResources[i]->CrossAdapterBackBuffer = std::make_unique<GCrossAdapterResource>(backBufferDesc, devices[GraphicAdapterPrimary], devices[GraphicAdapterSecond], L"Shared Back Buffer");
		
		frameResources[i]->PrimeDeviceBackBuffer = GTexture(devices[GraphicAdapterPrimary], MainWindow->GetBackBuffer(i).GetD3D12ResourceDesc(), L"Prime device Back Buffer" + std::to_wstring(i), TextureUsage::RenderTarget);		
	}
}

void MultiSplitGPU::InitRootSignature()
{
	for (int i = 0; i < GraphicAdapterCount; ++i)
	{
		auto rootSignature = std::make_shared<GRootSignature>();
		CD3DX12_DESCRIPTOR_RANGE texParam[4];
		texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0); //SkyMap
		texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0); //ShadowMap
		texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::AmbientMap - 3, 0); //SsaoMap
		texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, assets[i].GetLoadTexturesCount(), StandardShaderSlot::TexturesMap - 3, 0);


		rootSignature->AddConstantBufferParameter(0);
		rootSignature->AddConstantBufferParameter(1);
		rootSignature->AddShaderResourceView(0, 1);
		rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
		rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
		rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
		rootSignature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);
		rootSignature->Initialize(devices[i]);

		rootSignatures.push_back(std::move(rootSignature));
	}

	for (int i = 0; i < GraphicAdapterCount; ++i) 
	{
		auto ssaoRootSignature = std::make_shared<GRootSignature>();

		CD3DX12_DESCRIPTOR_RANGE texTable0;
		texTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);

		CD3DX12_DESCRIPTOR_RANGE texTable1;
		texTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

		ssaoRootSignature->AddConstantBufferParameter(0);
		ssaoRootSignature->AddConstantParameter(1, 1);
		ssaoRootSignature->AddDescriptorParameter(&texTable0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
		ssaoRootSignature->AddDescriptorParameter(&texTable1, 1, D3D12_SHADER_VISIBILITY_PIXEL);

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

		for (auto&& sampler : staticSamplers)
		{
			ssaoRootSignature->AddStaticSampler(sampler);
		}

		ssaoRootSignature->Initialize(devices[i]);

		ssaoRootSignatures.push_back(std::move(ssaoRootSignature));
	}	
}

void MultiSplitGPU::InitPipeLineResource()
{
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

	const D3D12_INPUT_LAYOUT_DESC desc = { defaultInputLayout.data(), defaultInputLayout.size() };
	
	for (int i = 0; i < GraphicAdapterCount; ++i)
	{
		defaultPipelineResources.push_back(std::move(ShaderFactory()));
		defaultPipelineResources[i].LoadDefaultShaders();
		defaultPipelineResources[i].LoadDefaultPSO(devices[i], rootSignatures[i], desc, backBufferFormat, depthStencilFormat, ssaoRootSignatures[i], NormalMapFormat, AmbientMapFormat );

	}
}

void MultiSplitGPU::BuildMaterials()
{
	for (int i = 0; i < GraphicAdapterCount; ++i)
	{
		auto seamless = std::make_shared<Material>(L"seamless", PsoType::Opaque);
		seamless->FresnelR0 = Vector3(0.02f, 0.02f, 0.02f);
		seamless->Roughness = 0.1f;

		auto tex = assets[i].GetTextureIndex(L"seamless");
		seamless->SetDiffuseTexture(assets[i].GetTexture(tex), tex);

		tex = assets[i].GetTextureIndex(L"defaultNormalMap");

		seamless->SetNormalMap(assets[i].GetTexture(tex), tex);
		assets[i].AddMaterial(seamless);



		auto skyBox = std::make_shared<Material>(L"sky", PsoType::SkyBox);
		skyBox->DiffuseAlbedo = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		skyBox->FresnelR0 = Vector3(0.1f, 0.1f, 0.1f);
		skyBox->Roughness = 1.0f;
		skyBox->SetNormalMap(assets[i].GetTexture(tex), tex);

		tex = assets[i].GetTextureIndex(L"skyTex");

		skyBox->SetDiffuseTexture(assets[i].GetTexture(tex), tex);
		assets[i].AddMaterial(skyBox);
	}
}

void MultiSplitGPU::InitSRVMemoryAndMaterials()
{
	for (int i = 0; i < GraphicAdapterCount; ++i)
	{
		srvTexturesMemory.push_back(devices[i]->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, assets[i].GetTextures().size()));


		auto materials = assets[i].GetMaterials();

		for (auto pair : materials)
		{
			pair->InitMaterial(&srvTexturesMemory[i]);
		}
	}

	
}

void MultiSplitGPU::InitRenderPaths()
{
	for (int i = 0; i < GraphicAdapterCount; ++i)
	{

		auto commandQueue = devices[i]->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		auto cmdList = commandQueue->GetCommandList();

		shadowPaths.push_back(std::make_shared<ShadowMap>(devices[i], 4096, 4096));

		ambientPaths.push_back(std::make_shared<Ssao>(
			devices[i],
			cmdList,
			MainWindow->GetClientWidth(), MainWindow->GetClientHeight()));

		antiAliasingPaths.push_back(std::make_shared<SSAA>(devices[i], 1, MainWindow->GetClientWidth(), MainWindow->GetClientHeight()));
		antiAliasingPaths[i]->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

		commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));
	}
}

void MultiSplitGPU::LoadStudyTexture()
{
	for (int i = 0; i < GraphicAdapterCount; ++i)
	{
		auto queue = devices[i]->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);

		auto cmdList = queue->GetCommandList();
		
		auto bricksTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\bricks2.dds", cmdList);
		bricksTex->SetName(L"bricksTex");
		assets[i].AddTexture(bricksTex);

		auto stoneTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\stone.dds", cmdList);
		stoneTex->SetName(L"stoneTex");
		assets[i].AddTexture(stoneTex);

		auto tileTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\tile.dds", cmdList);
		tileTex->SetName(L"tileTex");
		assets[i].AddTexture(tileTex);

		auto fenceTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\WireFence.dds", cmdList);
		fenceTex->SetName(L"fenceTex");
		assets[i].AddTexture(fenceTex);

		auto waterTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\water1.dds", cmdList);
		waterTex->SetName(L"waterTex");
		assets[i].AddTexture(waterTex);

		auto skyTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\skymap.dds", cmdList);
		skyTex->SetName(L"skyTex");
		assets[i].AddTexture(skyTex);

		auto grassTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\grass.dds", cmdList);
		grassTex->SetName(L"grassTex");
		assets[i].AddTexture(grassTex);

		auto treeArrayTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\treeArray2.dds", cmdList);
		treeArrayTex->SetName(L"treeArrayTex");
		assets[i].AddTexture(treeArrayTex);

		auto seamless = GTexture::LoadTextureFromFile(L"Data\\Textures\\seamless_grass.jpg", cmdList);
		seamless->SetName(L"seamless");
		assets[i].AddTexture(seamless);


		std::vector<std::wstring> texNormalNames =
		{
			L"bricksNormalMap",
			L"tileNormalMap",
			L"defaultNormalMap"
		};

		std::vector<std::wstring> texNormalFilenames =
		{
			L"Data\\Textures\\bricks2_nmap.dds",
			L"Data\\Textures\\tile_nmap.dds",
			L"Data\\Textures\\default_nmap.dds"
		};

		for (int j = 0; j < texNormalNames.size(); ++j)
		{
			auto texture = GTexture::LoadTextureFromFile(texNormalFilenames[j], cmdList, TextureUsage::Normalmap);
			texture->SetName(texNormalNames[j]);
			assets[i].AddTexture(texture);
		}

		queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));
	}
}

void MultiSplitGPU::LoadModels()
{
	{
		std::vector<std::shared_ptr<GModel>> models;
		auto queue = devices[GraphicAdapterPrimary]->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COPY);

		auto nano = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\Nanosuit\\Nanosuit.obj");
		models.push_back(nano);
		auto doom = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\DoomSlayer\\doommarine.obj");
		models.push_back(doom);
		auto atlas = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\Atlas\\Atlas.obj");
		models.push_back(atlas);
		auto pbody = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\P-Body\\P-Body.obj");
		models.push_back(pbody);
		auto golem = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\StoneGolem\\Stone.obj");
		models.push_back(golem);
		auto griffon = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\Griffon\\Griffon.FBX");
		griffon->scaleMatrix = Matrix::CreateScale(0.1);
		models.push_back(griffon);
		auto griffonOld = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\GriffonOld\\Griffon.FBX");
		griffonOld->scaleMatrix = Matrix::CreateScale(0.1);
		models.push_back(griffonOld);
		auto mountDragon = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\MOUNTAIN_DRAGON\\MOUNTAIN_DRAGON.FBX");
		mountDragon->scaleMatrix = Matrix::CreateScale(0.1);
		models.push_back(mountDragon);
		auto desertDragon = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\DesertDragon\\DesertDragon.FBX");
		desertDragon->scaleMatrix = Matrix::CreateScale(0.1);
		models.push_back(desertDragon);
		auto stair = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\Temple\\SM_AsianCastle_A.FBX");
		models.push_back(stair);
		auto columns = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\Temple\\SM_AsianCastle_E.FBX");
		models.push_back(columns);
		auto fountain = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\Temple\\SM_Fountain.FBX");
		models.push_back(fountain);
		auto platform = assets[GraphicAdapterPrimary].GetOrCreateModelFromFile(queue, "Data\\Objects\\Temple\\SM_PlatformSquare.FBX");
		models.push_back(platform);

		queue->Flush();

		for (int i = GraphicAdapterPrimary + 1; i < GraphicAdapterCount; ++i)
		{
			for (auto&& model : models)
			{
				model->DublicateModelData(devices[i]);
			}

			for (auto && material : assets[GraphicAdapterPrimary].GetMaterials())
			{
				assets[i].AddMaterial(material);				
			}
			
		}		
	}
}

bool MultiSplitGPU::Initialize()
{
	InitDevices();
	InitMainWindow();
	LoadStudyTexture();

	LoadModels();
	
	InitRenderPaths();
	BuildMaterials();	
	InitSRVMemoryAndMaterials();
	InitRootSignature();
	InitPipeLineResource();
	InitFrameResource();

	

	OnResize();	
	
	
	return true;
}

void MultiSplitGPU::Update(const GameTimer& gt)
{
	const auto commandQueue = devices[GraphicAdapterSecond]->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);

	currentFrameResourceIndex = (currentFrameResourceIndex + 1) % globalCountFrameResources;
	currentFrameResource = frameResources[currentFrameResourceIndex];

	if (currentFrameResource->FenceValue != 0 && !commandQueue->IsFinish(currentFrameResource->FenceValue))
	{
		commandQueue->WaitForFenceValue(currentFrameResource->FenceValue);
	}

	const float dt = gt.DeltaTime();

	for (auto& e : gameObjects)
	{
		e->Update();
	}

	for (int i = 0; i < GraphicAdapterCount; ++i)
	{
		auto currentMaterialBuffer = currentFrameResource->MaterialBuffers[i];

		for (auto&& material : assets[i].GetMaterials())
		{
			material->Update();
			auto constantData = material->GetMaterialConstantData();
			currentMaterialBuffer->CopyData(material->GetIndex(), constantData);
		}
	}
	
	UpdateShadowTransform(gt);
	UpdateMainPassCB(gt);
	UpdateShadowPassCB(gt);
	UpdateSsaoCB(gt);
}

void MultiSplitGPU::UpdateShadowTransform(const GameTimer& gt)
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
	Matrix lightProj = DirectX::XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

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

void MultiSplitGPU::UpdateShadowPassCB(const GameTimer& gt)
{
	auto view = mLightView;
	auto proj = mLightProj;

	auto viewProj = (view * proj);
	auto invView = view.Invert();
	auto invProj = proj.Invert();
	auto invViewProj = viewProj.Invert();

	

	shadowPassCB.View = view.Transpose();
	shadowPassCB.InvView = invView.Transpose();
	shadowPassCB.Proj = proj.Transpose();
	shadowPassCB.InvProj = invProj.Transpose();
	shadowPassCB.ViewProj = viewProj.Transpose();
	shadowPassCB.InvViewProj = invViewProj.Transpose();
	shadowPassCB.EyePosW = mLightPosW;	
	shadowPassCB.NearZ = mLightNearZ;
	shadowPassCB.FarZ = mLightFarZ;
	
	for (int i = 0; i < GraphicAdapterCount; ++i)
	{
		UINT w = shadowPaths[i]->Width();
		UINT h = shadowPaths[i]->Height();
		shadowPassCB.RenderTargetSize = Vector2(static_cast<float>(w), static_cast<float>(h));
		shadowPassCB.InvRenderTargetSize = Vector2(1.0f / w, 1.0f / h);

		auto currPassCB = currentFrameResource->PassConstantBuffers[i];
		currPassCB->CopyData(1, shadowPassCB);
	}
}

void MultiSplitGPU::UpdateMainPassCB(const GameTimer& gt)
{
	auto view = camera->GetViewMatrix();
	auto proj = camera->GetProjectionMatrix();

	auto viewProj = (view * proj);
	auto invView = view.Invert();
	auto invProj = proj.Invert();
	auto invViewProj = viewProj.Invert();
	auto shadowTransform = mShadowTransform;

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	Matrix T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);
	Matrix viewProjTex = XMMatrixMultiply(viewProj, T);
	mainPassCB.debugMap = pathMapShow;
	mainPassCB.View = view.Transpose();
	mainPassCB.InvView = invView.Transpose();
	mainPassCB.Proj = proj.Transpose();
	mainPassCB.InvProj = invProj.Transpose();
	mainPassCB.ViewProj = viewProj.Transpose();
	mainPassCB.InvViewProj = invViewProj.Transpose();
	mainPassCB.ViewProjTex = viewProjTex.Transpose();
	mainPassCB.ShadowTransform = shadowTransform.Transpose();
	mainPassCB.EyePosW = camera->gameObject->GetTransform()->GetWorldPosition();
	mainPassCB.RenderTargetSize = Vector2(static_cast<float>(MainWindow->GetClientWidth()),
		static_cast<float>(MainWindow->GetClientHeight()));
	mainPassCB.InvRenderTargetSize = Vector2(1.0f / mainPassCB.RenderTargetSize.x,
		1.0f / mainPassCB.RenderTargetSize.y);
	mainPassCB.NearZ = 1.0f;
	mainPassCB.FarZ = 1000.0f;
	mainPassCB.TotalTime = gt.TotalTime();
	mainPassCB.DeltaTime = gt.DeltaTime();
	mainPassCB.AmbientLight = Vector4{ 0.25f, 0.25f, 0.35f, 1.0f };

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
	mainPassCB.Lights[0].Strength = Vector3{ 0.9f, 0.8f, 0.7f };
	mainPassCB.Lights[1].Direction = mRotatedLightDirections[1];
	mainPassCB.Lights[1].Strength = Vector3{ 0.4f, 0.4f, 0.4f };
	mainPassCB.Lights[2].Direction = mRotatedLightDirections[2];
	mainPassCB.Lights[2].Strength = Vector3{ 0.2f, 0.2f, 0.2f };

	for (int i = 0; i < GraphicAdapterCount; ++i)
	{
		auto currentPassCB = currentFrameResource->PassConstantBuffers[i];
		currentPassCB->CopyData(0, mainPassCB);
	}

	
}

void MultiSplitGPU::UpdateSsaoCB(const GameTimer& gt)
{
	SsaoConstants ssaoCB;

	auto P = camera->GetProjectionMatrix();

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	Matrix T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f);

	ssaoCB.Proj = mainPassCB.Proj;
	ssaoCB.InvProj = mainPassCB.InvProj;
	XMStoreFloat4x4(&ssaoCB.ProjTex, XMMatrixTranspose(P * T));

	for (int i = 0; i < GraphicAdapterCount; ++i)
	{
		ambientPaths[i]->GetOffsetVectors(ssaoCB.OffsetVectors);

		auto blurWeights = ambientPaths[i]->CalcGaussWeights(2.5f);
		ssaoCB.BlurWeights[0] = Vector4(&blurWeights[0]);
		ssaoCB.BlurWeights[1] = Vector4(&blurWeights[4]);
		ssaoCB.BlurWeights[2] = Vector4(&blurWeights[8]);

		ssaoCB.InvRenderTargetSize = Vector2(1.0f / ambientPaths[i]->SsaoMapWidth(), 1.0f / ambientPaths[i]->SsaoMapHeight());

		// Coordinates given in view space.
		ssaoCB.OcclusionRadius = 0.5f;
		ssaoCB.OcclusionFadeStart = 0.2f;
		ssaoCB.OcclusionFadeEnd = 1.0f;
		ssaoCB.SurfaceEpsilon = 0.05f;

		auto currSsaoCB = currentFrameResource->SsaoConstantBuffers[i];
		currSsaoCB->CopyData(0, ssaoCB);
	}
	
	
}

void MultiSplitGPU::Draw(const GameTimer& gt)
{
	if (isResizing) return;

	
	
	
	backBufferIndex = MainWindow->Present();
}

void MultiSplitGPU::OnResize()
{
	D3DApp::OnResize();


	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = GetSRGBFormat(backBufferFormat);
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	for (int i = 0; i < globalCountFrameResources; ++i)
	{
		MainWindow->GetBackBuffer(i).CreateRenderTargetView(&rtvDesc, &frameResources[i]->RtvMemory[GraphicAdapterSecond], i);
				
		GTexture::Resize(frameResources[i]->PrimeDeviceBackBuffer, MainWindow->GetClientWidth(), MainWindow->GetClientHeight(), 1);
		
		frameResources[i]->PrimeDeviceBackBuffer.CreateRenderTargetView(&rtvDesc, &frameResources[i]->RtvMemory[GraphicAdapterPrimary], i);
	}

	
	primeViewport.Height = static_cast<float>(MainWindow->GetClientHeight());
	primeViewport.Width = static_cast<float>(MainWindow->GetClientWidth());
	primeViewport.MinDepth = 0.0f;
	primeViewport.MaxDepth = 1.0f;
	primeViewport.TopLeftX = 0;
	primeViewport.TopLeftY = 0;

		
	primeRect = D3D12_RECT{ 0,0, MainWindow->GetClientWidth() / 2, MainWindow->GetClientHeight() };
	copyRegionBox = CD3DX12_BOX(primeRect.left, primeRect.top, primeRect.right, primeRect.bottom);	
	secondRect = D3D12_RECT{ MainWindow->GetClientWidth() / 2,0, MainWindow->GetClientWidth() , MainWindow->GetClientHeight() };
}

bool MultiSplitGPU::InitMainWindow()
{
	MainWindow = CreateRenderWindow(devices[GraphicAdapterSecond], mainWindowCaption, 1920, 1080, false);

	return true;
}

LRESULT MultiSplitGPU::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{

	switch (msg)
	{
	case WM_INPUT:
	{
		UINT dataSize;
		GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dataSize,
			sizeof(RAWINPUTHEADER));
		//Need to populate data size first

		if (dataSize > 0)
		{
			std::unique_ptr<BYTE[]> rawdata = std::make_unique<BYTE[]>(dataSize);
			if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawdata.get(), &dataSize,
				sizeof(RAWINPUTHEADER)) == dataSize)
			{
				RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(rawdata.get());
				if (raw->header.dwType == RIM_TYPEMOUSE)
				{
					mouse.OnMouseMoveRaw(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
				}
			}
		}

		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	//Mouse Messages
	case WM_MOUSEMOVE:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnMouseMove(x, y);
		return 0;
	}
	case WM_LBUTTONDOWN:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnLeftPressed(x, y);
		return 0;
	}
	case WM_RBUTTONDOWN:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnRightPressed(x, y);
		return 0;
	}
	case WM_MBUTTONDOWN:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnMiddlePressed(x, y);
		return 0;
	}
	case WM_LBUTTONUP:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnLeftReleased(x, y);
		return 0;
	}
	case WM_RBUTTONUP:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnRightReleased(x, y);
		return 0;
	}
	case WM_MBUTTONUP:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		mouse.OnMiddleReleased(x, y);
		return 0;
	}
	case WM_MOUSEWHEEL:
	{
		int x = LOWORD(lParam);
		int y = HIWORD(lParam);
		if (GET_WHEEL_DELTA_WPARAM(wParam) > 0)
		{
			mouse.OnWheelUp(x, y);
		}
		else if (GET_WHEEL_DELTA_WPARAM(wParam) < 0)
		{
			mouse.OnWheelDown(x, y);
		}
		return 0;
	}
	case WM_KEYUP:

	{



		/*if ((int)wParam == VK_F2)
			Set4xMsaaState(!isM4xMsaa);*/
		unsigned char keycode = static_cast<unsigned char>(wParam);
		keyboard.OnKeyReleased(keycode);



		return 0;
	}
	case WM_KEYDOWN:
	{
		{
			unsigned char keycode = static_cast<unsigned char>(wParam);
			if (keyboard.IsKeysAutoRepeat())
			{
				keyboard.OnKeyPressed(keycode);
			}
			else
			{
				const bool wasPressed = lParam & 0x40000000;
				if (!wasPressed)
				{
					keyboard.OnKeyPressed(keycode);
				}
			}

			if (keycode == (VK_F2) && keyboard.KeyIsPressed(VK_F2))
			{
				pathMapShow = (pathMapShow + 1) % maxPathMap;
			}
		}
	}

	case WM_CHAR:
	{
		unsigned char ch = static_cast<unsigned char>(wParam);
		if (keyboard.IsCharsAutoRepeat())
		{
			keyboard.OnChar(ch);
		}
		else
		{
			const bool wasPressed = lParam & 0x40000000;
			if (!wasPressed)
			{
				keyboard.OnChar(ch);
			}
		}
		return 0;
	}
	}

	return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}
