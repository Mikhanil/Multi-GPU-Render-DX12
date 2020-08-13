#include "Ssao.h"
#include <DirectXPackedVector.h>


#include "GDataUploader.h"
#include "GResourceStateTracker.h"

using namespace Microsoft::WRL;

Ssao::Ssao(
	ID3D12Device* device,
	ID3D12GraphicsCommandList* cmdList,
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

ID3D12Resource* Ssao::NormalMap()
{
	return mNormalMap.GetD3D12Resource().Get();
}

ID3D12Resource* Ssao::AmbientMap()
{
	return mAmbientMap0.GetD3D12Resource().Get();
}

CD3DX12_CPU_DESCRIPTOR_HANDLE Ssao::NormalMapRtv() const
{
	return CD3DX12_CPU_DESCRIPTOR_HANDLE( normalMapRtvMemory.GetCPUHandle());
}

CD3DX12_GPU_DESCRIPTOR_HANDLE Ssao::NormalMapSrv() const
{
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(normalMapRtvMemory.GetGPUHandle());
}

CD3DX12_GPU_DESCRIPTOR_HANDLE Ssao::AmbientMapSrv() const
{
	return CD3DX12_GPU_DESCRIPTOR_HANDLE(ambientMapMapSrvMemory.GetGPUHandle(0)); 
}

void Ssao::BuildDescriptors(ID3D12Resource* depthStencilBuffer)
{
	//  Create the descriptors
	RebuildDescriptors(depthStencilBuffer);
}

void Ssao::RebuildDescriptors(ID3D12Resource* depthStencilBuffer)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = NormalMapFormat;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;
	md3dDevice->CreateShaderResourceView(mNormalMap.GetD3D12Resource().Get(), &srvDesc, normalMapSrvMemory.GetCPUHandle());

	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	md3dDevice->CreateShaderResourceView(depthStencilBuffer, &srvDesc, depthMapSrvMemory.GetCPUHandle());

	srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	md3dDevice->CreateShaderResourceView(mRandomVectorMap.GetD3D12Resource().Get(), &srvDesc, randomVectorSrvMemory.GetCPUHandle());

	srvDesc.Format = AmbientMapFormat;
	md3dDevice->CreateShaderResourceView(mAmbientMap0.GetD3D12Resource().Get(), &srvDesc, ambientMapMapSrvMemory.GetCPUHandle(0));
	md3dDevice->CreateShaderResourceView(mAmbientMap1.GetD3D12Resource().Get(), &srvDesc, ambientMapMapSrvMemory.GetCPUHandle(1));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Format = NormalMapFormat;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;
	md3dDevice->CreateRenderTargetView(mNormalMap.GetD3D12Resource().Get(), &rtvDesc, normalMapRtvMemory.GetCPUHandle());

	rtvDesc.Format = AmbientMapFormat;
	md3dDevice->CreateRenderTargetView(mAmbientMap0.GetD3D12Resource().Get(), &rtvDesc, ambientMapRtvMemory.GetCPUHandle());
	md3dDevice->CreateRenderTargetView(mAmbientMap1.GetD3D12Resource().Get(), &rtvDesc, ambientMapRtvMemory.GetCPUHandle(1));
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

		// We render to ambient map at half the resolution.
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
	ID3D12GraphicsCommandList* cmdList,
	FrameResource* currFrame,
	int blurCount)
{
	cmdList->RSSetViewports(1, &mViewport);
	cmdList->RSSetScissorRects(1, &mScissorRect);

	// We compute the initial SSAO to AmbientMap0.

	// Change to RENDER_TARGET.
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mAmbientMap0.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_COMMON,
	                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET));

	float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
	cmdList->ClearRenderTargetView(ambientMapRtvMemory.GetCPUHandle(), clearValue, 0, nullptr);

	// Specify the buffers we are going to render to.
	cmdList->OMSetRenderTargets(1, &ambientMapRtvMemory.GetCPUHandle(), true, nullptr);

	// Bind the constant buffer for this pass.
	auto ssaoCBAddress = currFrame->SsaoConstantBuffer->Resource()->GetGPUVirtualAddress();
	cmdList->SetPipelineState(mSsaoPso.GetPSO().Get());

	cmdList->SetGraphicsRootConstantBufferView(0, ssaoCBAddress);
	cmdList->SetGraphicsRoot32BitConstant(1, 0, 0);

	// Bind the normal and depth maps.
	cmdList->SetGraphicsRootDescriptorTable(2, normalMapSrvMemory.GetGPUHandle());

	// Bind the random vector map.
	cmdList->SetGraphicsRootDescriptorTable(3, randomVectorSrvMemory.GetGPUHandle());


	// Draw fullscreen quad.
	cmdList->IASetVertexBuffers(0, 0, nullptr);
	cmdList->IASetIndexBuffer(nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(6, 1, 0, 0);

	// Change back to GENERIC_READ so we can read the texture in a shader.
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mAmbientMap0.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COMMON));

	BlurAmbientMap(cmdList, currFrame, blurCount);
}

void Ssao::ClearAmbiantMap(
	ID3D12GraphicsCommandList* cmdList)
{
	// Change to RENDER_TARGET.
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mAmbientMap0.GetD3D12Resource().Get(),
		D3D12_RESOURCE_STATE_COMMON,
	                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET));

	float clearValue[] = {0.0f, 0.0f, 0.0f, 1.0f};
	cmdList->ClearRenderTargetView(ambientMapRtvMemory.GetCPUHandle(), clearValue, 0, nullptr);

	// Change back to GENERIC_READ so we can read the texture in a shader.
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mAmbientMap0.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COMMON));
}

void Ssao::BlurAmbientMap(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrame, int blurCount)
{
	cmdList->SetPipelineState(mBlurPso.GetPSO().Get());

	auto ssaoCBAddress = currFrame->SsaoConstantBuffer->Resource()->GetGPUVirtualAddress();
	cmdList->SetGraphicsRootConstantBufferView(0, ssaoCBAddress);

	for (int i = 0; i < blurCount; ++i)
	{
		BlurAmbientMap(cmdList, true);
		BlurAmbientMap(cmdList, false);
	}
}

void Ssao::BlurAmbientMap(ID3D12GraphicsCommandList* cmdList, bool horzBlur)
{
	Texture output;
	CD3DX12_GPU_DESCRIPTOR_HANDLE inputSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE outputRtv;

	// Ping-pong the two ambient map textures as we apply
	// horizontal and vertical blur passes.
	if (horzBlur == true)
	{
		output = mAmbientMap1;
		inputSrv = ambientMapMapSrvMemory.GetGPUHandle();
		outputRtv = ambientMapRtvMemory.GetCPUHandle(1);
		cmdList->SetGraphicsRoot32BitConstant(1, 1, 0);
	}
	else
	{
		output = mAmbientMap0;
		inputSrv = ambientMapMapSrvMemory.GetGPUHandle(1);
		outputRtv = ambientMapRtvMemory.GetCPUHandle(0);
		cmdList->SetGraphicsRoot32BitConstant(1, 0, 0);
	}

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(output.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_COMMON,
	                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET));

	float clearValue[] = {1.0f, 1.0f, 1.0f, 1.0f};
	cmdList->ClearRenderTargetView(outputRtv, clearValue, 0, nullptr);

	cmdList->OMSetRenderTargets(1, &outputRtv, true, nullptr);

	// Normal/depth map still bound.


	// Bind the normal and depth maps.
	cmdList->SetGraphicsRootDescriptorTable(2, normalMapSrvMemory.GetGPUHandle());

	// Bind the input ambient map to second texture table.
	cmdList->SetGraphicsRootDescriptorTable(3, inputSrv);

	// Draw fullscreen quad.
	cmdList->IASetVertexBuffers(0, 0, nullptr);
	cmdList->IASetIndexBuffer(nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(6, 1, 0, 0);

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(output.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_COMMON));
}

void Ssao::BuildResources()
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


	float normalClearColor[] = {0.0f, 0.0f, 1.0f, 0.0f};
	CD3DX12_CLEAR_VALUE optClear(NormalMapFormat, normalClearColor);
		
	mNormalMap = Texture(texDesc, L"SSAO NormalMap", TextureUsage::Normalmap, &optClear);
		

	// Ambient occlusion maps are at half resolution.
	texDesc.Width = mRenderTargetWidth / 2;
	texDesc.Height = mRenderTargetHeight / 2;
	texDesc.Format = AmbientMapFormat;

	float ambientClearColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
	optClear = CD3DX12_CLEAR_VALUE(AmbientMapFormat, ambientClearColor);

	mAmbientMap0 = Texture(texDesc, L"SSAO AmbientMap 0", TextureUsage::Normalmap, &optClear);
	
	mAmbientMap1 = Texture(texDesc, L"SSAO AmbientMap 1", TextureUsage::Normalmap, &optClear);	

}

void Ssao::BuildRandomVectorTexture(ID3D12GraphicsCommandList* cmdList)
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

	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mRandomVectorMap.GetD3D12Resource().Get(),
	                                                                  D3D12_RESOURCE_STATE_COMMON,
	                                                                  D3D12_RESOURCE_STATE_COPY_DEST));
	UpdateSubresources(cmdList, mRandomVectorMap.GetD3D12Resource().Get(), &upload.d3d12Resource,
	                   upload.Offset, 0, num2DSubresources, &subResourceData);
	cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mRandomVectorMap.GetD3D12Resource().Get(),
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
