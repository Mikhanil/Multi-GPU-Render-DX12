#pragma once
#include <string>

#include "GBuffer.h"
#include "ShaderBuffersData.h"
#include "Lazy.h"


class Material;
class GModel;
class GCommandList;

class GMesh
{
	friend GModel;
	
	std::wstring meshName;
	
	D3D12_PRIMITIVE_TOPOLOGY primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

	std::vector<Vertex> vertices{};
	std::vector<DWORD> indexes{};
	
	mutable std::unordered_map<std::shared_ptr<GDevice>, std::shared_ptr<GBuffer>> vertexBuffers{};
	mutable std::unordered_map < std::shared_ptr<GDevice>, std::shared_ptr<GBuffer>> indexBuffers{};


	mutable std::unordered_map < std::shared_ptr<GDevice>,std::shared_ptr<D3D12_VERTEX_BUFFER_VIEW>> vertexView{};
	mutable std::unordered_map < std::shared_ptr<GDevice>,std::shared_ptr<D3D12_INDEX_BUFFER_VIEW>> indexView{};


	
public:
	D3D12_PRIMITIVE_TOPOLOGY GetPrimitiveType() const;;
	D3D12_VERTEX_BUFFER_VIEW* GetVertexView(std::shared_ptr<GDevice> device) const;
	D3D12_INDEX_BUFFER_VIEW* GetIndexView(std::shared_ptr<GDevice> device) const;

	UINT GetIndexCount() const;

	GMesh(const std::wstring name = L"");

	GMesh(const GMesh& copy);

	void ChangeIndexes(std::shared_ptr<GCommandList> cmdList,const DWORD* indices, size_t indexesCount);
	
	void ChangeVertices(std::shared_ptr<GCommandList> cmdList, const Vertex* vertices, size_t vertexesCount);

	void SetName(const std::wstring& name);

	void SetMaterial(const std::shared_ptr<Material> material);

	std::wstring GetName() const;

	void SetTopology(D3D12_PRIMITIVE_TOPOLOGY topology);

	void DublicateGraphicData(std::shared_ptr<GDevice> device);
};




