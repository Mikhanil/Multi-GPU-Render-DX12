#include "pch.h"
#include "CloudGenerator.h"
#include "GCommandList.h"

void CloudGenerator::ChangeTextureSize(int width, int height)
{
	this->width = width;
	this->height = height;

	viewport.Height = static_cast<float>(height);
	viewport.Width = static_cast<float>(width);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;

	scissorRect = {0, 0, width, height};

	D3D12_RESOURCE_DESC cloudTexDesc;
	cloudTexDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	cloudTexDesc.Alignment = 0;
	cloudTexDesc.Width = width;
	cloudTexDesc.Height = height;
	cloudTexDesc.DepthOrArraySize = 1;
	cloudTexDesc.MipLevels = 1;
	cloudTexDesc.Format = rtvFormat;
	cloudTexDesc.SampleDesc.Count = 1;
	cloudTexDesc.SampleDesc.Quality = 0;
	cloudTexDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	cloudTexDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	if (!primeCloudTex.IsValid())
	{
		primeCloudTex = GTexture(primeDevice, cloudTexDesc, L"Prime Generated Cloud");
	}
	else
	{
		GTexture::Resize(primeCloudTex, width, height, 1);
	}

	if (!secondCloudTex.IsValid())
	{
		secondCloudTex = GTexture(secondDevice, cloudTexDesc, L"Second Cloud Generated Tex");
	}
	else
	{
		GTexture::Resize(secondCloudTex, width, height, 1);
	}

	if (crossAdapterCloudTex.IsInit())
	{
		crossAdapterCloudTex.Reset();
	}

	crossAdapterCloudTex = GCrossAdapterResource(cloudTexDesc, primeDevice, secondDevice, L"Cross Adapter Cloud Tex");

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
	uavDesc.Format = rtvFormat;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.PlaneSlice = 0;
	uavDesc.Texture2D.MipSlice = 0;

	primeCloudTex.CreateUnorderedAccessView(&uavDesc, &primeUavDescriptor);
	secondCloudTex.CreateUnorderedAccessView(&uavDesc, &secondUavDescriptor);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = rtvFormat;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.MipLevels = 1;

	primeCloudTex.CreateShaderResourceView(&srvDesc, &srvDescriptor);

	groupCountWidth = width / 32;
	groupCountHeight = height / 32;
}

void CloudGenerator::Initialize()
{
	primeUavDescriptor = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	srvDescriptor = primeDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

	secondUavDescriptor = secondDevice->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);


	ChangeTextureSize(width, height);


	CD3DX12_DESCRIPTOR_RANGE textures[1];
	textures[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	primeCloudGeneratedSignature.AddConstantParameter(sizeof(CloudGeneratorData) / sizeof(float), 0, 0);
	primeCloudGeneratedSignature.AddDescriptorParameter(&textures[0], 1);
	primeCloudGeneratedSignature.Initialize(primeDevice);

	cloudNoiseShader = GShader(L"Shaders\\CloudsNoiseCS.hlsl", ComputeShader, nullptr, "CS", "cs_5_1");

	cloudNoiseShader.LoadAndCompile();

	primeGeneratedPSO.SetRootSignature(primeCloudGeneratedSignature);
	primeGeneratedPSO.SetShader(&cloudNoiseShader);
	primeGeneratedPSO.Initialize(primeDevice);


	secondCloudGeneratedSignature.AddConstantParameter(sizeof(CloudGeneratorData) / sizeof(float), 0, 0);
	secondCloudGeneratedSignature.AddDescriptorParameter(&textures[0], 1);
	secondCloudGeneratedSignature.Initialize(secondDevice);

	secondGeneratedPSO.SetRootSignature(secondCloudGeneratedSignature);
	secondGeneratedPSO.SetShader(&cloudNoiseShader);
	secondGeneratedPSO.Initialize(secondDevice);
}

void CloudGenerator::PrimeCompute(const std::shared_ptr<GCommandList>& cmdList, const float deltaTime)
{
	generatorParameters.TotalTime += deltaTime;

	cmdList->TransitionBarrier(primeCloudTex.GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmdList->FlushResourceBarriers();

	cmdList->SetRootSignature(&primeCloudGeneratedSignature);
	cmdList->SetDescriptorsHeap(&primeUavDescriptor);
	cmdList->SetPipelineState(primeGeneratedPSO);

	cmdList->SetRoot32BitConstants(0, sizeof(CloudGeneratorData) / sizeof(float), &generatorParameters, 0);
	cmdList->SetRootDescriptorTable(1, &primeUavDescriptor);

	cmdList->Dispatch(groupCountWidth, groupCountHeight, 1);

	cmdList->TransitionBarrier(primeCloudTex.GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->FlushResourceBarriers();
}

void CloudGenerator::PrimeCopy(const std::shared_ptr<GCommandList>& primeCmdList) const
{
	primeCmdList->CopyResource(primeCloudTex, crossAdapterCloudTex.GetPrimeResource());
	primeCmdList->TransitionBarrier(primeCloudTex.GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	primeCmdList->FlushResourceBarriers();
}

void CloudGenerator::SecondCompute(const std::shared_ptr<GCommandList>& cmdList, float deltaTime)
{
	generatorParameters.TotalTime += deltaTime;

	cmdList->TransitionBarrier(secondCloudTex.GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmdList->FlushResourceBarriers();

	cmdList->SetRootSignature(&secondCloudGeneratedSignature);
	cmdList->SetDescriptorsHeap(&secondUavDescriptor);
	cmdList->SetPipelineState(secondGeneratedPSO);

	cmdList->SetRoot32BitConstants(0, sizeof(CloudGeneratorData) / sizeof(float), &generatorParameters, 0);
	cmdList->SetRootDescriptorTable(1, &secondUavDescriptor);

	cmdList->Dispatch(groupCountWidth, groupCountHeight, 1);

	cmdList->CopyResource(crossAdapterCloudTex.GetSharedResource(), secondCloudTex);
}


CloudGenerator::CloudGenerator(const std::shared_ptr<GDevice> primeDevice, const std::shared_ptr<GDevice> secondDevice,
                               UINT width, UINT height): primeDevice(primeDevice), secondDevice(secondDevice),
                                                         width(width), height(height)
{
	Initialize();

	generatorParameters.CloudScale = 1.1;
	generatorParameters.CloudSpeed = 0.006;
	generatorParameters.CloudDark = 0.5;
	generatorParameters.CloudLight = 0.3;

	generatorParameters.CloudCover = 0.2;
	generatorParameters.CloudAlpha = 8.0;
	generatorParameters.SkyTint = 0.5;
	generatorParameters.TotalTime = 0;

	generatorParameters.SkyColor1 = Vector4(0.2, 0.4, 0.6, 1);
	generatorParameters.SkyColor2 = Vector4(0.4, 0.7, 1.0, 1);
	generatorParameters.CloudColor = Vector4(1.1, 1.1, 0.9, 1);

	generatorParameters.RenderTargetSize = Vector2(height, width);
}

GDescriptor* CloudGenerator::GetCloudSRV()
{
	return &srvDescriptor;
}

GTexture& CloudGenerator::GetCloudTexture()
{
	return primeCloudTex;
}
