#pragma once

#include "AssetsLoader.h"
#include "FrameResource.h"
#include "GDescriptor.h"
#include "GModel.h"
#include "Light.h"
#include "SimpleMath.h"
#include "GraphicPSO.h"
#include <DirectXCollision.h>
#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Camera.h"

namespace Common
{
    class Scene
    {
    public:
        static constexpr UINT ReflectionProbeCount = kReflectionProbeCount;

        Scene(const std::shared_ptr<GDevice>& device);

        void Initialize(const std::shared_ptr<GCommandList>& cmdList, float aspectRatio,
                        const DirectX::SimpleMath::Vector3& directionalLightDirection);

        void Update();
        // Restores deterministic camera/orbit/car positions before each benchmark state.
        void ResetBenchmarkAnimation();
        void UpdateMaterials(FrameResource* frameResource, bool useSecondGpuBuffer = false);
        void Draw(const std::shared_ptr<GCommandList>& cmdList, PEPEngine::Graphics::RenderMode mode) const;
        void DrawReflectionProbesExcept(const std::shared_ptr<GCommandList>& cmdList, UINT excludedProbeIndex) const;

        GDescriptor* GetSrvHeap();
        std::shared_ptr<Camera> GetCamera() const;
        const std::array<DirectX::SimpleMath::Vector3, ReflectionProbeCount>&
        GetReflectionProbeCenters() const;
        const std::vector<Light*>& GetLights() const;
        DirectX::BoundingSphere GetBounds() const;
        size_t GetObjectCount() const;
        size_t GetMaterialCount();
        size_t GetTextureCount() const;

    private:
        void LoadStudyTexture(const std::shared_ptr<GCommandList>& cmdList);
        void LoadTextures(const std::shared_ptr<GCommandList>& cmdList);
        void LoadModels();
        void GenerateMipMaps();
        void BuildTextureHeap();
        void BuildShapeGeometry();
        void BuildMaterials();
        void BuildObjects(float aspectRatio, const DirectX::SimpleMath::Vector3& directionalLightDirection);
        void SortObjects();
        void ClearTrackedObjects();
        std::unique_ptr<GameObject> CreateGOWithRenderer(const std::shared_ptr<GModel>& model) const;

        std::shared_ptr<GDevice> device;
        AssetsLoader loader;
        GDescriptor srvHeap;

        std::unordered_map<std::wstring, std::shared_ptr<GModel>> models;
        std::vector<std::unique_ptr<GameObject>> gameObjects;
        std::vector<std::vector<GameObject*>> typedGameObjects;
        std::vector<Light*> lights;
        std::shared_ptr<Camera> sceneCamera;
        GameObject* benchmarkCamera = nullptr;
        GameObject* benchmarkCameraOrbit = nullptr;
        GameObject* benchmarkMovingCar = nullptr;
        DirectX::SimpleMath::Matrix benchmarkCameraMatrix = DirectX::SimpleMath::Matrix::Identity;
        DirectX::SimpleMath::Matrix benchmarkCameraOrbitMatrix = DirectX::SimpleMath::Matrix::Identity;
        DirectX::SimpleMath::Matrix benchmarkMovingCarMatrix = DirectX::SimpleMath::Matrix::Identity;
        DirectX::BoundingSphere bounds;
        std::array<DirectX::SimpleMath::Vector3, ReflectionProbeCount> reflectionProbeCenters{};
    };
}
