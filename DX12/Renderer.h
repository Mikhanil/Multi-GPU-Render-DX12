#pragma once
#include "Component.h"
#include "DirectXBuffers.h"
#include <ResourceUploadBatch.h>
#include <DDSTextureLoader.h>

using namespace Microsoft::WRL;

class Texture
{
	std::wstring Filename;
	std::string Name;
	Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
	bool isLoaded = false;
public:

	Texture( std::string name, std::wstring filename):
	Filename(std::move(filename)), Name(std::move(name)) {  }
	
	void LoadTexture(ID3D12Device* device, ID3D12CommandQueue* queue)
	{
		if(isLoaded) return;
		
		DirectX::ResourceUploadBatch upload(device);
		upload.Begin();

		ThrowIfFailed(DirectX::CreateDDSTextureFromFile(device,
			upload, Filename.c_str(), Resource.GetAddressOf()));

		
		// Upload the resources to the GPU.
		upload.End(queue).wait();

		isLoaded = true;
	}

	ID3D12Resource* GetResource() const
	{
		if(isLoaded)return Resource.Get();

		return nullptr;
	}

	std::string& GetName()
	{
		return Name;
	}

	std::wstring& GetFileName()
	{
		return Filename;
	}
};

enum MaterialType
{
	Opaque,
	Wireframe,
	AlphaDrop,
	Transparent,
};

enum ShaderType
{
	VertexShader,
	PixelShader,
	DomainShader,
	GeometryShader,
	HullShader
};

class Shader
{		
	std::wstring FileName;	
	ComPtr<ID3DBlob> shaderBlob = nullptr;
	ShaderType type;
	const D3D_SHADER_MACRO* defines;
	std::string entryPoint;
	std::string target;
public:

	Shader(const std::wstring fileName, const ShaderType type,const D3D_SHADER_MACRO* defines = nullptr, const std::string entryPoint = "Main", const std::string target = "ps_5_1")
	: FileName(fileName), type(type), defines(defines), entryPoint(entryPoint), target(target)
	{
	}

	void LoadAndCompile()
	{
		shaderBlob = d3dUtil::CompileShader(FileName, defines, entryPoint, target);
	}	
	

	ID3DBlob* GetShaderData()
	{
		return shaderBlob.Get();
	}

	D3D12_SHADER_BYTECODE GetShaderInfo() const
	{
		D3D12_SHADER_BYTECODE info{ shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };
		return info;
	}

	ShaderType GetType()
	{
		return type;
	}
};

struct MaterialConstants
{
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 0.25f;

	// Used in texture mapping.
	DirectX::XMFLOAT4X4 MaterialTransform = MathHelper::Identity4x4();
};

class Material
{
	std::unique_ptr<ConstantBuffer<MaterialConstants>> materialBuffer = nullptr;	
	ComPtr<ID3D12DescriptorHeap> materialViewDescriptorHeap;
	ComPtr<ID3D12PipelineState> pso;
	ComPtr<ID3D12PipelineState> debugPso;
	ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	std::string Name;

	std::vector<Shader*> shaders;
	
	MaterialConstants matConstants{};
	
	UINT NumFramesDirty = globalCountFrameResources;
	UINT cbvSrvUavDescriptorSize = 0;

	std::vector<Texture*> textures{1};
	DXGI_FORMAT backBufferFormat;
	DXGI_FORMAT depthStencilFormat;

	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
	bool ism4xmsaa;
	UINT m4xMsaaQuality = 0;

	static std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers()
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

	D3D12_INPUT_LAYOUT_DESC GetInputLayoutInfo()
	{
		D3D12_INPUT_LAYOUT_DESC info{ inputLayout.data(), inputLayout.size() };
		return info;
	}

	void CreateRootSignature(ID3D12Device* device)
	{
		if (rootSignature != nullptr) rootSignature->Release();
		
		CD3DX12_DESCRIPTOR_RANGE texTable[1];

		for (int i = 0; i < _countof(texTable); ++i)
		{
			texTable[i].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
				1,  // number of descriptors
				i); // register t
		}


		CD3DX12_ROOT_PARAMETER slotRootParameter[4];

		// Perfomance TIP: Order from most frequent to least frequent.
		slotRootParameter[0].InitAsDescriptorTable(1, &texTable[0], D3D12_SHADER_VISIBILITY_PIXEL);
		slotRootParameter[1].InitAsConstantBufferView(0);
		slotRootParameter[2].InitAsConstantBufferView(1);
		slotRootParameter[3].InitAsConstantBufferView(2);

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

		ThrowIfFailed(device->CreateRootSignature(
			0,
			serializedRootSig->GetBufferPointer(),
			serializedRootSig->GetBufferSize(),
			IID_PPV_ARGS(rootSignature.GetAddressOf())));
	}
	
	void CreatePso(ID3D12Device* device)
	{
		if (pso != nullptr) pso->Release();
		
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
		
		ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

		CreateRootSignature(device);
		
		psoDesc.InputLayout = GetInputLayoutInfo();
		psoDesc.pRootSignature = rootSignature.Get();
		psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = backBufferFormat;
		psoDesc.SampleDesc.Count = ism4xmsaa ? 4 : 1;
		psoDesc.SampleDesc.Quality = ism4xmsaa ? (m4xMsaaQuality - 1) : 0;
		psoDesc.DSVFormat = depthStencilFormat;

		for (auto && shader : shaders)
		{
			switch (shader->GetType())
			{
			case VertexShader:
				{
					psoDesc.VS = shader->GetShaderInfo();
					break;
				}
			case PixelShader:
				{
					psoDesc.PS = shader->GetShaderInfo();
					break;
				}
			case DomainShader:
				{
					psoDesc.DS = shader->GetShaderInfo();
					break;
				}
			case GeometryShader:
				{
					psoDesc.GS = shader->GetShaderInfo();
					break;
				}
			case HullShader:
				{
					psoDesc.HS = shader->GetShaderInfo();
					break;
				}
			}
		}

		switch (type)
		{
		case Opaque:
			{
				break;
			}
		case Wireframe:
			{
				psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
				break;
			}
		case AlphaDrop:
			{
				psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
				break;
			}
		case Transparent:
			{
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
				psoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
				break;
			}
		}


		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&pso)));

		psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;		
		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&debugPso)));
	}

	MaterialType type;
	
public:

	MaterialType GetType()
	{
		return type;
	}
	
	void SetDiffuseTexture(Texture* texture)
	{
		textures[0] = texture;
	}


	Material(ID3D12Device* device, std::string name, MaterialType type , DXGI_FORMAT backBufferFormat, DXGI_FORMAT depthStencilFormat,
		const std::vector<D3D12_INPUT_ELEMENT_DESC> layout, bool isM4xMsaa, UINT m4xMsaaQuality)
	: Name(std::move(name)), backBufferFormat(backBufferFormat) , depthStencilFormat(depthStencilFormat), inputLayout(layout),
	ism4xmsaa(isM4xMsaa), m4xMsaaQuality(m4xMsaaQuality), type(type)
	{
		cbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

		materialBuffer = std::make_unique<ConstantBuffer<MaterialConstants>>(device, 1);
	}

	void AddShader(Shader* shader)
	{
		shaders.push_back(shader);
	}
	
	void InitMaterial(ID3D12Device* device)
	{
		CreatePso(device);
		
		if (materialViewDescriptorHeap != nullptr) materialViewDescriptorHeap->Release();
		
		//
		// Create the SRV heap.
		//		
		D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
		srvHeapDesc.NumDescriptors = textures.size();
		srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&materialViewDescriptorHeap)));

		
		//
		// Fill out the heap with actual descriptors.
		//
		CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(materialViewDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		for (int i = 0; i < textures.size(); ++i)
		{
			srvDesc.Format = textures[i]->GetResource()->GetDesc().Format;
			srvDesc.Texture2D.MipLevels = textures[i]->GetResource()->GetDesc().MipLevels;
			device->CreateShaderResourceView(textures[i]->GetResource(), &srvDesc, hDescriptor);
			hDescriptor.Offset(1, cbvSrvUavDescriptorSize);
		}
	}

	ID3D12PipelineState* GetPSO()
	{
		return pso.Get();
	}

	ID3D12RootSignature* GetRootSignature()
	{
		return rootSignature.Get();
	}
	
	void Draw(ID3D12GraphicsCommandList* cmdList) const
	{
		cmdList->SetGraphicsRootConstantBufferView(3, materialBuffer->Resource()->GetGPUVirtualAddress());

		cmdList->SetDescriptorHeaps(1, materialViewDescriptorHeap.GetAddressOf());
		
		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(materialViewDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		for (int i=0; i < textures.size(); ++i)
		{
			tex.Offset(i, cbvSrvUavDescriptorSize);
			cmdList->SetGraphicsRootDescriptorTable(i, tex);
		}
	
	}

	void Update()
	{
		if (NumFramesDirty > 0)
		{
			matConstants.DiffuseAlbedo = DiffuseAlbedo;
			matConstants.FresnelR0 = FresnelR0;
			matConstants.Roughness = Roughness;
			XMStoreFloat4x4(&matConstants.MaterialTransform, XMMatrixTranspose(XMLoadFloat4x4(&MatTransform)));

			materialBuffer->CopyData(0, matConstants);

			// Next FrameResource need to be updated too.
			NumFramesDirty--;
		}
	}
	
	std::string& GetName()
	{
		return Name;
	}
	
	
	// Index into SRV heap for diffuse texture.
	//int DiffuseSrvHeapIndex = -1;

	// Material constant buffer data used for shading.
	DirectX::XMFLOAT4 DiffuseAlbedo = DirectX::XMFLOAT4(DirectX::Colors::White);
	DirectX::XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = .25f;
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};





class Renderer :	public Component
{
	
public:

	Renderer(ID3D12Device* device, ID3D12DescriptorHeap* descriptor);

	Material* Material = nullptr;
	MeshGeometry* Mesh = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;


	void Update() override;;
	void Draw(ID3D12GraphicsCommandList* cmdList) override;;
};

