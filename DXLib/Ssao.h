#pragma once

#include "d3dUtil.h"
#include "FrameResource.h"
#include "GraphicPSO.h"


class Ssao
{
public:

	Ssao(ID3D12Device* device,
	     ID3D12GraphicsCommandList* cmdList,
	     UINT width, UINT height);
	Ssao(const Ssao& rhs) = delete;
	Ssao& operator=(const Ssao& rhs) = delete;
	~Ssao() = default;

	static const DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;
	static const DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

	static const int MaxBlurRadius = 5;

	UINT SsaoMapWidth() const;
	UINT SsaoMapHeight() const;

	void GetOffsetVectors(Vector4 offsets[]);
	std::vector<float> CalcGaussWeights(float sigma);


	ID3D12Resource* NormalMap();
	ID3D12Resource* AmbientMap();

	CD3DX12_CPU_DESCRIPTOR_HANDLE NormalMapRtv() const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE NormalMapSrv() const;
	CD3DX12_GPU_DESCRIPTOR_HANDLE AmbientMapSrv() const;

	void BuildDescriptors(
		ID3D12Resource* depthStencilBuffer);

	void RebuildDescriptors(ID3D12Resource* depthStencilBuffer);

	void SetPSOs(GraphicPSO& ssaoPso, GraphicPSO& ssaoBlurPso);

	///<summary>
	/// Call when the backbuffer is resized.  
	///</summary>
	void OnResize(UINT newWidth, UINT newHeight);

	///<summary>
	/// Changes the render target to the Ambient render target and draws a fullscreen
	/// quad to kick off the pixel shader to compute the AmbientMap.  We still keep the
	/// main depth buffer binded to the pipeline, but depth buffer read/writes
	/// are disabled, as we do not need the depth buffer computing the Ambient map.
	///</summary>
	void ComputeSsao(
		ID3D12GraphicsCommandList* cmdList,
		FrameResource* currFrame,
		int blurCount);
	void ClearAmbiantMap(ID3D12GraphicsCommandList* cmdList);


private:

	///<summary>
	/// Blurs the ambient map to smooth out the noise caused by only taking a
	/// few random samples per pixel.  We use an edge preserving blur so that 
	/// we do not blur across discontinuities--we want edges to remain edges.
	///</summary>
	void BlurAmbientMap(ID3D12GraphicsCommandList* cmdList, FrameResource* currFrame, int blurCount);
	void BlurAmbientMap(ID3D12GraphicsCommandList* cmdList, bool horzBlur);

	void BuildResources();
	void BuildRandomVectorTexture(ID3D12GraphicsCommandList* cmdList);

	void BuildOffsetVectors();


private:
	ID3D12Device* md3dDevice;

	GraphicPSO mSsaoPso;
	GraphicPSO mBlurPso;

	Texture mRandomVectorMap;
	Texture mNormalMap;
	Texture mAmbientMap0;
	Texture mAmbientMap1;

	GMemory normalMapSrvMemory = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,1);
	GMemory normalMapRtvMemory = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1);
	
	GMemory depthMapSrvMemory = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	
	GMemory randomVectorSrvMemory = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1);
	

	// Need two for ping-ponging during blur.
	GMemory ambientMapMapSrvMemory = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2);
	GMemory ambientMapRtvMemory = DXAllocator::AllocateDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2);
	

	UINT mRenderTargetWidth;
	UINT mRenderTargetHeight;

	Vector4 mOffsets[14];

	D3D12_VIEWPORT mViewport;
	D3D12_RECT mScissorRect;
};
