#include "pch.h"
#include "CloudGenerator.h"
#include "GCommandList.h"

void CloudGenerator::Initialize()
{
	uavDescriptor = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	srvDescriptor = device->AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);

	int width = this->width;
	int height = this->height;

	viewport.Height = static_cast<float>(height);
	viewport.Width = static_cast<float>(width);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;

	scissorRect = {0, 0, width, height};


	D3D12_RESOURCE_DESC renderTargetDesc;
	renderTargetDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	renderTargetDesc.Alignment = 0;
	renderTargetDesc.Width = width;
	renderTargetDesc.Height = height;
	renderTargetDesc.DepthOrArraySize = 1;
	renderTargetDesc.MipLevels = 1;
	renderTargetDesc.Format = rtvFormat;
	renderTargetDesc.SampleDesc.Count = 1;
	renderTargetDesc.SampleDesc.Quality = 0;
	renderTargetDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	renderTargetDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	generatedCloudTex = GTexture(device, renderTargetDesc, L"Generated Cloud");

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
	uavDesc.Format = rtvFormat;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.PlaneSlice = 0;
	uavDesc.Texture2D.MipSlice = 0;
	
	generatedCloudTex.CreateUnorderedAccessView(&uavDesc, &uavDescriptor);


	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = rtvFormat;
	srvDesc.Texture2D.PlaneSlice = 0;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.MipLevels = 1;
	
	generatedCloudTex.CreateShaderResourceView(&srvDesc, &srvDescriptor);


	CD3DX12_DESCRIPTOR_RANGE textures[1];
	textures[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	cloudGeneratedSignature.AddDescriptorParameter(&textures[0], 1);
	cloudGeneratedSignature.Initialize(device);

	cloudNoiseShader = GShader(L"Shaders\\CloudsNoiseCS.hlsl", ComputeShader, nullptr, "CS", "cs_5_1");

	cloudNoiseShader.LoadAndCompile();

	generatedPSO.SetRootSignature(cloudGeneratedSignature);
	generatedPSO.SetShader(&cloudNoiseShader);
	generatedPSO.Initialize(device);

	groupCountWidth = width / 32;
	groupCountHeight = height / 32;
}

void CloudGenerator::Compute(const std::shared_ptr<GCommandList> cmdList)
{
	cmdList->TransitionBarrier(generatedCloudTex.GetD3D12Resource(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	cmdList->FlushResourceBarriers();
	
	cmdList->SetRootSignature(&cloudGeneratedSignature);
	cmdList->SetDescriptorsHeap(&uavDescriptor);
	cmdList->SetPipelineState(generatedPSO);

	cmdList->SetRootDescriptorTable(0, &uavDescriptor);

	cmdList->Dispatch(groupCountWidth, groupCountHeight, 1);

	cmdList->TransitionBarrier(generatedCloudTex.GetD3D12Resource(), D3D12_RESOURCE_STATE_COMMON);
	cmdList->FlushResourceBarriers();	
}

CloudGenerator::CloudGenerator(const std::shared_ptr<GDevice> device, UINT width, UINT height): device(device), width(width), height(height)
{
	Initialize();
}

GDescriptor* CloudGenerator::GetCloudSRV()
{
	return &srvDescriptor;
}

GTexture& CloudGenerator::GetCloudTexture()
{
	return generatedCloudTex;
}
