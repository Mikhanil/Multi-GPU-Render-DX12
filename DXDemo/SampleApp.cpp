#include "SampleApp.h"

#include <DirectXTex.h>

#include <ppl.h>

#include "AssetsLoader.h"
#include "Camera.h"
#include "CameraController.h"
#include "GameObject.h"
#include "GCommandList.h"
#include "GCommandQueue.h"
#include "GeometryGenerator.h"
#include "GMemory.h"
#include "GResourceStateTracker.h"
#include "ModelRenderer.h"
#include "pix3.h"
#include "Renderer.h"
#include "SquidRoom.h"
#include "Window.h"

namespace DXLib
{
	SampleApp::SampleApp(HINSTANCE hInstance)
		: D3DApp(hInstance)
	{
		mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
		mSceneBounds.Radius = sqrtf(15 * 15.0f + 15.0f * 15.0f);

		for (int i = 0; i < PsoType::Count; ++i)
		{
			typedGameObjects.push_back(DXAllocator::CreateVector<GameObject*>());
		}
	}

	SampleApp::~SampleApp()
	{
		Flush();
	}

	void SampleApp::GeneratedMipMap()
	{
		std::vector<GTexture*> generatedMipTextures;

		auto textures = loader.GetTextures();
		
		for (auto&& texture : textures)
		{
			texture.second->ClearTrack();

			/*ТОлько те что можно использовать как UAV*/
			if (texture.second->GetD3D12Resource()->GetDesc().Flags != D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)
				continue;

			if (!texture.second->HasMipMap)
			{
				generatedMipTextures.push_back(texture.second.get());
			}
		}


		auto graphicQueue = GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		auto graphicList = graphicQueue->GetCommandList();

		for (auto&& texture : generatedMipTextures)
		{
			graphicList->TransitionBarrier(texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}
		graphicQueue->WaitForFenceValue( graphicQueue->ExecuteCommandList(graphicList));

		
		const auto computeQueue = this->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE);
		auto computeList = computeQueue->GetCommandList();		
		GTexture::GenerateMipMaps(computeList, generatedMipTextures.data(), generatedMipTextures.size());
		computeQueue->WaitForFenceValue(computeQueue->ExecuteCommandList(computeList));


		graphicList = graphicQueue->GetCommandList();
		for (auto&& texture : generatedMipTextures)
		{
			graphicList->TransitionBarrier(texture->GetD3D12Resource(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		}
		graphicQueue->WaitForFenceValue(graphicQueue->ExecuteCommandList(graphicList));


		for (auto&& pair : textures)
		{
			pair.second->ClearTrack();
		}

	}

	bool SampleApp::Initialize()
	{
		if (!D3DApp::Initialize())
			return false;

		auto commandQueue = this->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		auto cmdList = commandQueue->GetCommandList();

		mShadowMap = std::make_unique<ShadowMap>(4096, 4096);

		mSsao = std::make_unique<Ssao>(
			dxDevice.Get(),
			cmdList,
			MainWindow->GetClientWidth(), MainWindow->GetClientHeight());

		commandQueue->WaitForFenceValue(commandQueue->ExecuteCommandList(cmdList));
		
		LoadTextures(cmdList);
		LoadModels();
		GeneratedMipMap();


		BuildTexturesHeap();
		BuildShadersAndInputLayout();
		BuildRootSignature();
		BuildSsaoRootSignature();
		BuildShapeGeometry();
		BuildPSOs();
		BuildMaterials();
		BuildGameObjects();
		BuildFrameResources();
		SortGO();

		mSsao->SetPSOs(*psos[PsoType::Ssao], *psos[PsoType::SsaoBlur]);

		commandQueue->Flush();

		loader.ClearTrackedObjects();
		
		return true;
	}

	LRESULT SampleApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
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

	void SampleApp::OnResize()
	{
		D3DApp::OnResize();

		if (camera != nullptr)
		{
			camera->SetAspectRatio(AspectRatio());
		}

		if (mSsao != nullptr)
		{
			mSsao->OnResize(MainWindow->GetClientWidth(), MainWindow->GetClientHeight());
			mSsao->RebuildDescriptors(MainWindow->GetDepthStencilBuffer());
		}
	}

	void SampleApp::Update(const GameTimer& gt)
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


		UpdateGameObjects(gt);
		UpdateMaterial(gt);
		UpdateShadowTransform(gt);
		UpdateMainPassCB(gt);
		UpdateShadowPassCB(gt);
		UpdateSsaoCB(gt);
	}

	void SampleApp::Draw(const GameTimer& gt)
	{
		if (isResizing) return;

		auto commandQueue = GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);

		auto cmdList = commandQueue->GetCommandList();

		cmdList->SetGMemory(&srvHeap);
		cmdList->SetRootSignature(rootSignature.get());

		PIXBeginEvent(commandQueue->GetD3D12CommandQueue().Get(), 0, L"Prepare Render 3D");


		/*Bind all materials*/
		auto matBuffer = currentFrameResource->MaterialBuffer->Resource();
		cmdList->SetRootShaderResourceView(StandardShaderSlot::MaterialData, matBuffer->GetGPUVirtualAddress());


		/*Bind all Diffuse Textures*/
		cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap, &srvHeap);
		PIXEndEvent(commandQueue->GetD3D12CommandQueue().Get());


		PIXBeginEvent(commandQueue->GetD3D12CommandQueue().Get(), 0, L"Render 3D");

		PIXBeginEvent(commandQueue->GetD3D12CommandQueue().Get(), 0, L"Shadow Map Pass");
		DrawSceneToShadowMap(cmdList);
		PIXEndEvent(commandQueue->GetD3D12CommandQueue().Get());

		PIXBeginEvent(commandQueue->GetD3D12CommandQueue().Get(), 0, L"Normal and Depth Pass");
		DrawNormalsAndDepth(cmdList);
		PIXEndEvent(commandQueue->GetD3D12CommandQueue().Get());

		PIXBeginEvent(commandQueue->GetD3D12CommandQueue().Get(), 0, L"Compute SSAO");

		cmdList->SetRootSignature(ssaoRootSignature.get());
		mSsao->ComputeSsao(cmdList, currentFrameResource, 3);
		
		PIXEndEvent(commandQueue->GetD3D12CommandQueue().Get());

		PIXBeginEvent(commandQueue->GetD3D12CommandQueue().Get(), 0, L"Main Pass");

		cmdList->SetRootSignature(rootSignature.get());

		cmdList->SetViewports(&MainWindow->GetViewPort(), 1);
		cmdList->SetScissorRects(&MainWindow->GetRect(), 1);

		cmdList->TransitionBarrier((MainWindow->GetCurrentBackBuffer()), D3D12_RESOURCE_STATE_RENDER_TARGET);
		cmdList->FlushResourceBarriers();

		cmdList->ClearRenderTarget(MainWindow->GetRTVMemory(), MainWindow->GetCurrentBackBufferIndex());


		cmdList->SetRenderTargets(1, MainWindow->GetRTVMemory(), MainWindow->GetCurrentBackBufferIndex(),
		                          MainWindow->GetDSVMemory());


		auto passCB = currentFrameResource->PassConstantBuffer->Resource();

		cmdList->
			SetRootConstantBufferView(StandardShaderSlot::CameraData, passCB->GetGPUVirtualAddress());

		cmdList->SetRootDescriptorTable(StandardShaderSlot::ShadowMap, mShadowMap->GetSrvMemory());
		cmdList->SetRootDescriptorTable(StandardShaderSlot::SsaoMap, mSsao->AmbientMapSrv(), 0);

		/*Bind all Diffuse Textures*/
		cmdList->SetRootDescriptorTable(StandardShaderSlot::TexturesMap,
		                                &srvHeap);


		cmdList->SetPipelineState(*psos[PsoType::SkyBox]);
		DrawGameObjects(cmdList, typedGameObjects[static_cast<int>(PsoType::SkyBox)]);

		cmdList->SetPipelineState(*psos[PsoType::Opaque]);
		DrawGameObjects(cmdList, typedGameObjects[static_cast<int>(PsoType::Opaque)]);

		cmdList->SetPipelineState(*psos[PsoType::OpaqueAlphaDrop]);
		DrawGameObjects(cmdList, typedGameObjects[static_cast<int>(PsoType::OpaqueAlphaDrop)]);

		cmdList->SetPipelineState(*psos[PsoType::Transparent]);
		DrawGameObjects(cmdList, typedGameObjects[static_cast<int>(PsoType::Transparent)]);

		if (isDebug)
		{
			cmdList->SetPipelineState(*psos[PsoType::Debug]);
			DrawGameObjects(cmdList, typedGameObjects[static_cast<int>(PsoType::Debug)]);
		}

		cmdList->TransitionBarrier(MainWindow->GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT);
		cmdList->FlushResourceBarriers();
		
		currentFrameResource->FenceValue = commandQueue->ExecuteCommandList(cmdList);

		PIXEndEvent(commandQueue->GetD3D12CommandQueue().Get());
		PIXEndEvent(commandQueue->GetD3D12CommandQueue().Get());

		currBackBufferIndex = MainWindow->Present();

	}
	
	void SampleApp::UpdateGameObjects(const GameTimer& gt)
	{
		const float dt = gt.DeltaTime();

		for (auto& e : gameObjects)
		{
			e->Update();
		}
	}

	void SampleApp::UpdateMaterial(const GameTimer& gt)
	{
		auto currentMaterialBuffer = currentFrameResource->MaterialBuffer.get();

		for (auto&& material : loader.GetMaterials())
		{
			material.second->Update();
			auto constantData = material.second->GetMaterialConstantData();
			currentMaterialBuffer->CopyData(material.second->GetIndex(), constantData);
		}
	}

	void SampleApp::UpdateShadowTransform(const GameTimer& gt)
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

	void SampleApp::UpdateShadowPassCB(const GameTimer& gt)
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

	void SampleApp::UpdateMainPassCB(const GameTimer& gt)
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
		mainPassCB.debugMap = showPathMap;
		mainPassCB.View = view.Transpose();
		mainPassCB.InvView = invView.Transpose();
		mainPassCB.Proj = proj.Transpose();
		mainPassCB.InvProj = invProj.Transpose();
		mainPassCB.ViewProj = viewProj.Transpose();
		mainPassCB.InvViewProj = invViewProj.Transpose();
		mainPassCB.ViewProjTex = viewProjTex.Transpose();
		mainPassCB.ShadowTransform = shadowTransform.Transpose();
		mainPassCB.EyePosW = camera->gameObject->GetTransform()->GetPosition();
		mainPassCB.RenderTargetSize = XMFLOAT2(static_cast<float>(MainWindow->GetClientWidth()),
		                                       static_cast<float>(MainWindow->GetClientHeight()));
		mainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mainPassCB.RenderTargetSize.x,
		                                          1.0f / mainPassCB.RenderTargetSize.y);
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

	void SampleApp::UpdateSsaoCB(const GameTimer& gt)
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


	void SampleApp::LoadStudyTexture(std::shared_ptr<GCommandList> cmdList)
	{
		auto bricksTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\bricks2.dds", cmdList);
		bricksTex->SetName(L"bricksTex");
		loader.AddTexture(bricksTex);

		auto stoneTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\stone.dds", cmdList);
		stoneTex->SetName(L"stoneTex");
		loader.AddTexture(stoneTex);

		auto tileTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\tile.dds", cmdList);
		tileTex->SetName(L"tileTex");
		loader.AddTexture(tileTex);

		auto fenceTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\WireFence.dds", cmdList);
		fenceTex->SetName(L"fenceTex");
		loader.AddTexture(fenceTex);

		auto waterTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\water1.dds", cmdList);
		waterTex->SetName(L"waterTex");
		loader.AddTexture(waterTex);

		auto skyTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\skymap.dds", cmdList);
		skyTex->SetName(L"skyTex");
		loader.AddTexture(skyTex);

		auto grassTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\grass.dds", cmdList);
		grassTex->SetName(L"grassTex");
		loader.AddTexture(grassTex);

		auto treeArrayTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\treeArray2.dds", cmdList);
		treeArrayTex->SetName(L"treeArrayTex");
		loader.AddTexture(treeArrayTex);

		auto seamless = GTexture::LoadTextureFromFile(L"Data\\Textures\\seamless_grass.jpg", cmdList);
		seamless->SetName(L"seamless");
		loader.AddTexture(seamless);


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
			auto texture = GTexture::LoadTextureFromFile(texNormalFilenames[i], cmdList, TextureUsage::Normalmap);
			texture->SetName(texNormalNames[i]);
			loader.AddTexture(texture);
		}
	}
		
	
	void SampleApp::LoadModels()
	{
		std::string modelPath[] = {
			"Data\\Objects\\Nanosuit\\Nanosuit.obj",
			"Data\\Objects\\DoomSlayer\\doommarine.obj",
			"Data\\Objects\\Atlas\\Atlas.obj",
			"Data\\Objects\\P-Body\\P-Body.obj",
			"Data\\Objects\\StoneGolem\\Stone.obj",
		};
		
		
		auto queue = GetCommandQueue();

		for (auto && path : modelPath)
		{
			auto model = loader.GetOrCreateModelFromFile(queue, path);			
			models.push_back(model);
		}

		queue->Flush();

		
	}
	
	void SampleApp::LoadTextures(std::shared_ptr<GCommandList> cmdList)
	{
		auto queue = GetCommandQueue();
		
		auto graphicCmdList = GetCommandQueue()->GetCommandList();		
		LoadStudyTexture(graphicCmdList);		;		
		queue->WaitForFenceValue(queue->ExecuteCommandList(graphicCmdList));

	}

	void SampleApp::BuildRootSignature()
	{
		rootSignature = std::make_unique<GRootSignature>();

		CD3DX12_DESCRIPTOR_RANGE texParam[4];
		texParam[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SkyMap - 3, 0); //SkyMap
		texParam[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::ShadowMap - 3, 0); //ShadowMap
		texParam[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, StandardShaderSlot::SsaoMap - 3, 0); //SsaoMap
		texParam[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, loader.GetLoadTexturesCount(), StandardShaderSlot::TexturesMap - 3, 0);


		rootSignature->AddConstantBufferParameter(0);
		rootSignature->AddConstantBufferParameter(1);
		rootSignature->AddShaderResourceView(0, 1);
		rootSignature->AddDescriptorParameter(&texParam[0], 1, D3D12_SHADER_VISIBILITY_PIXEL);
		rootSignature->AddDescriptorParameter(&texParam[1], 1, D3D12_SHADER_VISIBILITY_PIXEL);
		rootSignature->AddDescriptorParameter(&texParam[2], 1, D3D12_SHADER_VISIBILITY_PIXEL);
		rootSignature->AddDescriptorParameter(&texParam[3], 1, D3D12_SHADER_VISIBILITY_PIXEL);

		rootSignature->Initialize();
	}
		
	Keyboard* SampleApp::GetKeyboard()
	{
		return &keyboard;
	}

	Mouse* SampleApp::GetMouse()
	{
		return &mouse;
	}

	Camera* SampleApp::GetMainCamera() const
	{
		return camera.get();
	}
	
	void SampleApp::BuildSsaoRootSignature()
	{
		ssaoRootSignature = std::make_unique<GRootSignature>();

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

		ssaoRootSignature->Initialize();
	}

	void SampleApp::BuildTexturesHeap()
	{
		srvHeap = std::move(DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,  loader.GetLoadTexturesCount()));


		mShadowMap->BuildDescriptors();

		mSsao->BuildDescriptors(MainWindow->GetDepthStencilBuffer());
	}

	void SampleApp::BuildShadersAndInputLayout()
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
			std::make_unique<GShader>(L"Shaders\\Default.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
		shaders["AlphaDrop"] = std::move(
			std::make_unique<GShader>(L"Shaders\\Default.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));
		shaders["shadowVS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\Shadows.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
		shaders["shadowOpaquePS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\Shadows.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));
		shaders["shadowOpaqueDropPS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\Shadows.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));
		shaders["OpaquePixel"] = std::move(
			std::make_unique<GShader>(L"Shaders\\Default.hlsl", PixelShader, defines, "PS", "ps_5_1"));
		shaders["SkyBoxVertex"] = std::move(
			std::make_unique<GShader>(L"Shaders\\SkyBoxShader.hlsl", VertexShader, defines, "SKYMAP_VS", "vs_5_1"));
		shaders["SkyBoxPixel"] = std::move(
			std::make_unique<GShader>(L"Shaders\\SkyBoxShader.hlsl", PixelShader, defines, "SKYMAP_PS", "ps_5_1"));

		shaders["treeSpriteVS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\TreeSprite.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
		shaders["treeSpriteGS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\TreeSprite.hlsl", GeometryShader, nullptr, "GS", "gs_5_1"));
		shaders["treeSpritePS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\TreeSprite.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));


		shaders["drawNormalsVS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\DrawNormals.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
		shaders["drawNormalsPS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\DrawNormals.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));
		shaders["drawNormalsAlphaDropPS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\DrawNormals.hlsl", PixelShader, alphaTestDefines, "PS", "ps_5_1"));


		shaders["ssaoVS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\Ssao.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
		shaders["ssaoPS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\Ssao.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

		shaders["ssaoBlurVS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\SsaoBlur.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
		shaders["ssaoBlurPS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\SsaoBlur.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

		shaders["ssaoDebugVS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\SsaoDebug.hlsl", VertexShader, nullptr, "VS", "vs_5_1"));
		shaders["ssaoDebugPS"] = std::move(
			std::make_unique<GShader>(L"Shaders\\SsaoDebug.hlsl", PixelShader, nullptr, "PS", "ps_5_1"));

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

	void SampleApp::BuildShapeGeometry()
	{
		auto queue = GetCommandQueue();
		auto cmdList = queue->GetCommandList();

		const auto sphereModel = loader.GenerateSphere(cmdList);		
		genModels[L"sphere"] = (sphereModel);

		const auto quadModel = loader.GenerateQuad(cmdList);
		genModels[L"quad"] = (quadModel);

		queue->WaitForFenceValue(queue->ExecuteCommandList(cmdList));	
	}

	void SampleApp::BuildPSOs()
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


		auto opaquePSO = std::make_unique<GraphicPSO>();
		opaquePSO->SetPsoDesc(basePsoDesc);
		depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_EQUAL;
		depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		opaquePSO->SetDepthStencilState(depthStencilDesc);

		auto alphaDropPso = std::make_unique<GraphicPSO>(PsoType::OpaqueAlphaDrop);
		alphaDropPso->SetPsoDesc(opaquePSO->GetPsoDescription());
		alphaDropPso->SetShader(shaders["AlphaDrop"].get());
		rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
		alphaDropPso->SetRasterizationState(rasterizedDesc);


		auto shadowMapPSO = std::make_unique<GraphicPSO>(PsoType::ShadowMapOpaque);
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


		auto shadowMapDropPSO = std::make_unique<GraphicPSO>(PsoType::ShadowMapOpaqueDrop);
		shadowMapDropPSO->SetPsoDesc(shadowMapPSO->GetPsoDescription());
		shadowMapDropPSO->SetShader(shaders["shadowOpaqueDropPS"].get());


		auto drawNormalsPso = std::make_unique<GraphicPSO>(PsoType::DrawNormalsOpaque);
		drawNormalsPso->SetPsoDesc(basePsoDesc);
		drawNormalsPso->SetShader(shaders["drawNormalsVS"].get());
		drawNormalsPso->SetShader(shaders["drawNormalsPS"].get());
		drawNormalsPso->SetRTVFormat(0, Ssao::NormalMapFormat);
		drawNormalsPso->SetSampleCount(1);
		drawNormalsPso->SetSampleQuality(0);
		drawNormalsPso->SetDSVFormat(depthStencilFormat);

		auto drawNormalsDropPso = std::make_unique<GraphicPSO>(PsoType::DrawNormalsOpaqueDrop);
		drawNormalsDropPso->SetPsoDesc(drawNormalsPso->GetPsoDescription());
		drawNormalsDropPso->SetShader(shaders["drawNormalsAlphaDropPS"].get());
		rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
		drawNormalsDropPso->SetRasterizationState(rasterizedDesc);

		auto ssaoPSO = std::make_unique<GraphicPSO>(PsoType::Ssao);
		ssaoPSO->SetPsoDesc(basePsoDesc);
		ssaoPSO->SetInputLayout({nullptr, 0});
		ssaoPSO->SetRootSignature(ssaoRootSignature->GetRootSignature().Get());
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

		auto ssaoBlurPSO = std::make_unique<GraphicPSO>(PsoType::SsaoBlur);
		ssaoBlurPSO->SetPsoDesc(ssaoPSO->GetPsoDescription());
		ssaoBlurPSO->SetShader(shaders["ssaoBlurVS"].get());
		ssaoBlurPSO->SetShader(shaders["ssaoBlurPS"].get());

		auto skyBoxPSO = std::make_unique<GraphicPSO>(PsoType::SkyBox);
		skyBoxPSO->SetPsoDesc(basePsoDesc);
		skyBoxPSO->SetShader(shaders["SkyBoxVertex"].get());
		skyBoxPSO->SetShader(shaders["SkyBoxPixel"].get());

		depthStencilDesc = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		skyBoxPSO->SetDepthStencilState(depthStencilDesc);
		rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
		skyBoxPSO->SetRasterizationState(rasterizedDesc);


		auto transperentPSO = std::make_unique<GraphicPSO>(PsoType::Transparent);
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


		auto treeSprite = std::make_unique<GraphicPSO>(PsoType::AlphaSprites);
		treeSprite->SetPsoDesc(basePsoDesc);
		treeSprite->SetShader(shaders["treeSpriteVS"].get());
		treeSprite->SetShader(shaders["treeSpriteGS"].get());
		treeSprite->SetShader(shaders["treeSpritePS"].get());
		treeSprite->SetInputLayout({treeSpriteInputLayout.data(), static_cast<UINT>(treeSpriteInputLayout.size())});
		treeSprite->SetPrimitiveType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT);
		rasterizedDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		rasterizedDesc.CullMode = D3D12_CULL_MODE_NONE;
		treeSprite->SetRasterizationState(rasterizedDesc);

		auto debugPso = std::make_unique<GraphicPSO>(PsoType::Debug);
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

	void SampleApp::BuildFrameResources()
	{
		for (int i = 0; i < globalCountFrameResources; ++i)
		{
			frameResources.push_back(
				std::make_unique<FrameResource>(2, gameObjects.size(), loader.GetMaterials().size() ));
		}
	}

	void SampleApp::BuildMaterials()
	{		
		auto seamless = std::make_shared<Material>(L"seamless", PsoType::Opaque);
		seamless->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
		seamless->Roughness = 0.1f;
		seamless->SetDiffuseTexture(loader.GetTexture(L"seamless"));
		seamless->SetNormalMap(loader.GetTexture(L"defaultNormalMap"));
		loader.AddMaterial(seamless);

	

		auto skyBox = std::make_shared<Material>(L"sky", PsoType::SkyBox);
		skyBox->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		skyBox->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
		skyBox->Roughness = 1.0f;
		skyBox->SetDiffuseTexture(loader.GetTexture(L"skyTex"));
		skyBox->SetNormalMap(loader.GetTexture(L"defaultNormalMap"));
		loader.AddMaterial(skyBox);
		

		auto materials = loader.GetMaterials();
		
		for (auto pair : materials)
		{
			pair.second->InitMaterial(srvHeap);
		}
	}

	void SampleApp::BuildGameObjects()
	{
		auto camera = std::make_unique<GameObject>(dxDevice.Get(), "MainCamera");
		camera->GetTransform()->SetPosition({-3.667396, 3.027442, -12.024042});
		camera->GetTransform()->SetRadianRotate({-0.100110, -2.716100, 0.000000});
		camera->AddComponent(new Camera(AspectRatio()));
		camera->AddComponent(new CameraController());
		gameObjects.push_back(std::move(camera));

		auto skySphere = std::make_unique<GameObject>(dxDevice.Get(), "Sky");
		skySphere->GetTransform()->SetScale({500, 500, 500});
		auto renderer = new ModelRenderer();
		renderer->material = loader.GetMaterials(L"sky").get();
		renderer->AddModel(genModels[L"sphere"]);
		renderer->SetMeshMaterial(0, loader.GetMaterials(L"sky"));
		skySphere->AddComponent(renderer);
		typedGameObjects[PsoType::SkyBox].push_back(skySphere.get());
		gameObjects.push_back(std::move(skySphere));

		auto quadRitem = std::make_unique<GameObject>(dxDevice.Get(), "Quad");
		renderer = new ModelRenderer();
		renderer->material = loader.GetMaterials(L"seamless").get();
		renderer->AddModel(genModels[L"quad"]);
		renderer->SetMeshMaterial(0, loader.GetMaterials(L"seamless"));
		quadRitem->AddComponent(renderer);
		typedGameObjects[PsoType::Debug].push_back(quadRitem.get());
		gameObjects.push_back(std::move(quadRitem));
		
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

		for (int i = 0; i < models.size(); ++i)
		{
			auto mod = models[i];			
			auto man = std::make_unique<GameObject>(dxDevice.Get());
			man->GetTransform()->SetPosition(Vector3::Forward * ( -30 + (10 * i)) );
			man->GetTransform()->SetScale(Vector3::One * 0.25);
			man->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
			renderer = new ModelRenderer();
			renderer->AddModel(mod);
			man->AddComponent(renderer);
			typedGameObjects[PsoType::Opaque].push_back(man.get());
			gameObjects.push_back(std::move(man));
		}									
	}

	void SampleApp::DrawGameObjects(std::shared_ptr<GCommandList> cmdList, const custom_vector<GameObject*>& ritems)
	{
		// For each render item...
		for (auto& ri : ritems)
		{
			ri->Draw(cmdList);
		}
	}

	void SampleApp::DrawSceneToShadowMap(std::shared_ptr<GCommandList> list)
	{
		list->SetViewports(&mShadowMap->Viewport(), 1);
		list->SetScissorRects(&mShadowMap->ScissorRect(), 1);

		list->TransitionBarrier(mShadowMap->GetTexture(), D3D12_RESOURCE_STATE_DEPTH_WRITE);
		list->FlushResourceBarriers();


		list->ClearDepthStencil(mShadowMap->GetDsvMemory(), 0,
		                        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

		list->SetRenderTargets(0, nullptr, false, mShadowMap->GetDsvMemory());

		UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
		auto passCB = currentFrameResource->PassConstantBuffer->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = passCB->GetGPUVirtualAddress() + 1 * passCBByteSize;
		list->SetRootConstantBufferView(StandardShaderSlot::CameraData, passCBAddress);

		list->SetPipelineState(*psos[PsoType::ShadowMapOpaque]);

		DrawGameObjects(list, typedGameObjects[PsoType::Opaque]);

		list->SetPipelineState(*psos[PsoType::ShadowMapOpaqueDrop]);
		DrawGameObjects(list, typedGameObjects[PsoType::OpaqueAlphaDrop]);

		list->TransitionBarrier(mShadowMap->GetTexture(), D3D12_RESOURCE_STATE_COMMON);
		list->FlushResourceBarriers();
	}

	void SampleApp::DrawNormalsAndDepth(std::shared_ptr<GCommandList> list)
	{
		list->SetViewports(&MainWindow->GetViewPort(), 1);
		list->SetScissorRects(&MainWindow->GetRect(), 1);

		auto normalMap = mSsao->NormalMap();
		auto normalMapRtv = mSsao->NormalMapRtv();

		list->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_RENDER_TARGET);
		list->FlushResourceBarriers();

		float clearValue[] = {0.0f, 0.0f, 1.0f, 0.0f};
		list->ClearRenderTarget(normalMapRtv, 0, clearValue);
		list->ClearDepthStencil(MainWindow->GetDSVMemory(), 0,
		                        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0);

		list->SetRenderTargets(1, normalMapRtv, 0, MainWindow->GetDSVMemory());

		auto passCB = currentFrameResource->PassConstantBuffer->Resource();
		list->SetRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

		list->SetPipelineState(*psos[PsoType::DrawNormalsOpaque]);
		DrawGameObjects(list, typedGameObjects[PsoType::Opaque]);
		list->SetPipelineState(*psos[PsoType::DrawNormalsOpaqueDrop]);
		DrawGameObjects(list, typedGameObjects[PsoType::OpaqueAlphaDrop]);


		list->TransitionBarrier(normalMap, D3D12_RESOURCE_STATE_COMMON);
		list->FlushResourceBarriers();
	}

	void SampleApp::SortGO()
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

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> SampleApp::GetStaticSamplers()
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
