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

Texture& Ssao::NormalMap()
{
	return mNormalMap;
}

Texture& Ssao::AmbientMap()
{
	return mAmbientMap0;
}



void Ssao::BuildDescriptors(Texture& depthStencilBuffer)
{
	RebuildDescriptors(depthStencilBuffer);
}

void Ssao::RebuildDescriptors(Texture& depthStencilBuffer)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = NormalMapFormat;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	mNormalMap.CreateShaderResourceView( &srvDesc, &normalMapSrvMemory);

	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	depthStencilBuffer.CreateShaderResourceView( &srvDesc, &depthMapSrvMemory);

	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	mRandomVectorMap.CreateShaderResourceView( &srvDesc, &randomVectorSrvMemory);

	srvDesc.Format = AmbientMapFormat;
	mAmbientMap0.CreateShaderResourceView( &srvDesc, &ambientMapMapSrvMemory);
	mAmbientMap1.CreateShaderResourceView( &srvDesc, &ambientMapMapSrvMemory,1);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Format = NormalMapFormat;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	mNormalMap.CreateRenderTargetView( &rtvDesc, &normalMapRtvMemory);

	rtvDesc.Format = AmbientMapFormat;
	mAmbientMap0.CreateRenderTargetView(&rtvDesc, &ambientMapRtvMemory);
	mAmbientMap1.CreateRenderTargetView(&rtvDesc, &ambientMapRtvMemory, 1);
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
		mViewport.Width = mRenderTargetWidth / 2.0f;
		mViewport.Height = mRenderTargetHeight / 2.0f;
		mViewport.MinDepth = 0.0f;
		mViewport.MaxDepth = 1.0f;

		mScissorRect = {0, 0, static_cast<int>(mRenderTargetWidth) / 2, static_cast<int>(mRenderTargetHeight) / 2};

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

	auto d3d12Cmdlist = cmdList->GetGraphicsCommandList();	
	d3d12Cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mAmbientMap0.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_COMMON,
	                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET));

	float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
	cmdList->ClearRenderTarget(&ambientMapRtvMemory,0 , clearValue);

	cmdList->SetRenderTargets(1, &ambientMapRtvMemory, 0);

	auto ssaoCBAddress = currFrame->SsaoConstantBuffer->Resource()->GetGPUVirtualAddress();
	cmdList->SetPipelineState(mSsaoPso);

	cmdList->SetRootConstantBufferView(0, ssaoCBAddress);
	cmdList->SetRoot32BitConstant(1, 0, 0);

	cmdList->SetRootDescriptorTable(2, &normalMapSrvMemory);

	cmdList->SetRootDescriptorTable(3, &randomVectorSrvMemory);


	d3d12Cmdlist->IASetVertexBuffers(0, 0, nullptr);
	d3d12Cmdlist->IASetIndexBuffer(nullptr);
	d3d12Cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	d3d12Cmdlist->DrawInstanced(6, 1, 0, 0);

	d3d12Cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mAmbientMap0.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COMMON));

	BlurAmbientMap(cmdList, currFrame, blurCount);
}

void Ssao::ClearAmbiantMap(
	std::shared_ptr<GCommandList> cmdList)
{
	auto d3d12Cmdlist = cmdList->GetGraphicsCommandList();
	
	d3d12Cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mAmbientMap0.GetD3D12Resource().Get(),
		D3D12_RESOURCE_STATE_COMMON,
	                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET));

	float clearValue[] = {0.0f, 0.0f, 0.0f, 1.0f};
	cmdList->ClearRenderTarget(&ambientMapRtvMemory,0, clearValue);

	d3d12Cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mAmbientMap0.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COMMON));
}

void Ssao::BlurAmbientMap(std::shared_ptr<GCommandList> cmdList, FrameResource* currFrame, int blurCount)
{
	cmdList->SetPipelineState(mBlurPso);

	auto ssaoCBAddress = currFrame->SsaoConstantBuffer->Resource()->GetGPUVirtualAddress();
	cmdList->SetRootConstantBufferView(0, ssaoCBAddress);

	for (int i = 0; i < blurCount; ++i)
	{
		BlurAmbientMap(cmdList, true);
		BlurAmbientMap(cmdList, false);
	}
}

void Ssao::BlurAmbientMap(std::shared_ptr<GCommandList> cmdList, bool horzBlur)
{
	Texture output;
	size_t inputSrv;
	size_t outputRtv;

	if (horzBlur == true)
	{
		output = mAmbientMap1;
		inputSrv = 0;
		outputRtv = 1;
		cmdList->SetRoot32BitConstant(1, 1, 0);
	}
	else
	{
		output = mAmbientMap0;
		inputSrv = 1;
		outputRtv = 0;
		cmdList->SetRoot32BitConstant(1, 0, 0);
	}

	auto d3d12Cmdlist = cmdList->GetGraphicsCommandList();
	
	d3d12Cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(output.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_COMMON,
	                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET));

	float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
	cmdList->ClearRenderTarget(&ambientMapRtvMemory, outputRtv, clearValue);

	cmdList->SetRenderTargets(1, &ambientMapRtvMemory, outputRtv);

	cmdList->SetRootDescriptorTable(2, &normalMapSrvMemory);

	cmdList->SetRootDescriptorTable(3, &ambientMapMapSrvMemory, inputSrv);

	// Draw fullscreen quad.
	d3d12Cmdlist->IASetVertexBuffers(0, 0, nullptr);
	d3d12Cmdlist->IASetIndexBuffer(nullptr);
	d3d12Cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	d3d12Cmdlist->DrawInstanced(6, 1, 0, 0);

	d3d12Cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(output.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COMMON));
}

Texture Ssao::CreateNormalMap()
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

	return  Texture(texDesc, L"SSAO NormalMap", TextureUsage::Normalmap, &optClear);
}

Texture Ssao::CreateAmbientMap()
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
	texDesc.Width = mRenderTargetWidth / 2;
	texDesc.Height = mRenderTargetHeight / 2;
	texDesc.Format = AmbientMapFormat;

	float ambientClearColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	auto optClear = CD3DX12_CLEAR_VALUE(AmbientMapFormat, ambientClearColor);

	return Texture(texDesc, L"SSAO AmbientMap", TextureUsage::Normalmap, &optClear);
}

void Ssao::BuildResources()
{
	if (mNormalMap.GetD3D12Resource() == nullptr)
	{
		mNormalMap = CreateNormalMap();
	}
	else
	{
		Texture::Resize(mNormalMap, mRenderTargetWidth, mRenderTargetHeight, 1);
	}

	if(mAmbientMap0.GetD3D12Resource() == nullptr)
	{
		mAmbientMap0 = CreateAmbientMap();
	}
	else
	{
		Texture::Resize(mAmbientMap0, mRenderTargetWidth /2, mRenderTargetHeight /2, 1);
	}

	if (mAmbientMap1.GetD3D12Resource() == nullptr)
	{
		mAmbientMap1 = CreateAmbientMap();
	}
	else
	{
		Texture::Resize(mAmbientMap1, mRenderTargetWidth / 2, mRenderTargetHeight / 2, 1);
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

	mRandomVectorMap = Texture(texDesc, L"SSAO Random Vector Map", TextureUsage::Normalmap);

	//
	// In order to copy CPU memory data into our default buffer, we need to create
	// an intermediate upload heap. 
	//

	const UINT num2DSubresources = texDesc.DepthOrArraySize * texDesc.MipLevels;
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mRandomVectorMap.GetD3D12Resource().Get(), 0, num2DSubresources);

	auto upload = DXAllocator::UploadData(uploadBufferSize, nullptr, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	
	

	custom_vector<Vector4> data = DXAllocator::CreateVector<Vector4>();
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

	auto d3d12Cmdlist = cmdList->GetGraphicsCommandList();
	
	d3d12Cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mRandomVectorMap.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_COMMON,
	                                                                  D3D12_RESOURCE_STATE_COPY_DEST));
	UpdateSubresources(d3d12Cmdlist.Get(), mRandomVectorMap.GetD3D12Resource().Get(), &upload.d3d12Resource,
	                   upload.Offset, 0, num2DSubresources, &subResourceData);
	d3d12Cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mRandomVectorMap.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_COPY_DEST,
	                                                                  D3D12_RESOURCE_STATE_GENERIC_READ));
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
