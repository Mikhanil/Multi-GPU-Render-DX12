#pragma once

#include "d3dUtil.h"
#include "GraphicPSO.h"
#include "GMemory.h"
#include "GTexture.h"
#include "ShaderBuffersData.h"

using namespace DirectX::SimpleMath;

class GCommandList;

class Ssao
{
public:

	Ssao(const std::shared_ptr<GDevice> device,
	     std::shared_ptr<GCommandList> cmdList,
	     UINT width, UINT height);
	Ssao(const Ssao& rhs) = delete;
	Ssao& operator=(const Ssao& rhs) = delete;
	~Ssao();

	static const DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;
	static const DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	static const DXGI_FORMAT DepthMapFormat = DXGI_FORMAT_R24G8_TYPELESS;

	static const int MaxBlurRadius = 5;

	UINT SsaoMapWidth() const;
	UINT SsaoMapHeight() const;

	void GetOffsetVectors(Vector4 offsets[]);
	std::vector<float> CalcGaussWeights(float sigma);


	GTexture& NormalMap();
	GTexture& AmbientMap();
	GTexture& NormalDepthMap();

	GMemory* NormalMapDSV();
	GMemory* NormalMapRtv();
	GMemory* NormalMapSrv();
	GMemory* AmbientMapSrv();

	void BuildDescriptors();

	void RebuildDescriptors();

	void SetPSOs(GraphicPSO& ssaoPso, GraphicPSO& ssaoBlurPso);

	void OnResize(UINT newWidth, UINT newHeight);

	
	void ComputeSsao(
		std::shared_ptr<GCommandList> cmdList,
		std::shared_ptr<ConstantBuffer<SsaoConstants>> currFrame,
		int blurCount);
	void ClearAmbiantMap(std::shared_ptr<GCommandList> cmdList);


private:
	std::shared_ptr<GDevice> device;
	
	void BlurAmbientMap(std::shared_ptr<GCommandList> cmdList, std::shared_ptr<ConstantBuffer<SsaoConstants>> currFrame, int blurCount);
	void BlurAmbientMap(std::shared_ptr<GCommandList> cmdList, bool horzBlur);
	GTexture CreateNormalMap() const;
	GTexture CreateAmbientMap() const;
	GTexture CreateDepthMap() const;

	void BuildResources();
	void BuildRandomVectorTexture(std::shared_ptr<GCommandList> cmdList);

	void BuildOffsetVectors();


private:

	GraphicPSO mSsaoPso;
	GraphicPSO mBlurPso;

	GTexture randomVectorMap;
	GTexture normalMap;
	GTexture ambientMap0;
	GTexture ambientMap1;
	GTexture depthMap;

	
	GMemory normalMapSrvMemory;
	GMemory normalMapRtvMemory;
	
	GMemory depthMapSrvMemory;
	GMemory depthMapDSVMemory;
	
	GMemory randomVectorSrvMemory;
	
	GMemory ambientMapMapSrvMemory;
	GMemory ambientMapRtvMemory;
	

	UINT mRenderTargetWidth;
	UINT mRenderTargetHeight;

	Vector4 mOffsets[14];

	D3D12_VIEWPORT mViewport;
	D3D12_RECT mScissorRect;
};
