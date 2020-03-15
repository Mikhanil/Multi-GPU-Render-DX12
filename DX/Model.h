#pragma once
#include "GameObject.h"

namespace GameEngine
{
	namespace Graphics
	{
		using namespace DirectX::SimpleMath;
		
		// Vertex data for a colored cube.
		struct VertexPosColor
		{
			Vector3 Position;
			Vector4 Color;
		};
		
		class Model : public GameObject
		{
			std::vector<WORD> indecies;
			std::vector < VertexPosColor> verteces;


		public:
			bool Initialize(std::vector <VertexPosColor>& vertex, std::vector<WORD>& index )
			{
				this->indecies = index;
				this->verteces = vertex;

				return  true;
			}

			void Update() override
			{
				for (auto component : components)
				{
					component->Update();
				}
				
			};
		};
	}
}



