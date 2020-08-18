#include "SampleApp.h"

#include <DirectXTex.h>

#include <ppl.h>

#include "BinAssetsLoader.h"
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
		LoadSquidModels(cmdList);
	


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
		if (computeSsao)
		{
			cmdList->SetRootSignature(ssaoRootSignature.get());
			mSsao->ComputeSsao(cmdList, currentFrameResource, 3);
		}
		else
		{
			mSsao->ClearAmbiantMap(cmdList);
		}
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

		if (ShowAmbiantMap)
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

		Mesh::isDebug = true;
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

		for (auto&& material : materials)
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

	void SampleApp::LoadDoomSlayerTexture(std::shared_ptr<GCommandList> cmdList)
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
			auto texture = GTexture::LoadTextureFromFile(doomFolder + doomTextures[i], cmdList, TextureUsage::Diffuse);
			texture->SetName(doomNames[i]);

			auto normal = GTexture::LoadTextureFromFile(doomFolder + doomNormals[i], cmdList,
			                                           TextureUsage::Normalmap);
			normal->SetName(doomNames[i].append(L"Normal"));

			textures[texture->GetName()] = std::move(texture);
			textures[normal->GetName()] = std::move(normal);
		}
	}

	void SampleApp::LoadStudyTexture(std::shared_ptr<GCommandList> cmdList)
	{
		auto bricksTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\bricks2.dds", cmdList);
		bricksTex->SetName(L"bricksTex");
		textures[bricksTex->GetName()] = std::move(bricksTex);

		auto stoneTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\stone.dds", cmdList);
		stoneTex->SetName(L"stoneTex");
		textures[stoneTex->GetName()] = std::move(stoneTex);

		auto tileTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\tile.dds", cmdList);
		tileTex->SetName(L"tileTex");
		textures[tileTex->GetName()] = std::move(tileTex);

		auto fenceTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\WireFence.dds", cmdList);
		fenceTex->SetName(L"fenceTex");
		textures[fenceTex->GetName()] = std::move(fenceTex);

		auto waterTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\water1.dds", cmdList);
		waterTex->SetName(L"waterTex");
		textures[waterTex->GetName()] = std::move(waterTex);

		auto skyTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\skymap.dds", cmdList);
		skyTex->SetName(L"skyTex");
		textures[skyTex->GetName()] = std::move(skyTex);

		auto grassTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\grass.dds", cmdList);
		grassTex->SetName(L"grassTex");
		textures[grassTex->GetName()] = std::move(grassTex);

		auto treeArrayTex = GTexture::LoadTextureFromFile(L"Data\\Textures\\treeArray2.dds", cmdList);
		treeArrayTex->SetName(L"treeArrayTex");
		textures[treeArrayTex->GetName()] = std::move(treeArrayTex);

		auto seamless = GTexture::LoadTextureFromFile(L"Data\\Textures\\seamless_grass.jpg", cmdList);
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
			auto texture = GTexture::LoadTextureFromFile(texNormalFilenames[i], cmdList, TextureUsage::Normalmap);
			texture->SetName(texNormalNames[i]);
			textures[texture->GetName()] = std::move(texture);
		}
	}

	void SampleApp::LoadNanosuitTexture(std::shared_ptr<GCommandList> cmdList)
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
			auto texture = GTexture::LoadTextureFromFile(nanoFolder + nanoTextures[i], cmdList, TextureUsage::Diffuse);
			texture->SetName(nanoNames[i]);
			auto normal = GTexture::LoadTextureFromFile(nanoFolder + nanoNormals[i], cmdList,
			                                           TextureUsage::Normalmap);
			normal->SetName(nanoNames[i].append(L"Normal"));
			textures[texture->GetName()] = std::move(texture);
			textures[normal->GetName()] = std::move(normal);
		}
	}

	void SampleApp::LoadAtlasTexture(std::shared_ptr<GCommandList> cmdList)
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
			auto texture = GTexture::LoadTextureFromFile(atlasFolder + AtlasTextures[i], cmdList, TextureUsage::Diffuse);
			texture->SetName(AtlasNames[i]);
			textures[texture->GetName()] = std::move(texture);
		}
	}

	void SampleApp::LoadPBodyTexture(std::shared_ptr<GCommandList> cmdList)
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
			auto texture = GTexture::LoadTextureFromFile(PBodyFolder + PBodyTextures[i], cmdList,
			                                            TextureUsage::Diffuse);
			texture->SetName(PBodyNames[i]);
			textures[texture->GetName()] = std::move(texture);
		}
	}

	void SampleApp::LoadGolemTexture(std::shared_ptr<GCommandList> cmdList)
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
			auto texture = GTexture::LoadTextureFromFile(mechFolder + mechTextures[i], cmdList, TextureUsage::Diffuse);
			texture->SetName(mechNames[i]);
			auto normal = GTexture::LoadTextureFromFile(mechFolder + mechNormals[i], cmdList,
			                                           TextureUsage::Normalmap);
			normal->SetName(mechNames[i].append(L"Normal"));
			textures[texture->GetName()] = std::move(texture);
			textures[normal->GetName()] = std::move(normal);
		}
	}

	void SampleApp::LoadBinTextures(std::shared_ptr<GCommandList> cmdList, const UINT8* assetData)
	{		
		const UINT textureCount = _countof(SampleAssets::Textures);

		for (int i = 0; i < textureCount; ++i)
		{
			// Describe and create a Texture2D.
			const SampleAssets::TextureResource& tex = SampleAssets::Textures[i];

			textures[tex.FileName] = std::move(BinAssetsLoader::LoadTexturesFromBin(assetData, tex, cmdList));
		}
		
	}

	void SampleApp::LoadSquidModels(std::shared_ptr<GCommandList> cmdLit)
	{
		// Load scene assets.
		UINT fileSize = 0;
		UINT8* pAssetData;
		ThrowIfFailed(ReadDataFromFile((SampleAssets::DataFileName), &pAssetData, &fileSize));
				
		squidVertexBuffer = std::make_shared<GBuffer>(std::move( GBuffer::CreateBuffer(cmdLit, pAssetData + SampleAssets::VertexDataOffset, sizeof(Vertex), SampleAssets::VertexDataSize / sizeof(Vertex), L"SquidVertexBuffer")));

		squidIndexBuffer = std::make_shared<GBuffer>(std::move(GBuffer::CreateBuffer(cmdLit, pAssetData + SampleAssets::IndexDataOffset, sizeof(UINT), SampleAssets::IndexDataSize / sizeof(UINT32), L"SquidindexBuffer")));
		
		cmdLit->TransitionBarrier(squidVertexBuffer->GetD3D12Resource(), D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
		cmdLit->FlushResourceBarriers();

		const UINT meshCount = _countof(SampleAssets::Draws);
				
		
		
		for (int i = 0; i < meshCount; ++i)
		{
			const auto& msh = SampleAssets::Draws[i];

			auto mesh = BinAssetsLoader::LoadModelFromBin(pAssetData, msh, squidVertexBuffer, squidIndexBuffer, cmdLit);
						
			auto it = models.find(msh.FileName);
			if(it == models.end())
			{
				models[msh.FileName] = std::move(std::make_shared<Model>(msh.FileName));
				it = models.find(msh.FileName);
			}

			meshesModel.push_back(std::move(mesh));
						
			it->second->AddMesh(meshesModel.back());
		}
		
		free(pAssetData);		
	}
	
	void SampleApp::LoadTextures(std::shared_ptr<GCommandList> cmdList)
	{
		auto queue = GetCommandQueue();
		
		auto graphicCmdList = GetCommandQueue()->GetCommandList();		

		// Load scene assets.
		UINT fileSize = 0;
		UINT8* pAssetData;
		ThrowIfFailed(ReadDataFromFile((SampleAssets::DataFileName), &pAssetData, &fileSize));
				
		LoadBinTextures(graphicCmdList, pAssetData);
		queue->ExecuteCommandList(graphicCmdList);
		
		graphicCmdList = GetCommandQueue()->GetCommandList();
		LoadStudyTexture(graphicCmdList);
		queue->ExecuteCommandList(graphicCmdList);

		graphicCmdList = GetCommandQueue()->GetCommandList();
		LoadDoomSlayerTexture(graphicCmdList);
		queue->ExecuteCommandList(graphicCmdList);

		graphicCmdList = GetCommandQueue()->GetCommandList();
		LoadNanosuitTexture(graphicCmdList);
		queue->ExecuteCommandList(graphicCmdList);

		graphicCmdList = GetCommandQueue()->GetCommandList();
		LoadAtlasTexture(graphicCmdList);
		queue->ExecuteCommandList(graphicCmdList);

		graphicCmdList = GetCommandQueue()->GetCommandList();
		LoadPBodyTexture(graphicCmdList);
		queue->ExecuteCommandList(graphicCmdList);

		graphicCmdList = GetCommandQueue()->GetCommandList();
		LoadGolemTexture(graphicCmdList);
		
		auto fenceValue = queue->ExecuteCommandList(graphicCmdList);

		queue->WaitForFenceValue(fenceValue);

		free(pAssetData);
	}

	void SampleApp::BuildRootSignature()
	{
		rootSignature = std::make_unique<GRootSignature>();

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
		srvHeap = std::move(DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, textures.size()));


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
		vertices.resize(totalVertexCount);

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
		                                                    cmdList->GetGraphicsCommandList().Get(), vertices.data(),
		                                                    vbByteSize,
		                                                    geo->VertexBufferUploader);

		geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(dxDevice.Get(),
		                                                   cmdList->GetGraphicsCommandList().Get(), indices.data(),
		                                                   ibByteSize,
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

	void SampleApp::BuildPSOs()
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc;

		ZeroMemory(&basePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
		basePsoDesc.InputLayout = {defaultInputLayout.data(), static_cast<UINT>(defaultInputLayout.size())};
		basePsoDesc.pRootSignature = rootSignature->GetRootSignature().Get();
		basePsoDesc.VS = shaders["StandardVertex"]->GetShaderResource();
		basePsoDesc.PS = shaders["OpaquePixel"]->GetShaderResource();
		basePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		basePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
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
				std::make_unique<FrameResource>(2, gameObjects.size(), materials.size()));
		}
	}

	void SampleApp::BuildMaterials()
	{
		for (auto && draw : SampleAssets::Draws)
		{
			auto it = materials.find(draw.FileName);
			if(it == materials.end())
			{
				auto mat = std::make_unique<Material>(draw.FileName, psos[PsoType::Opaque].get());
				mat->DiffuseAlbedo = Vector4::One;
				mat->FresnelR0 = Vector3::One * 0.002  ;
				mat->Roughness = 1;
				mat->SetDiffuseTexture(textures[SampleAssets::Textures[draw.DiffuseTextureIndex].FileName].get());
				mat->SetNormalMap(textures[SampleAssets::Textures[draw.NormalTextureIndex].FileName].get());
				materials[mat->GetName()] = std::move(mat);
				it = materials.find(draw.FileName);
			}
		}
		
		auto seamless = std::make_unique<Material>(L"seamless", psos[PsoType::Opaque].get());
		seamless->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
		seamless->Roughness = 0.1f;
		seamless->SetDiffuseTexture(textures[L"seamless"].get());
		seamless->SetNormalMap(textures[L"defaultNormalMap"].get());
		materials[seamless->GetName()] = std::move(seamless);

		std::vector<std::wstring> doomNames =
		{
			L"Doomarms",
			L"Doomcowl",
			L"Doomhelmet",
			L"Doomlegs",
			L"Doomtorso",
			L"Doomvisor"
		};

		for (auto&& name : doomNames)
		{
			auto material = std::make_unique<Material>(name, psos[PsoType::Opaque].get());
			material->FresnelR0 = Vector3::One * 0.05;
			material->Roughness = 0.95;

			material->SetDiffuseTexture(textures[name].get());
			material->SetNormalMap(textures[(name + L"Normal")].get());

			if (material->GetName() == L"Doomvisor")
			{
				material->DiffuseAlbedo = XMFLOAT4(0, 0.8f, 0, 0.5f);
				material->FresnelR0 = Vector3::One * 0.1;
				material->Roughness = 0.99f;
			}

			materials[material->GetName()] = std::move(material);
		}

		std::vector<std::wstring> nanoNames =
		{
			L"Nanoarm",
			L"Nanobody",
			L"Nanoglass",
			L"Nanohand",
			L"Nanohelm",
			L"Nanoleg"
		};

		for (auto&& name : nanoNames)
		{
			auto material = std::make_unique<Material>(name, psos[PsoType::Opaque].get());
			material->FresnelR0 = Vector3::One * 0.05;
			material->Roughness = 0.95;

			material->SetDiffuseTexture(textures[(name)].get());
			material->SetNormalMap(textures[(name + L"Normal")].get());

			if (material->GetName() == L"Nanoglass")
			{
				material->DiffuseAlbedo = XMFLOAT4(0, 0.8f, 0, 0.5f);
				material->FresnelR0 = Vector3::One * 0.1;
				material->Roughness = 0.99f;
			}

			materials[material->GetName()] = std::move(material);
		}

		std::vector<std::wstring> AtlasNames =
		{
			L"Atlasframe",
			L"Atlasshell",
			L"Atlaseye",
		};

		for (auto&& name : AtlasNames)
		{
			auto material = std::make_unique<Material>(name, psos[PsoType::Opaque].get());
			material->FresnelR0 = Vector3::One * 0.05;
			material->Roughness = 0.6;
			material->SetDiffuseTexture(textures[(name)].get());
			material->SetNormalMap(textures[L"defaultNormalMap"].get());
			materials[material->GetName()] = std::move(material);
		}

		std::vector<std::wstring> PBodyNames =
		{
			L"PBodyframe",
			L"PBodyshell",
			L"PBodyorange",
			L"PBodyeye",
		};

		for (auto&& name : PBodyNames)
		{
			auto material = std::make_unique<Material>(name, psos[PsoType::Opaque].get());
			material->FresnelR0 = Vector3::One * 0.05;
			material->Roughness = 0.6;
			material->SetDiffuseTexture(textures[(name)].get());
			material->SetNormalMap(textures[L"defaultNormalMap"].get());
			materials[material->GetName()] = std::move(material);
		}

		std::vector<std::wstring> mechNames =
		{
			L"golemColor",
		};

		for (auto&& name : mechNames)
		{
			auto material = std::make_unique<Material>(name, psos[PsoType::Opaque].get());
			material->FresnelR0 = Vector3::One * 0.05;
			material->Roughness = 0.95;

			material->SetDiffuseTexture(textures[(name)].get());
			material->SetNormalMap(textures[(name + L"Normal")].get());
			materials[material->GetName()] = std::move(material);
		}

		

		auto skyBox = std::make_unique<Material>(L"sky", psos[PsoType::SkyBox].get());
		skyBox->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
		skyBox->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
		skyBox->Roughness = 1.0f;
		skyBox->SetDiffuseTexture(textures[L"skyTex"].get());
		skyBox->SetNormalMap(textures[L"defaultNormalMap"].get());
		materials[skyBox->GetName()] = std::move(skyBox);
		

		for (auto&& pair : materials)
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
		auto renderer = new Renderer();
		renderer->material = materials[L"sky"].get();
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
		renderer->material = materials[L"seamless"].get();
		renderer->mesh = meshes["shapeMesh"].get();
		renderer->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		renderer->IndexCount = renderer->mesh->Submeshs["quad"].IndexCount;
		renderer->StartIndexLocation = renderer->mesh->Submeshs["quad"].StartIndexLocation;
		renderer->BaseVertexLocation = renderer->mesh->Submeshs["quad"].BaseVertexLocation;
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

		auto commandQueue = this->GetCommandQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		auto cmdList = commandQueue->GetCommandList();

		auto man = std::make_unique<GameObject>(dxDevice.Get());
		man->GetTransform()->SetPosition(Vector3::Forward * 10 + (Vector3::Right * 5));
		man->GetTransform()->SetScale(Vector3::One * 0.25);
		man->GetTransform()->SetEulerRotate(Vector3(0, 90, 0));
		auto modelRenderer = new ModelRenderer();
		if (modelRenderer->AddModel(cmdList, "Data\\Objects\\Nanosuit\\Nanosuit.obj"))
		{
			if (modelRenderer->GetMeshesCount() > 0)
			{
				std::vector<std::wstring> names =
				{

					L"Nanoglass",
					L"Nanoleg",
					L"Nanohand",
					L"Nanoleg",
					L"Nanoarm",
					L"Nanohelm",
					L"Nanobody",
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
		if (modelRenderer->AddModel(cmdList, "Data\\Objects\\DoomSlayer\\doommarine.obj"))
		{
			if (modelRenderer->GetMeshesCount() > 0)
			{
				for (UINT i = 0; i < modelRenderer->GetMeshesCount(); ++i)
				{
					modelRenderer->SetMeshMaterial(i, materials[L"seamless"].get());
				}

				std::vector<std::wstring> doomNames =
				{
					L"Doomlegs",
					L"Doomtorso",
					L"Doomcowl",
					L"Doomarms",
					L"Doomvisor",
					L"Doomhelmet",
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
		if (modelRenderer->AddModel(cmdList, "Data\\Objects\\Atlas\\Atlas.obj"))
		{
			if (modelRenderer->GetMeshesCount() > 0)
			{
				for (UINT i = 0; i < modelRenderer->GetMeshesCount(); ++i)
				{
					modelRenderer->SetMeshMaterial(i, materials[L"seamless"].get());
				}

				std::vector<std::wstring> AtlasNames =
				{

					L"Atlasshell",
					L"Atlasframe",
					L"Atlaseye",
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
		if (modelRenderer->AddModel(cmdList, "Data\\Objects\\P-Body\\P-Body.obj"))
		{
			if (modelRenderer->GetMeshesCount() > 0)
			{
				for (UINT i = 0; i < modelRenderer->GetMeshesCount(); ++i)
				{
					modelRenderer->SetMeshMaterial(i, materials[L"seamless"].get());
				}

				std::vector<std::wstring> PBodyNames =
				{

					L"PBodyshell",
					L"PBodyframe",
					L"PBodyorange",
					L"PBodyeye",
					L"PBodyframe",
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
		if (modelRenderer->AddModel(cmdList, "Data\\Objects\\StoneGolem\\Stone.obj"))
		{
			if (modelRenderer->GetMeshesCount() > 0)
			{
				for (UINT i = 0; i < modelRenderer->GetMeshesCount(); ++i)
				{
					modelRenderer->SetMeshMaterial(i, materials[L"golemColor"].get());
				}
			}
			golem->AddComponent(modelRenderer);
		}
		typedGameObjects[PsoType::Opaque].push_back(golem.get());
		gameObjects.push_back(std::move(golem));
		
		for (auto && model : models)
		{			
			auto modelGO = std::make_unique<GameObject>(dxDevice.Get());
			modelGO->GetTransform()->SetScale(Vector3::One * 0.01);
			modelRenderer = new ModelRenderer();
			modelRenderer->AddModel(model.second);

			for (UINT i = 0; i < modelRenderer->GetMeshesCount(); ++i)
			{
				modelRenderer->SetMeshMaterial(i, materials[model.second->GetName()].get());
			}
			
			modelGO->AddComponent(modelRenderer);
			typedGameObjects[PsoType::Opaque].push_back(modelGO.get());
			gameObjects.push_back(std::move(modelGO));
		}
						
		
		commandQueue->ExecuteCommandList(cmdList);
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
