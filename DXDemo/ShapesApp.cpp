#include "ShapesApp.h"

#include <DirectXTex.h>

#include "Renderer.h"
#include "GameObject.h"
#include "ModelRenderer.h"
#include "Camera.h"
#include <ppl.h>
#include "CameraController.h"
#include "GCommandQueue.h"
#include "Reflected.h"
#include "Shadow.h"
#include "pix3.h"
#include "filesystem"
#include "GCommandList.h"
#include "GDataUploader.h"
#include "Window.h"
#include "GMemory.h"
namespace DXLib
{
	ShapesApp::ShapesApp(HINSTANCE hInstance)
		: D3DApp(hInstance)
	{
		mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
		mSceneBounds.Radius = sqrtf(15 * 15.0f + 15.0f * 15.0f);

		for (int i = 0; i < PsoType::Count; ++i)
		{
			typedGameObjects.push_back(DXAllocator::CreateVector<GameObject*>());
		}
	}

	ShapesApp::~ShapesApp()
	{
		Flush();
	}

	

	void ShapesApp::GeneratedMipMap()
	{
		std::vector<Texture*> generatedMipTextures;

		for (auto&& texture : textures)
		{
			texture.second->ClearTrack();

			/*ТОлько те что можно использовать как UAV*/
			if (texture.second->GetResource()->GetDesc().Flags != D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
				continue;

			
			if (!texture.second->HasMipMap)
			{
				generatedMipTextures.push_back(texture.second.get());
			}
		}
				

		auto graphicQueue = GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		auto graphicList = graphicQueue->GetCommandList();

		for (auto && texture : generatedMipTextures)
		{
			graphicList->TransitionBarrier(texture->GetResource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}		
		graphicQueue->WaitForFenceValue(graphicQueue->ExecuteCommandList(graphicList));

		const auto computeQueue = this->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
		
		Texture::GenerateMipMaps(computeQueue.get(), generatedMipTextures.data(), generatedMipTextures.size());				
		

		
		
		graphicList = graphicQueue->GetCommandList();
		for (auto&& texture : generatedMipTextures)
		{
			graphicList->TransitionBarrier(texture->GetResource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}
		graphicQueue->WaitForFenceValue(graphicQueue->ExecuteCommandList(graphicList));

		
		for (auto&& pair : textures)
		{
			pair.second->ClearTrack();
		}

		DXAllocator::UploaderClear();
	}

	bool ShapesApp::Initialize()
	{
		if (!D3DApp::Initialize())
			return false;

		auto commandQueue = this->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		auto cmdList = commandQueue->GetCommandList();

		mShadowMap = std::make_unique<ShadowMap>( 4096, 4096);

		mSsao = std::make_unique<Ssao>(
			dxDevice.Get(),
			cmdList->GetGraphicsCommandList().Get(),
			MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

		LoadTextures(commandQueue->GetD3D12CommandQueue().Get(), cmdList->GetGraphicsCommandList().Get());


		commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));


		GeneratedMipMap();


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


		// Wait until initialization is complete.
		commandQueue->Flush();

		return true;
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
			mSsao->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

			// Resources changed, so need to rebuild descriptors.
			mSsao->RebuildDescriptors(MainWindow->GetDepthStencilBuffer().GetResource());
		}
	}

	void ShapesApp::Update(const GameTimer& gt)
	{
		auto commandQueue = this->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);

		// Cycle through the circular frame resource array.
		currentFrameResourceIndex = (currentFrameResourceIndex + 1) % globalCountFrameResources;
		currentFrameResource = frameResources[currentFrameResourceIndex].get();

		// Has the GPU finished processing the commands of the current frame resource?
		// If not, wait until the GPU has completed commands up to this fence point.
		if (currentFrameResource->FenceValue != 0 && !commandQueue->IsFenceComplete(currentFrameResource->FenceValue))
		{
			commandQueue->WaitForFenceValue(currentFrameResource->FenceValue);
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

		auto commandQueue = GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);

		auto cmdListD3d = commandQueue->GetCommandList();
		auto cmdList = cmdListD3d->GetGraphicsCommandList();


		ID3D12DescriptorHeap* descriptorHeaps[] = {srvHeap.GetDescriptorHeap()};
		cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

		cmdList->SetGraphicsRootSignature(rootSignature->GetRootSignature().Get());

		PIXBeginEvent(commandQueue->GetD3D12CommandQueue().Get(), 0, L"Prepare Render 3D");
			

		/*Bind all materials*/
		auto matBuffer = currentFrameResource->MaterialBuffer->Resource();
		cmdList->SetGraphicsRootShaderResourceView(StandardShaderSlot::MaterialData, matBuffer->GetGPUVirtualAddress());

		
		/*Bind all Diffuse Textures*/
		cmdList->SetGraphicsRootDescriptorTable(StandardShaderSlot::TexturesMap,srvHeap.GetGPUHandle());
		PIXEndEvent(commandQueue->GetD3D12CommandQueue().Get());


		PIXBeginEvent(commandQueue->GetD3D12CommandQueue().Get(), 0, L"Render 3D");

		PIXBeginEvent(commandQueue->GetD3D12CommandQueue().Get(), 0, L"Shadow Map Pass");
		DrawSceneToShadowMap(cmdList.Get());
		PIXEndEvent(commandQueue->GetD3D12CommandQueue().Get());

		PIXBeginEvent(commandQueue->GetD3D12CommandQueue().Get(), 0, L"Normal and Depth Pass");
		DrawNormalsAndDepth(cmdList.Get());
		PIXEndEvent(commandQueue->GetD3D12CommandQueue().Get());

		PIXBeginEvent(commandQueue->GetD3D12CommandQueue().Get(), 0, L"Compute SSAO");
		if (computeSsao)
		{
			cmdList->SetGraphicsRootSignature(ssaoRootSignature.Get());
			mSsao->ComputeSsao(cmdList.Get(), currentFrameResource, 3);
		}
		else
		{
			mSsao->ClearAmbiantMap(cmdList.Get());
		}
		PIXEndEvent(commandQueue->GetD3D12CommandQueue().Get());

		PIXBeginEvent(commandQueue->GetD3D12CommandQueue().Get(), 0, L"Main Pass");

		cmdList->SetGraphicsRootSignature(rootSignature->GetRootSignature().Get());

		cmdList->RSSetViewports(1, &MainWindow->GetViewPort());
		cmdList->RSSetScissorRects(1, &MainWindow->GetRect());

		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(MainWindow->GetCurrentBackBuffer().GetResource(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

		cmdList->ClearRenderTargetView(MainWindow->GetCurrentBackBufferView(), Colors::BlanchedAlmond, 0, nullptr);

		/*cmdList->ClearDepthStencilView(GetDepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
			1.0f, 0, 0, nullptr);*/


		cmdList->OMSetRenderTargets(1, &MainWindow->GetCurrentBackBufferView(), true,
		                            &MainWindow->GetDepthStencilView());


		auto passCB = currentFrameResource->PassConstantBuffer->Resource();

		
		cmdList->
			SetGraphicsRootConstantBufferView(StandardShaderSlot::CameraData, passCB->GetGPUVirtualAddress());

		cmdList->SetGraphicsRootDescriptorTable(StandardShaderSlot::ShadowMap, mShadowMap->Srv());
		cmdList->SetGraphicsRootDescriptorTable(StandardShaderSlot::SsaoMap, mSsao->AmbientMapSrv());

		/*Bind all Diffuse Textures*/
		cmdList->SetGraphicsRootDescriptorTable(StandardShaderSlot::TexturesMap,
		                                        srvHeap.GetGPUHandle());


		cmdList->SetPipelineState(psos[PsoType::SkyBox]->GetPSO().Get());
		DrawGameObjects(cmdList.Get(), typedGameObjects[static_cast<int>(PsoType::SkyBox)]);

		cmdList->SetPipelineState(psos[PsoType::Opaque]->GetPSO().Get());
		DrawGameObjects(cmdList.Get(), typedGameObjects[static_cast<int>(PsoType::Opaque)]);

		cmdList->SetPipelineState(psos[PsoType::OpaqueAlphaDrop]->GetPSO().Get());
		DrawGameObjects(cmdList.Get(), typedGameObjects[static_cast<int>(PsoType::OpaqueAlphaDrop)]);

		cmdList->SetPipelineState(psos[PsoType::Transparent]->GetPSO().Get());
		DrawGameObjects(cmdList.Get(), typedGameObjects[static_cast<int>(PsoType::Transparent)]);

		if (ShowAmbiantMap)
		{
			cmdList->SetPipelineState(psos[PsoType::Debug]->GetPSO().Get());
			DrawGameObjects(cmdList.Get(), typedGameObjects[static_cast<int>(PsoType::Debug)]);
		}

		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(MainWindow->GetCurrentBackBuffer().GetResource(),
		                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
		                                                                  D3D12_RESOURCE_STATE_PRESENT));

		currentFrameResource->FenceValue = commandQueue->ExecuteCommandList(cmdListD3d);

		PIXEndEvent(commandQueue->GetD3D12CommandQueue().Get());
		PIXEndEvent(commandQueue->GetD3D12CommandQueue().Get());

		currBackBufferIndex = MainWindow->Present();
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
		mainPassCB.RenderTargetSize = XMFLOAT2(static_cast<float>(MainWindow->GetClientWidth()), static_cast<float>(MainWindow->GetClientHeight()));
		mainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mainPassCB.RenderTargetSize.x, 1.0f / mainPassCB.RenderTargetSize.y);
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

	void ShapesApp::LoadDoomSlayerTexture(ID3D12GraphicsCommandList2* cmdList)
	{
		std::wstring doomFolder(L"Data\\Objects\\DoomSlayer\\");

		std::vector<std::wstring> doomNames =
		{
			L"Doomarms",
			L"Doomcowl",
			L"Doomhelmet",
			L"Doomlegs",
			L"Doomtorso",
			L"Doomvisor"
		};

		std::vector<std::wstring> doomTextures =
		{
			L"models_characters_doommarine_doommarine_arms_c.png",
			L"models_characters_doommarine_doommarine_cowl_c.png",
			L"models_characters_doommarine_doommarine_helmet_c.png",
			L"models_characters_doommarine_doommarine_legs_c.png",
			L"models_characters_doommarine_doommarine_torso_c.png",
			L"models_characters_doommarine_doommarine_visor_c.png"
		};

		std::vector<std::wstring> doomNormals =
		{
			L"models_characters_doommarine_doommarine_arms_n.png",
			L"models_characters_doommarine_doommarine_cowl_n.png",
			L"models_characters_doommarine_doommarine_helmet_n.png",
			L"models_characters_doommarine_doommarine_legs_n.png",
			L"models_characters_doommarine_doommarine_torso_n.png",
			L"models_characters_doommarine_doommarine_visor_n.png"
		};

		for (UINT i = 0; i < doomNames.size(); ++i)
		{			
			auto texture = Texture::LoadTextureFromFile( doomFolder + doomTextures[i], cmdList, TextureUsage::Diffuse);
			texture->SetName(doomNames[i]);
			
			auto normal = Texture::LoadTextureFromFile(doomFolder + doomNormals[i], cmdList,
			                                        TextureUsage::Normalmap);
			normal->SetName(doomNames[i].append(L"Normal"));
			
			textures[texture->GetName()] = std::move(texture);
			textures[normal->GetName()] = std::move(normal);
		}
	}

	void ShapesApp::LoadStudyTexture(ID3D12GraphicsCommandList2* cmdList)
	{
		auto bricksTex = Texture::LoadTextureFromFile( L"Data\\Textures\\bricks2.dds", cmdList);
		bricksTex->SetName(L"bricksTex");
		textures[bricksTex->GetName()] = std::move(bricksTex);

		auto stoneTex = Texture::LoadTextureFromFile( L"Data\\Textures\\stone.dds", cmdList);
		stoneTex->SetName(L"stoneTex");
		textures[stoneTex->GetName()] = std::move(stoneTex);

		auto tileTex = Texture::LoadTextureFromFile( L"Data\\Textures\\tile.dds", cmdList);
		tileTex->SetName(L"tileTex");
		textures[tileTex->GetName()] = std::move(tileTex);

		auto fenceTex = Texture::LoadTextureFromFile(L"Data\\Textures\\WireFence.dds", cmdList);
		fenceTex->SetName(L"fenceTex");
		textures[fenceTex->GetName()] = std::move(fenceTex);

		auto waterTex = Texture::LoadTextureFromFile( L"Data\\Textures\\water1.dds", cmdList);
		waterTex->SetName(L"waterTex" );
		textures[waterTex->GetName()] = std::move(waterTex);

		auto skyTex = Texture::LoadTextureFromFile( L"Data\\Textures\\skymap.dds", cmdList);
		skyTex->SetName(L"skyTex");
		textures[skyTex->GetName()] = std::move(skyTex);

		auto grassTex = Texture::LoadTextureFromFile( L"Data\\Textures\\grass.dds", cmdList);
		grassTex->SetName(L"grassTex");
		textures[grassTex->GetName()] = std::move(grassTex);

		auto treeArrayTex = Texture::LoadTextureFromFile( L"Data\\Textures\\treeArray2.dds", cmdList);
		treeArrayTex->SetName(L"treeArrayTex");
		textures[treeArrayTex->GetName()] = std::move(treeArrayTex);

		auto seamless = Texture::LoadTextureFromFile( L"Data\\Textures\\seamless_grass.jpg", cmdList);
		seamless->SetName(L"seamless");
		textures[seamless->GetName()] = std::move(seamless);


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

		for (int i = 0; i < texNormalNames.size(); ++i)
		{
			auto texture = Texture::LoadTextureFromFile( texNormalFilenames[i], cmdList, TextureUsage::Normalmap);
			texture->SetName(texNormalNames[i]);
			textures[texture->GetName()] = std::move(texture);
		}
	}

	void ShapesApp::LoadNanosuitTexture(ID3D12GraphicsCommandList2* cmdList)
	{
		std::wstring nanoFolder(L"Data\\Objects\\Nanosuit\\");

		std::vector<std::wstring> nanoNames =
		{
			L"Nanoarm",
			L"Nanobody",
			L"Nanoglass",
			L"Nanohand",
			L"Nanohelm",
			L"Nanoleg"
		};

		std::vector<std::wstring> nanoTextures =
		{
			L"arm_dif.png",
			L"body_dif.png",
			L"glass_dif.png",
			L"hand_dif.png",
			L"helmet_dif.png",
			L"leg_dif.png",
		};

		std::vector<std::wstring> nanoNormals =
		{
			L"arm_ddn.png",
			L"body_ddn.png",
			L"glass_ddn.png",
			L"hand_ddn.png",
			L"helmet_ddn.png",
			L"leg_ddn.png",
		};

		for (UINT i = 0; i < nanoNames.size(); ++i)
		{
			auto texture = Texture::LoadTextureFromFile(nanoFolder + nanoTextures[i], cmdList, TextureUsage::Diffuse);
			texture->SetName(nanoNames[i]);
			auto normal = Texture::LoadTextureFromFile( nanoFolder + nanoNormals[i], cmdList,
			                                        TextureUsage::Normalmap);
			normal->SetName(nanoNames[i].append(L"Normal"));
			textures[texture->GetName()] = std::move(texture);
			textures[normal->GetName()] = std::move(normal);
		}
	}

	void ShapesApp::LoadAtlasTexture(ID3D12GraphicsCommandList2* cmdList)
	{
		std::wstring atlasFolder(L"Data\\Objects\\Atlas\\");

		std::vector<std::wstring> AtlasNames =
		{
			L"Atlasframe",
			L"Atlasshell",
			L"Atlaseye",
		};

		std::vector<std::wstring> AtlasTextures =
		{
			L"ballbot_frame.png",
			L"ballbot_shell.png",
			L"bot_eye_ring_lights.png"
		};


		for (UINT i = 0; i < AtlasNames.size(); ++i)
		{
			auto texture =Texture::LoadTextureFromFile( atlasFolder + AtlasTextures[i], cmdList,TextureUsage::Diffuse);
			texture->SetName(AtlasNames[i]);
			textures[texture->GetName()] = std::move(texture);
		}
	}

	void ShapesApp::LoadPBodyTexture(ID3D12GraphicsCommandList2* cmdList)
	{
		std::wstring PBodyFolder(L"Data\\Objects\\P-Body\\");

		std::vector<std::wstring> PBodyNames =
		{
			L"PBodyframe",
			L"PBodyshell",
			L"PBodyorange",
			L"PBodyeye",
		};

		std::vector<std::wstring> PBodyTextures =
		{
			L"eggbot_frame.png",
			L"eggbot_shell.png",
			L"eggbot_orange.png",
			L"bot_eye_ring_lights.png"
		};


		for (UINT i = 0; i < PBodyNames.size(); ++i)
		{			
			auto texture = Texture::LoadTextureFromFile( PBodyFolder + PBodyTextures[i], cmdList,
			                                         TextureUsage::Diffuse);
			texture->SetName(PBodyNames[i]);
			textures[texture->GetName()] = std::move(texture);
		}
	}

	void ShapesApp::LoadGolemTexture(ID3D12GraphicsCommandList2* cmdList)
	{
		std::wstring mechFolder(L"Data\\Objects\\StoneGolem\\");

		std::vector<std::wstring> mechNames =
		{
			L"golemColor",
		};

		std::vector<std::wstring> mechTextures =
		{
			L"diffuso.tif",
		};

		std::vector<std::wstring> mechNormals =
		{
			L"normal.png",
		};

		for (UINT i = 0; i < mechNames.size(); ++i)
		{
			auto texture = Texture::LoadTextureFromFile( mechFolder + mechTextures[i], cmdList, TextureUsage::Diffuse);
			texture->SetName(mechNames[i]);
			auto normal = Texture::LoadTextureFromFile(mechFolder + mechNormals[i], cmdList,
			                                        TextureUsage::Normalmap);
			normal->SetName(mechNames[i].append(L"Normal"));
			textures[texture->GetName()] = std::move(texture);
			textures[normal->GetName()] = std::move(normal);
		}
	}

	void ShapesApp::LoadTextures(ID3D12CommandQueue* queue, ID3D12GraphicsCommandList2* cmdList)
	{
		LoadStudyTexture(cmdList);

		LoadDoomSlayerTexture(cmdList);

		LoadNanosuitTexture(cmdList);

		LoadAtlasTexture(cmdList);

		LoadPBodyTexture(cmdList);

		LoadGolemTexture(cmdList);


		//for (auto&& pair : textures)
		//{
		//	pair.second->LoadTextureFromFile( cmdList);
		//}
	}

	void ShapesApp::BuildRootSignature()
	{
		rootSignature = std::make_unique<RootSignature>();

		CD3DX12_DESCRIPTOR_RANGE texParam[4];
		texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0); //SkyMap
		texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0); //ShadowMap
		texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SsaoMap - 3, 0); //SsaoMap
		texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, textures.size(), StandardShaderSlot::TexturesMap - 3, 0);


		rootSignature->AddConstantBufferParameter(0);
		rootSignature->AddConstantBufferParameter(1);
		rootSignature->AddShaderResourceView(0, 1);
		rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
		rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
		rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
		rootSignature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);

		rootSignature->Initialize();
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
		srvHeap = std::move(DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, textures.size()));			


		mShadowMap->BuildDescriptors();

		mSsao->BuildDescriptors(MainWindow->GetDepthStencilBuffer().GetResource());
	}

	void ShapesApp::BuildShadersAndInputLayout()
	{
		const D3D_SHADER_MACRO defines[] =
		{
			"FOG", "1",
			nullptr, nullptr
		};

		const D3D_SHADER_MACRO alphaTestDefines[] =
		{
			"FOG", "1",
			"ALPHA_TEST", "1",
			nullptr, nullptr
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

		custom_vector<Vertex> vertices = DXAllocator::CreateVector<Vertex>();
		vertices.resize (totalVertexCount);

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

		custom_vector<std::uint16_t> indices = DXAllocator::CreateVector<std::uint16_t>();
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

		auto commandQueue = this->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);

		auto cmdList = commandQueue->GetCommandList();

		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
		                                                    cmdList->GetGraphicsCommandList().Get(), vertices.data(), vbByteSize,
		                                                    geo->VertexBufferUploader);

		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
			cmdList->GetGraphicsCommandList().Get(), indices.data(), ibByteSize,
		                                                   geo->IndexBufferUploader);

		commandQueue->ExecuteCommandList(cmdList);

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

		custom_vector<Vertex> vertices = DXAllocator::CreateVector<Vertex>();
		vertices.resize (grid.Vertices.size());
		for (size_t i = 0; i < grid.Vertices.size(); ++i)
		{
			vertices[i] = grid.Vertices[i];
			vertices[i].Position.y = GetHillsHeight(vertices[i].Position.x, vertices[i].Position.z);
			vertices[i].Normal = GetHillsNormal(vertices[i].Position.x, vertices[i].Position.z);
		}

		const UINT vbByteSize = static_cast<UINT>(vertices.size()) * sizeof(Vertex);

		custom_vector<std::uint16_t> indices = grid.GetIndices16();
		const UINT ibByteSize = static_cast<UINT>(indices.size()) * sizeof(std::uint16_t);

		auto geo = std::make_unique<MeshGeometry>();
		geo->Name = "landGeo";

		ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

		ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

		auto commandQueue = this->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		auto cmdList = commandQueue->GetCommandList();

		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
			cmdList->GetGraphicsCommandList().Get(), vertices.data(), vbByteSize,
		                                                    geo->VertexBufferUploader);

		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
			cmdList->GetGraphicsCommandList().Get(), indices.data(), ibByteSize,
		                                                   geo->IndexBufferUploader);

		commandQueue->ExecuteCommandList(cmdList);

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
		custom_vector<TreeSpriteVertex> vertices = DXAllocator::CreateVector<TreeSpriteVertex>();
		custom_vector<uint16_t> indices = DXAllocator::CreateVector<uint16_t>();

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

		auto commandQueue = this->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		auto cmdList = commandQueue->GetCommandList();


		geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
			cmdList->GetGraphicsCommandList().Get(), vertices.data(), vbByteSize,
		                                                    geo->VertexBufferUploader);

		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
			cmdList->GetGraphicsCommandList().Get(), indices.data(), ibByteSize,
		                                                   geo->IndexBufferUploader);

		commandQueue->ExecuteCommandList(cmdList);

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
		basePsoDesc.InputLayout = {defaultInputLayout.data(), static_cast<UINT>(defaultInputLayout.size())};
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
		ssaoPSO->SetInputLayout({nullptr, 0});
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
		treeSprite->SetInputLayout({treeSpriteInputLayout.data(), static_cast<UINT>(treeSpriteInputLayout.size())});
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
				std::make_unique<FrameResource>(2, gameObjects.size(), materials.size()));
		}
	}

	void ShapesApp::BuildMaterials()
	{
		auto bricks0 = std::make_unique<Material>("bricks", psos[PsoType::Opaque].get());
		bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		bricks0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
		bricks0->Roughness = 0.3f;
		bricks0->SetDiffuseTexture(textures[L"bricksTex"].get());
		bricks0->SetNormalMap(textures[L"bricksNormalMap"].get());
		materials[bricks0->GetName()] = std::move(bricks0);

		auto seamless = std::make_unique<Material>("seamless", psos[PsoType::Opaque].get());
		seamless->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
		seamless->Roughness = 0.1f;
		seamless->SetDiffuseTexture(textures[L"seamless"].get());
		seamless->SetNormalMap(textures[L"defaultNormalMap"].get());
		materials[seamless->GetName()] = std::move(seamless);

		std::vector<std::string> doomNames =
		{
			"Doomarms",
			"Doomcowl",
			"Doomhelmet",
			"Doomlegs",
			"Doomtorso",
			"Doomvisor"
		};

		for (auto&& name : doomNames)
		{
			auto material = std::make_unique<Material>(name, psos[PsoType::Opaque].get());
			material->FresnelR0 = Vector3::One * 0.05;
			material->Roughness = 0.95;

			material->SetDiffuseTexture(textures[AnsiToWString( name)].get());
			material->SetNormalMap(textures[AnsiToWString(name + "Normal")].get());

			if (material->GetName() == "Doomvisor")
			{
				material->DiffuseAlbedo = XMFLOAT4(0, 0.8f, 0, 0.5f);
				material->FresnelR0 = Vector3::One * 0.1;
				material->Roughness = 0.99f;
			}

			materials[material->GetName()] = std::move(material);
		}

		std::vector<std::string> nanoNames =
		{
			"Nanoarm",
			"Nanobody",
			"Nanoglass",
			"Nanohand",
			"Nanohelm",
			"Nanoleg"
		};

		for (auto&& name : nanoNames)
		{
			auto material = std::make_unique<Material>(name, psos[PsoType::Opaque].get());
			material->FresnelR0 = Vector3::One * 0.05;
			material->Roughness = 0.95;

			material->SetDiffuseTexture(textures[AnsiToWString(name)].get());
			material->SetNormalMap(textures[AnsiToWString(name + "Normal")].get());

			if (material->GetName() == "Nanoglass")
			{
				material->DiffuseAlbedo = XMFLOAT4(0, 0.8f, 0, 0.5f);
				material->FresnelR0 = Vector3::One * 0.1;
				material->Roughness = 0.99f;
			}

			materials[material->GetName()] = std::move(material);
		}

		std::vector<std::string> AtlasNames =
		{
			"Atlasframe",
			"Atlasshell",
			"Atlaseye",
		};

		for (auto&& name : AtlasNames)
		{
			auto material = std::make_unique<Material>(name, psos[PsoType::Opaque].get());
			material->FresnelR0 = Vector3::One * 0.05;
			material->Roughness = 0.6;
			material->SetDiffuseTexture(textures[AnsiToWString(name)].get());
			material->SetNormalMap(textures[L"defaultNormalMap"].get());
			materials[material->GetName()] = std::move(material);
		}

		std::vector<std::string> PBodyNames =
		{
			"PBodyframe",
			"PBodyshell",
			"PBodyorange",
			"PBodyeye",
		};

		for (auto&& name : PBodyNames)
		{
			auto material = std::make_unique<Material>(name, psos[PsoType::Opaque].get());
			material->FresnelR0 = Vector3::One * 0.05;
			material->Roughness = 0.6;
			material->SetDiffuseTexture(textures[AnsiToWString(name)].get());
			material->SetNormalMap(textures[L"defaultNormalMap"].get());
			materials[material->GetName()] = std::move(material);
		}

		std::vector<std::string> mechNames =
		{
			"golemColor",
		};

		for (auto&& name : mechNames)
		{
			auto material = std::make_unique<Material>(name, psos[PsoType::Opaque].get());
			material->FresnelR0 = Vector3::One * 0.05;
			material->Roughness = 0.95;

			material->SetDiffuseTexture(textures[AnsiToWString(name)].get());
			material->SetNormalMap(textures[AnsiToWString(name + "Normal")].get());
			materials[material->GetName()] = std::move(material);
		}

		auto stone0 = std::make_unique<Material>("stone0", psos[PsoType::Opaque].get());
		stone0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
		stone0->Roughness = 0.1f;
		stone0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
		stone0->SetDiffuseTexture(textures[L"stoneTex"].get());
		stone0->SetNormalMap(textures[L"defaultNormalMap"].get());
		materials[stone0->GetName()] = std::move(stone0);

		auto tile0 = std::make_unique<Material>("tile0", psos[PsoType::Opaque].get());
		tile0->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
		tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
		tile0->Roughness = 0.1f;
		tile0->SetDiffuseTexture(textures[L"tileTex"].get());
		tile0->SetNormalMap(textures[L"tileNormalMap"].get());
		materials[tile0->GetName()] = std::move(tile0);

		auto wirefence = std::make_unique<Material>("wirefence", psos[PsoType::OpaqueAlphaDrop].get());
		wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
		wirefence->Roughness = 0.25f;
		wirefence->SetDiffuseTexture(textures[L"fenceTex"].get());
		wirefence->SetNormalMap(textures[L"defaultNormalMap"].get());
		materials[wirefence->GetName()] = std::move(wirefence);

		auto water = std::make_unique<Material>("water", psos[PsoType::Transparent].get());
		water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
		water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
		water->Roughness = 0.0f;
		water->SetDiffuseTexture(textures[L"waterTex"].get());
		water->SetNormalMap(textures[L"defaultNormalMap"].get());
		materials[water->GetName()] = std::move(water);

		auto skyBox = std::make_unique<Material>("sky", psos[PsoType::SkyBox].get());
		skyBox->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		skyBox->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
		skyBox->Roughness = 1.0f;
		skyBox->SetDiffuseTexture(textures[L"skyTex"].get());
		skyBox->SetNormalMap(textures[L"defaultNormalMap"].get());
		materials[skyBox->GetName()] = std::move(skyBox);


		auto treeSprites = std::make_unique<Material>("treeSprites", psos[PsoType::AlphaSprites].get());
		treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
		treeSprites->Roughness = 0.125f;
		treeSprites->SetDiffuseTexture(textures[L"treeArrayTex"].get());
		treeSprites->SetNormalMap(textures[L"defaultNormalMap"].get());
		materials[treeSprites->GetName()] = std::move(treeSprites);

		for (auto&& pair : materials)
		{
			pair.second->InitMaterial(srvHeap);
		}
	}

	void ShapesApp::BuildGameObjects()
	{
		auto camera = std::make_unique<GameObject>(dxDevice.Get(), "MainCamera");
		camera->GetTransform()->SetPosition({-3.667396, 3.027442, -12.024042});
		camera->GetTransform()->SetRadianRotate({-0.100110, -2.716100, 0.000000});
		camera->AddComponent(new Camera(AspectRatio()));
		camera->AddComponent(new CameraController());
		gameObjects.push_back(std::move(camera));

		auto skySphere = std::make_unique<GameObject>(dxDevice.Get(), "Sky");
		skySphere->GetTransform()->SetScale({500, 500, 500});
		auto renderer = new Renderer();
		renderer->material = materials["sky"].get();
		renderer->mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->mesh->Submeshs["sphere"].IndexCount;
		renderer->StartIndexLocation = renderer->mesh->Submeshs["sphere"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->mesh->Submeshs["sphere"].BaseVertexLocation;
		skySphere->AddComponent(renderer);
		typedGameObjects[PsoType::SkyBox].push_back(skySphere.get());
		gameObjects.push_back(std::move(skySphere));

		auto quadRitem = std::make_unique<GameObject>(dxDevice.Get(), "Quad");
		renderer = new Renderer();
		renderer->material = materials["bricks"].get();
		renderer->mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->mesh->Submeshs["quad"].IndexCount;
		renderer->StartIndexLocation = renderer->mesh->Submeshs["quad"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->mesh->Submeshs["quad"].BaseVertexLocation;
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


		/*auto sphere = std::make_unique<GameObject>(dxDevice.Get(), "Skull");
		sphere->GetTransform()->SetPosition(Vector3{0, 1, -3} + Vector3::Backward);
		sphere->GetTransform()->SetScale({2, 2, 2});
		renderer = new Renderer();
		renderer->material = materials["bricks"].get();
		renderer->mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->mesh->Submeshs["sphere"].IndexCount;
		renderer->StartIndexLocation = renderer->mesh->Submeshs["sphere"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->mesh->Submeshs["sphere"].BaseVertexLocation;
		sphere->AddComponent(renderer);
		sphere->AddComponent(new ObjectMover());
		player = sphere.get();
		typedGameObjects[static_cast<int>(PsoType::Opaque)].push_back(sphere.get());
		gameObjects.push_back(std::move(sphere));*/

		auto sun1 = std::make_unique<GameObject>(dxDevice.Get(), "Directional Light");
		auto light = new Light(Directional);
		light->Direction({0.57735f, -0.57735f, 0.57735f});
		light->Strength({0.8f, 0.8f, 0.8f});
		sun1->AddComponent(light);
		gameObjects.push_back(std::move(sun1));

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

		auto commandQueue = this->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		auto cmdList = commandQueue->GetCommandList();

		auto man = std::make_unique<GameObject>(dxDevice.Get());
		man->GetTransform()->SetPosition(Vector3::Forward * 10 + (Vector3::Right * 5));
		man->GetTransform()->SetScale(Vector3::One * 0.25);
		man->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
		auto modelRenderer = new ModelRenderer();
		if (modelRenderer->AddModel(dxDevice.Get(), cmdList->GetGraphicsCommandList().Get(), "Data\\Objects\\Nanosuit\\Nanosuit.obj"))
		{
			for (UINT i = 0; i < modelRenderer->GetMeshesCount(); ++i)
			{
				modelRenderer->SetMeshMaterial(i, materials["seamless"].get());
			}

			if (modelRenderer->GetMeshesCount() > 0)
			{
				std::vector<std::string> names =
				{

					"Nanoglass",
					"Nanoleg",
					"Nanohand",
					"Nanoleg",
					"Nanoarm",
					"Nanohelm",
					"Nanobody",
				};
				for (UINT i = 0; i < names.size(); ++i)
				{
					modelRenderer->SetMeshMaterial(i, materials[names[i]].get());
				}
			}
			man->AddComponent(modelRenderer);
		}
		typedGameObjects[PsoType::Opaque].push_back(man.get());
		gameObjects.push_back(std::move(man));

		auto doomMan = std::make_unique<GameObject>(dxDevice.Get());
		doomMan->GetTransform()->SetPosition(Vector3::Forward * -10 + (Vector3::Right * 5));
		doomMan->GetTransform()->SetScale(Vector3::One * 0.02);
		doomMan->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
		modelRenderer = new ModelRenderer();
		if (modelRenderer->AddModel(dxDevice.Get(), cmdList->GetGraphicsCommandList().Get(), "Data\\Objects\\DoomSlayer\\doommarine.obj"))
		{
			if (modelRenderer->GetMeshesCount() > 0)
			{
				for (UINT i = 0; i < modelRenderer->GetMeshesCount(); ++i)
				{
					modelRenderer->SetMeshMaterial(i, materials["seamless"].get());
				}

				std::vector<std::string> doomNames =
				{
					"Doomlegs",
					"Doomtorso",
					"Doomcowl",
					"Doomarms",
					"Doomvisor",
					"Doomhelmet",
				};
				for (UINT i = 0; i < doomNames.size(); ++i)
				{
					modelRenderer->SetMeshMaterial(i, materials[doomNames[i]].get());
				}
			}


			doomMan->AddComponent(modelRenderer);
		}


		typedGameObjects[PsoType::Opaque].push_back(doomMan.get());
		gameObjects.push_back(std::move(doomMan));


		auto AtlasMan = std::make_unique<GameObject>(dxDevice.Get());
		AtlasMan->GetTransform()->SetPosition((Vector3::Right * 5) + (Vector3::Up * 2.4) + (Vector3::Forward * 5));
		AtlasMan->GetTransform()->SetScale(Vector3::One * 0.2);
		AtlasMan->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
		modelRenderer = new ModelRenderer();
		if (modelRenderer->AddModel(dxDevice.Get(), cmdList->GetGraphicsCommandList().Get(), "Data\\Objects\\Atlas\\Atlas.obj"))
		{
			if (modelRenderer->GetMeshesCount() > 0)
			{
				for (UINT i = 0; i < modelRenderer->GetMeshesCount(); ++i)
				{
					modelRenderer->SetMeshMaterial(i, materials["seamless"].get());
				}

				std::vector<std::string> AtlasNames =
				{

					"Atlasshell",
					"Atlasframe",
					"Atlaseye",
				};

				for (UINT i = 0; i < AtlasNames.size(); ++i)
				{
					modelRenderer->SetMeshMaterial(i, materials[AtlasNames[i]].get());
				}
			}


			AtlasMan->AddComponent(modelRenderer);
		}


		typedGameObjects[PsoType::Opaque].push_back(AtlasMan.get());
		gameObjects.push_back(std::move(AtlasMan));


		auto PBodyMan = std::make_unique<GameObject>(dxDevice.Get());
		PBodyMan->GetTransform()->SetPosition((Vector3::Right * 5) + (Vector3::Up * 2.4));
		PBodyMan->GetTransform()->SetScale(Vector3::One * 0.2);
		PBodyMan->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
		modelRenderer = new ModelRenderer();
		if (modelRenderer->AddModel(dxDevice.Get(), cmdList->GetGraphicsCommandList().Get(), "Data\\Objects\\P-Body\\P-Body.obj"))
		{
			if (modelRenderer->GetMeshesCount() > 0)
			{
				for (UINT i = 0; i < modelRenderer->GetMeshesCount(); ++i)
				{
					modelRenderer->SetMeshMaterial(i, materials["seamless"].get());
				}

				std::vector<std::string> PBodyNames =
				{

					"PBodyshell",
					"PBodyframe",
					"PBodyorange",
					"PBodyeye",
					"PBodyframe",
				};

				for (UINT i = 0; i < PBodyNames.size(); ++i)
				{
					modelRenderer->SetMeshMaterial(i, materials[PBodyNames[i]].get());
				}
			}


			PBodyMan->AddComponent(modelRenderer);
		}


		typedGameObjects[PsoType::Opaque].push_back(PBodyMan.get());
		gameObjects.push_back(std::move(PBodyMan));

		auto golem = std::make_unique<GameObject>(dxDevice.Get());
		golem->GetTransform()->SetPosition(Vector3::Forward * -5 + (Vector3::Right * 5));
		golem->GetTransform()->SetScale(Vector3::One * 0.5);
		golem->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
		modelRenderer = new ModelRenderer();
		if (modelRenderer->AddModel(dxDevice.Get(), cmdList->GetGraphicsCommandList().Get(), "Data\\Objects\\StoneGolem\\Stone.obj"))
		{
			if (modelRenderer->GetMeshesCount() > 0)
			{
				for (UINT i = 0; i < modelRenderer->GetMeshesCount(); ++i)
				{
					modelRenderer->SetMeshMaterial(i, materials["golemColor"].get());
				}
			}
			golem->AddComponent(modelRenderer);
		}
		typedGameObjects[PsoType::Opaque].push_back(golem.get());
		gameObjects.push_back(std::move(golem));


		auto box = std::make_unique<GameObject>(dxDevice.Get());
		box->GetTransform()->SetScale(Vector3(5.0f, 5.0f, 5.0f));
		box->GetTransform()->SetPosition(Vector3(0.0f, 0.25f, 0.0f) + (Vector3::Forward * -0.25));
		renderer = new Renderer();
		renderer->material = materials["water"].get();
		renderer->mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->mesh->Submeshs["box"].IndexCount;
		renderer->StartIndexLocation = renderer->mesh->Submeshs["box"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->mesh->Submeshs["box"].BaseVertexLocation;
		box->AddComponent(renderer);
		typedGameObjects[PsoType::Transparent].push_back(box.get());
		gameObjects.push_back(std::move(box));


		auto grid = std::make_unique<GameObject>(dxDevice.Get());
		grid->GetTransform()->SetScale(Vector3::One * 1.3);
		renderer = new Renderer();
		renderer->material = materials["tile0"].get();
		renderer->mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->mesh->Submeshs["grid"].IndexCount;
		renderer->StartIndexLocation = renderer->mesh->Submeshs["grid"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->mesh->Submeshs["grid"].BaseVertexLocation;
		XMStoreFloat4x4(&renderer->material->MatTransform, XMMatrixScaling(8.0f * 1.3, 8.0f * 1.3, 1.0f));
		grid->AddComponent(renderer);
		typedGameObjects[PsoType::Opaque].push_back(grid.get());
		gameObjects.push_back(std::move(grid));

		const XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
		for (int i = 0; i < 5; ++i)
		{
			auto leftCylRitem = std::make_unique<GameObject>(dxDevice.Get());
			auto rightCylRitem = std::make_unique<GameObject>(dxDevice.Get());
			auto rightSphere = std::make_unique<GameObject>(dxDevice.Get());
			auto leftSphere = std::make_unique<GameObject>(dxDevice.Get());

			leftCylRitem->GetTransform()->SetPosition(Vector3(+5.0f, 1.5f, -10.0f + i * 5.0f));
			XMStoreFloat4x4(&leftCylRitem->GetTransform()->TextureTransform, brickTexTransform);
			renderer = new Renderer();
			renderer->material = materials["bricks"].get();
			renderer->mesh = meshes["shapeMesh"].get();
			renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			renderer->IndexCount = renderer->mesh->Submeshs["cylinder"].IndexCount;
			renderer->StartIndexLocation = renderer->mesh->Submeshs["cylinder"].StartIndexLocation;
			renderer->BaseVertexLocation = renderer->mesh->Submeshs["cylinder"].BaseVertexLocation;
			leftCylRitem->AddComponent(renderer);

			rightCylRitem->GetTransform()->SetPosition(Vector3(-5.0f, 1.5f, -10.0f + i * 5.0f));
			XMStoreFloat4x4(&rightCylRitem->GetTransform()->TextureTransform, brickTexTransform);
			renderer = new Renderer();
			renderer->material = materials["bricks"].get();
			renderer->mesh = meshes["shapeMesh"].get();
			renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			renderer->IndexCount = renderer->mesh->Submeshs["cylinder"].IndexCount;
			renderer->StartIndexLocation = renderer->mesh->Submeshs["cylinder"].StartIndexLocation;
			renderer->BaseVertexLocation = renderer->mesh->Submeshs["cylinder"].BaseVertexLocation;
			rightCylRitem->AddComponent(renderer);


			rightSphere->GetTransform()->SetPosition(Vector3(-5.0f, 3.5f, -10.0f + i * 5.0f));
			rightSphere->GetTransform()->TextureTransform = brickTexTransform;
			renderer = new Renderer();
			renderer->material = materials["wirefence"].get();
			renderer->mesh = meshes["shapeMesh"].get();
			renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			renderer->IndexCount = renderer->mesh->Submeshs["sphere"].IndexCount;
			renderer->StartIndexLocation = renderer->mesh->Submeshs["sphere"].StartIndexLocation;
			renderer->BaseVertexLocation = renderer->mesh->Submeshs["sphere"].BaseVertexLocation;
			rightSphere->AddComponent(renderer);


			leftSphere->GetTransform()->SetPosition(Vector3(+5.0f, 3.5f, -10.0f + i * 5.0f));
			leftSphere->GetTransform()->TextureTransform = brickTexTransform;
			renderer = new Renderer();
			renderer->material = materials["wirefence"].get();
			renderer->mesh = meshes["shapeMesh"].get();
			renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
			renderer->IndexCount = renderer->mesh->Submeshs["sphere"].IndexCount;
			renderer->StartIndexLocation = renderer->mesh->Submeshs["sphere"].StartIndexLocation;
			renderer->BaseVertexLocation = renderer->mesh->Submeshs["sphere"].BaseVertexLocation;
			leftSphere->AddComponent(renderer);

			//typedGameObjects[PsoType::Opaque].push_back(leftCylRitem.get());
			typedGameObjects[PsoType::Opaque].push_back(rightCylRitem.get());
			typedGameObjects[PsoType::OpaqueAlphaDrop].push_back(rightSphere.get());
			//typedGameObjects[PsoType::OpaqueAlphaDrop].push_back(leftSphere.get());

			//gameObjects.push_back(std::move(leftCylRitem));
			gameObjects.push_back(std::move(rightCylRitem));
			gameObjects.push_back(std::move(rightSphere));
			//gameObjects.push_back(std::move(leftSphere));
		}

		commandQueue->ExecuteCommandList(cmdList);
	}

	void ShapesApp::DrawGameObjects(ID3D12GraphicsCommandList* cmdList, const custom_vector<GameObject*>& ritems)
	{
		// For each render item...
		for (auto& ri : ritems)
		{
			ri->Draw(cmdList);
		}
	}

	void ShapesApp::DrawSceneToShadowMap(ID3D12GraphicsCommandList* cmdList)
	{
		cmdList->RSSetViewports(1, &mShadowMap->Viewport());
		cmdList->RSSetScissorRects(1, &mShadowMap->ScissorRect());

		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		                                                                  D3D12_RESOURCE_STATE_GENERIC_READ,
		                                                                  D3D12_RESOURCE_STATE_DEPTH_WRITE));


		cmdList->ClearDepthStencilView(mShadowMap->Dsv(),
		                               D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

		cmdList->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());

		UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
		// Bind the pass constant buffer for the shadow map pass.
		auto passCB = currentFrameResource->PassConstantBuffer->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(StandardShaderSlot::CameraData, passCBAddress);

		cmdList->SetPipelineState(psos[PsoType::ShadowMapOpaque]->GetPSO().Get());

		DrawGameObjects(cmdList, typedGameObjects[PsoType::Opaque]);

		cmdList->SetPipelineState(psos[PsoType::ShadowMapOpaqueDrop]->GetPSO().Get());
		DrawGameObjects(cmdList, typedGameObjects[PsoType::OpaqueAlphaDrop]);


		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mShadowMap->Resource(),
		                                                                  D3D12_RESOURCE_STATE_DEPTH_WRITE,
		                                                                  D3D12_RESOURCE_STATE_GENERIC_READ));
	}

	void ShapesApp::DrawNormalsAndDepth(ID3D12GraphicsCommandList* cmdList)
	{
		cmdList->RSSetViewports(1, &MainWindow->GetViewPort());
		cmdList->RSSetScissorRects(1, &MainWindow->GetRect());

		auto normalMap = mSsao->NormalMap();
		auto normalMapRtv = mSsao->NormalMapRtv();

		// Change to RENDER_TARGET.
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
		                                                                  D3D12_RESOURCE_STATE_GENERIC_READ,
		                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET));

		// Clear the screen normal map and depth buffer.
		float clearValue[] = {0.0f, 0.0f, 1.0f, 0.0f};
		cmdList->ClearRenderTargetView(normalMapRtv, clearValue, 0, nullptr);
		cmdList->ClearDepthStencilView(MainWindow->GetDepthStencilView(),
		                               D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0,
		                               0, nullptr);

		// Specify the buffers we are going to render to.
		cmdList->OMSetRenderTargets(1, &normalMapRtv, true, &MainWindow->GetDepthStencilView());

		// Bind the constant buffer for this pass.
		auto passCB = currentFrameResource->PassConstantBuffer->Resource();
		cmdList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

		cmdList->SetPipelineState(psos[PsoType::DrawNormalsOpaque]->GetPSO().Get());

		DrawGameObjects(cmdList, typedGameObjects[PsoType::Opaque]);

		cmdList->SetPipelineState(psos[PsoType::DrawNormalsOpaqueDrop]->GetPSO().Get());
		DrawGameObjects(cmdList, typedGameObjects[PsoType::OpaqueAlphaDrop]);

		// Change back to GENERIC_READ so we can read the texture in a shader.
		cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(normalMap,
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
}
