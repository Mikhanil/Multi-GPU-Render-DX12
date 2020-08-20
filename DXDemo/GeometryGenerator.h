#pragma once

#include <cstdint>
#include <DirectXMath.h>
#include <DirectXMesh.h>
#include <vector>

#include "ShaderBuffersData.h"
#include "DXAllocator.h"

static void CalculateTangent(UINT i1, UINT i2, UINT i3, Vertex* vertices)
{
	UINT idx[3];
	idx[0] = i1;
	idx[1] = i2;
	idx[2] = i3;

	DirectX::XMFLOAT3 pos[3];
	pos[0] = vertices[i1].Position;
	pos[1] = vertices[i2].Position;
	pos[2] = vertices[i3].Position;

	DirectX::XMFLOAT3 normals[3];
	normals[0] = vertices[i1].Normal;
	normals[1] = vertices[i2].Normal;
	normals[2] = vertices[i3].Normal;

	DirectX::XMFLOAT2 t[3];
	t[0] = vertices[i1].TexCord;
	t[1] = vertices[i2].TexCord;
	t[2] = vertices[i3].TexCord;

	DirectX::XMFLOAT4 tangent[3];

	ComputeTangentFrame(idx, 1, pos, normals, t, 3, tangent);

	vertices[i1].TangentU = Vector3(tangent[0].x, tangent[0].y, tangent[0].z);
	vertices[i2].TangentU = Vector3(tangent[1].x, tangent[1].y, tangent[1].z);
	vertices[i3].TangentU = Vector3(tangent[2].x, tangent[2].y, tangent[2].z);
}

static void RecalculateTangent(DWORD* indices, size_t indexesCount, Vertex* vertices)
{
	for (UINT i = 0; i < indexesCount - 3; i += 3)
	{
		CalculateTangent(indices[i], indices[i + 1], indices[i + 2], vertices);
	}
}


class GeometryGenerator
{
public:

	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;


	struct MeshData
	{
		custom_vector<Vertex> Vertices = DXAllocator::CreateVector<Vertex>();
		custom_vector<DWORD> Indices32 = DXAllocator::CreateVector<DWORD>();

		custom_vector<uint16>& GetIndices16()
		{
			if (mIndices16.empty())
			{
				mIndices16.resize(Indices32.size());
				for (size_t i = 0; i < Indices32.size(); ++i)
					mIndices16[i] = static_cast<uint16>(Indices32[i]);
			}

			return mIndices16;
		}

	private:
		custom_vector<uint16> mIndices16 = DXAllocator::CreateVector<uint16>();
	};

	///<summary>
	/// Creates a box centered at the origin with the given dimensions, where each
	/// face has m rows and n columns of vertices.
	///</summary>
	MeshData CreateBox(float width, float height, float depth, uint32 numSubdivisions);

	///<summary>
	/// Creates a sphere centered at the origin with the given radius.  The
	/// slices and stacks parameters control the degree of tessellation.
	///</summary>
	MeshData CreateSphere(float radius, uint32 sliceCount, uint32 stackCount);

	///<summary>
	/// Creates a geosphere centered at the origin with the given radius.  The
	/// depth controls the level of tessellation.
	///</summary>
	MeshData CreateGeosphere(float radius, uint32 numSubdivisions);

	MeshData CreateSkySphere(int LatLines, int LongLines);

	///<summary>
	/// Creates a cylinder parallel to the y-axis, and centered about the origin.  
	/// The bottom and top radius can vary to form various cone shapes rather than true
	// cylinders.  The slices and stacks parameters control the degree of tessellation.
	///</summary>
	MeshData CreateCylinder(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount);

	///<summary>
	/// Creates an mxn grid in the xz-plane with m rows and n columns, centered
	/// at the origin with the specified width and depth.
	///</summary>
	MeshData CreateGrid(float width, float depth, uint32 m, uint32 n);

	///<summary>
	/// Creates a quad aligned with the screen.  This is useful for postprocessing and screen effects.
	///</summary>
	MeshData CreateQuad(float x, float y, float w, float h, float depth);

private:
	void Subdivide(MeshData& meshData);
	Vertex MidPoint(const Vertex& v0, const Vertex& v1);
	void BuildCylinderTopCap(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount,
	                         MeshData& meshData);
	void BuildCylinderBottomCap(float bottomRadius, float topRadius, float height, uint32 sliceCount, uint32 stackCount,
	                            MeshData& meshData);
};
