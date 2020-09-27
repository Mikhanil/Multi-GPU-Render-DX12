#pragma once
#include <d3d12.h>
#include <string>
#include "Lazy.h"


class NativeMesh;
class GModel;
class GCommandList;
class GBuffer;

class GMesh
{
	friend GModel;	

	std::shared_ptr<NativeMesh> mesh;
	
	
	std::shared_ptr<GBuffer> vertexBuffer = nullptr;
	std::shared_ptr<GBuffer> indexBuffer = nullptr;
	DXLib::Lazy< D3D12_VERTEX_BUFFER_VIEW> vertexView;
	DXLib::Lazy< D3D12_INDEX_BUFFER_VIEW> indexView;	
public:

	std::shared_ptr<NativeMesh> GetMeshData() const;
	
	D3D12_PRIMITIVE_TOPOLOGY GetPrimitiveType() const;
	
	D3D12_VERTEX_BUFFER_VIEW* GetVertexView();
	
	D3D12_INDEX_BUFFER_VIEW* GetIndexView();


	GMesh(std::shared_ptr<NativeMesh> meshData, std::shared_ptr<GCommandList>& cmdList);

	GMesh(const GMesh& copy);

	void Draw(std::shared_ptr<GCommandList> cmdList);

	std::wstring GetName() const;
};




