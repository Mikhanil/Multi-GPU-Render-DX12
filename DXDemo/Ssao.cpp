#include "Ssao.h"
#include <DirectXPackedVector.h>



#include "GCommandList.h"
#include "GDataUploader.h"
#include "GResourceStateTracker.h"

using namespace Microsoft::WRL;

Ssao::Ssao(
	ID3D12Device* device,
	std::shared_ptr<GCommandList> cmdList,
	UINT width, UINT height)

{
	md3dDevice = device;

	OnResize(width, height);

	BuildOffsetVectors();
	BuildRandomVectorTexture(cmdList);
}

Ssao::~Ssao() = default;

GMemory* Ssao::NormalMapRtv()
{
	return &normalMapRtvMemory;
}

GMemory* Ssao::NormalMapSrv()
{
	return &normalMapSrvMemory;
}

GMemory* Ssao::AmbientMapSrv()
{
	return &ambientMapMapSrvMemory;
}

UINT Ssao::SsaoMapWidth() const
{
	return mRenderTargetWidth / 2;
}

UINT Ssao::SsaoMapHeight() const
{
	return mRenderTargetHeight / 2;
}

void Ssao::GetOffsetVectors(Vector4 offsets[14])
{
	std::copy(&mOffsets[0], &mOffsets[14], &offsets[0]);
}

std::vector<float> Ssao::CalcGaussWeights(float sigma)
{
	float twoSigma2 = 2.0f * sigma * sigma;

	// Estimate the blur radius based on sigma since sigma controls the "width" of the bell curve.
	// For example, for sigma = 3, the width of the bell curve is 
	int blurRadius = static_cast<int>(ceil(2.0f * sigma));

	assert(blurRadius <= MaxBlurRadius);

	std::vector<float> weights;
	weights.resize(2 * blurRadius + 1);

	float weightSum = 0.0f;

	for (int i = -blurRadius; i <= blurRadius; ++i)
	{
		float x = static_cast<float>(i);

		weights[i + blurRadius] = expf(-x * x / twoSigma2);

		weightSum += weights[i + blurRadius];
	}

	// Divide by the sum so all the weights add up to 1.0.
	for (int i = 0; i < weights.size(); ++i)
	{
		weights[i] /= weightSum;
	}

	return weights;
}

GTexture& Ssao::NormalMap()
{
	return normalMap;
}

GTexture& Ssao::AmbientMap()
{
	return ambientMap0;
}

GTexture& Ssao::NormalDepthMap()
{
	return depthMap;
}

GMemory* Ssao::NormalMapDSV()
{
	return &depthMapDSVMemory;
}


void Ssao::BuildDescriptors()
{
	RebuildDescriptors();
}

void Ssao::RebuildDescriptors()
{
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.Texture2D.MipSlice = 0;
	depthMap.CreateDepthStencilView(&dsvDesc, &depthMapDSVMemory);

	
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = NormalMapFormat;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	normalMap.CreateShaderResourceView( &srvDesc, &normalMapSrvMemory);

	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	depthMap.CreateShaderResourceView( &srvDesc, &depthMapSrvMemory);

	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	randomVectorMap.CreateShaderResourceView( &srvDesc, &randomVectorSrvMemory);

	srvDesc.Format = AmbientMapFormat;
	ambientMap0.CreateShaderResourceView( &srvDesc, &ambientMapMapSrvMemory);
	ambientMap1.CreateShaderResourceView( &srvDesc, &ambientMapMapSrvMemory,1);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Format = NormalMapFormat;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	normalMap.CreateRenderTargetView( &rtvDesc, &normalMapRtvMemory);

	rtvDesc.Format = AmbientMapFormat;
	ambientMap0.CreateRenderTargetView(&rtvDesc, &ambientMapRtvMemory);
	ambientMap1.CreateRenderTargetView(&rtvDesc, &ambientMapRtvMemory, 1);
}

void Ssao::SetPSOs(GraphicPSO& ssaoPso, GraphicPSO& ssaoBlurPso)
{
	mSsaoPso = ssaoPso;
	mBlurPso = ssaoBlurPso;
}

void Ssao::OnResize(UINT newWidth, UINT newHeight)
{
	if (mRenderTargetWidth != newWidth || mRenderTargetHeight != newHeight)
	{
		mRenderTargetWidth = newWidth;
		mRenderTargetHeight = newHeight;

		mViewport.TopLeftX = 0.0f;
		mViewport.TopLeftY = 0.0f;
		mViewport.Width = mRenderTargetWidth ;
		mViewport.Height = mRenderTargetHeight ;
		mViewport.MinDepth = 0.0f;
		mViewport.MaxDepth = 1.0f;

		mScissorRect = {0, 0, static_cast<int>(mRenderTargetWidth), static_cast<int>(mRenderTargetHeight)};

		BuildResources();
	}
}

void Ssao::ComputeSsao(
	std::shared_ptr<GCommandList> cmdList,
	FrameResource* currFrame,
	int blurCount)
{
	cmdList->SetViewports( &mViewport,1);
	cmdList->SetScissorRects( &mScissorRect,1);
	
	cmdList->TransitionBarrier(ambientMap0, D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmdList->FlushResourceBarriers();

	
	float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
	cmdList->ClearRenderTarget(&ambientMapRtvMemory,0 , clearValue);

	cmdList->SetRenderTargets(1, &ambientMapRtvMemory, 0);

	cmdList->SetPipelineState(mSsaoPso);

	cmdList->SetRootConstantBufferView(0, *currFrame->SsaoConstantBuffer);
	cmdList->SetRoot32BitConstant(1, 0, 0);

	cmdList->SetRootDescriptorTable(2, &normalMapSrvMemory);

	cmdList->SetRootDescriptorTable(3, &randomVectorSrvMemory);


	cmdList->SetVBuffer(0, 0, nullptr);
	cmdList->SetIBuffer(nullptr);
	cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->Draw(6, 1, 0, 0);


	cmdList->TransitionBarrier(ambientMap0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmdList->FlushResourceBarriers();
	
	BlurAmbientMap(cmdList, currFrame, blurCount);
}

void Ssao::ClearAmbiantMap(
	std::shared_ptr<GCommandList> cmdList)
{
	cmdList->TransitionBarrier(ambientMap0, D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmdList->FlushResourceBarriers();

	float clearValue[] = {0.0f, 0.0f, 0.0f, 1.0f};
	cmdList->ClearRenderTarget(&ambientMapRtvMemory,0, clearValue);


	cmdList->TransitionBarrier(ambientMap0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmdList->FlushResourceBarriers();
}

void Ssao::BlurAmbientMap(std::shared_ptr<GCommandList> cmdList, FrameResource* currFrame, int blurCount)
{
	cmdList->SetPipelineState(mBlurPso);
	
	cmdList->SetRootConstantBufferView(0, *currFrame->SsaoConstantBuffer);

	for (int i = 0; i < blurCount; ++i)
	{
		BlurAmbientMap(cmdList, true);
		BlurAmbientMap(cmdList, false);
	}
}

void Ssao::BlurAmbientMap(std::shared_ptr<GCommandList> cmdList, bool horzBlur)
{
	GTexture output;
	size_t inputSrv;
	size_t outputRtv;

	if (horzBlur == true)
	{
		output = ambientMap1;
		inputSrv = 0;
		outputRtv = 1;
		cmdList->SetRoot32BitConstant(1, 1, 0);
	}
	else
	{
		output = ambientMap0;
		inputSrv = 1;
		outputRtv = 0;
		cmdList->SetRoot32BitConstant(1, 0, 0);
	}

	cmdList->TransitionBarrier(output, D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmdList->FlushResourceBarriers();
	
	float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
	cmdList->ClearRenderTarget(&ambientMapRtvMemory, outputRtv, clearValue);

	cmdList->SetRenderTargets(1, &ambientMapRtvMemory, outputRtv);

	cmdList->SetRootDescriptorTable(2, &normalMapSrvMemory);

	cmdList->SetRootDescriptorTable(3, &ambientMapMapSrvMemory, inputSrv);

	// Draw fullscreen quad.
	cmdList->SetVBuffer(0, 0, nullptr);
	cmdList->SetIBuffer(nullptr);
	cmdList->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->Draw(6, 1, 0, 0);

	cmdList->TransitionBarrier(output, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmdList->FlushResourceBarriers();
}

GTexture Ssao::CreateNormalMap() const
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = mRenderTargetWidth;
	texDesc.Height = mRenderTargetHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = NormalMapFormat;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;


	float normalClearColor[] = { 0.0f, 0.0f, 1.0f, 0.0f };
	CD3DX12_CLEAR_VALUE optClear(NormalMapFormat, normalClearColor);

	return  GTexture(texDesc, L"SSAO NormalMap", TextureUsage::Normalmap, &optClear);
}

GTexture Ssao::CreateAmbientMap() const
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	texDesc.Width = mRenderTargetWidth;
	texDesc.Height = mRenderTargetHeight ;
	texDesc.Format = AmbientMapFormat;

	float ambientClearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	auto optClear = CD3DX12_CLEAR_VALUE(AmbientMapFormat, ambientClearColor);

	return GTexture(texDesc, L"SSAO AmbientMap", TextureUsage::Normalmap, &optClear);
}


GTexture Ssao::CreateDepthMap() const
{
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = mRenderTargetWidth;
	depthStencilDesc.Height = mRenderTargetHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = DepthMapFormat;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	return GTexture(depthStencilDesc, L"SSAO Depth Normal Map", TextureUsage::Depth, &optClear);
}


void Ssao::BuildResources()
{
	if (normalMap.GetD3D12Resource() == nullptr)
	{
		normalMap = CreateNormalMap();
	}
	else
	{
		GTexture::Resize(normalMap, mRenderTargetWidth, mRenderTargetHeight, 1);
	}
	
	if(depthMap.GetD3D12Resource() == nullptr)
	{
		depthMap = CreateDepthMap();
	}
	else
	{
		GTexture::Resize(depthMap, mRenderTargetWidth, mRenderTargetHeight, 1);
	}
	
	if(ambientMap0.GetD3D12Resource() == nullptr)
	{
		ambientMap0 = CreateAmbientMap();
	}
	else
	{
		GTexture::Resize(ambientMap0, mRenderTargetWidth, mRenderTargetHeight, 1);
	}

	if (ambientMap1.GetD3D12Resource() == nullptr)
	{
		ambientMap1 = CreateAmbientMap();
	}
	else
	{
		GTexture::Resize(ambientMap1, mRenderTargetWidth , mRenderTargetHeight, 1);
	}
}

void Ssao::BuildRandomVectorTexture(std::shared_ptr<GCommandList> cmdList)
{
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = 256;
	texDesc.Height = 256;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	randomVectorMap = GTexture(texDesc, L"SSAO Random Vector Map", TextureUsage::Normalmap);
	
	std::vector<Vector4> data;
	data.resize(256 * 256);

	for (int i = 0; i < 256; ++i)
	{
		for (int j = 0; j < 256; ++j)
		{
			// Random vector in [0,1].  We will decompress in shader to [-1,1].
			Vector3 v(MathHelper::RandF(), MathHelper::RandF(), MathHelper::RandF());
			data[i * 256 + j] = Vector4(v.x, v.y, v.z, 0.0f);
		}
	}

	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = data.data();
	subResourceData.RowPitch = 256 * sizeof(Vector4);
	subResourceData.SlicePitch = subResourceData.RowPitch * 256;	

	cmdList->TransitionBarrier(randomVectorMap.GetD3D12Resource(), D3D12_RESOURCE_STATE_COPY_DEST);
	cmdList->FlushResourceBarriers();

	cmdList->UpdateSubresource(randomVectorMap, &subResourceData, 1);
	
	cmdList->TransitionBarrier(randomVectorMap.GetD3D12Resource(), D3D12_RESOURCE_STATE_GENERIC_READ);
	cmdList->FlushResourceBarriers();
}

void Ssao::BuildOffsetVectors()
{
	// Start with 14 uniformly distributed vectors.  We choose the 8 corners of the cube
	// and the 6 center points along each cube face.  We always alternate the points on 
	// opposites sides of the cubes.  This way we still get the vectors spread out even
	// if we choose to use less than 14 samples.

	// 8 cube corners
	mOffsets[0] = Vector4(+1.0f, +1.0f, +1.0f, 0.0f);
	mOffsets[1] = Vector4(-1.0f, -1.0f, -1.0f, 0.0f);

	mOffsets[2] = Vector4(-1.0f, +1.0f, +1.0f, 0.0f);
	mOffsets[3] = Vector4(+1.0f, -1.0f, -1.0f, 0.0f);

	mOffsets[4] = Vector4(+1.0f, +1.0f, -1.0f, 0.0f);
	mOffsets[5] = Vector4(-1.0f, -1.0f, +1.0f, 0.0f);

	mOffsets[6] = Vector4(-1.0f, +1.0f, -1.0f, 0.0f);
	mOffsets[7] = Vector4(+1.0f, -1.0f, +1.0f, 0.0f);

	// 6 centers of cube faces
	mOffsets[8] = Vector4(-1.0f, 0.0f, 0.0f, 0.0f);
	mOffsets[9] = Vector4(+1.0f, 0.0f, 0.0f, 0.0f);

	mOffsets[10] = Vector4(0.0f, -1.0f, 0.0f, 0.0f);
	mOffsets[11] = Vector4(0.0f, +1.0f, 0.0f, 0.0f);

	mOffsets[12] = Vector4(0.0f, 0.0f, -1.0f, 0.0f);
	mOffsets[13] = Vector4(0.0f, 0.0f, +1.0f, 0.0f);

	for (auto& mOffset : mOffsets)
	{
		// Create random lengths in [0.25, 1.0].
		float s = MathHelper::RandF(0.25f, 1.0f);
		mOffset.Normalize();
		mOffset = s * mOffset;
	}
}
