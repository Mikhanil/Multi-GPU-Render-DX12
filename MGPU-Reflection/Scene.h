#pragma once

#include "AssetsLoader.h"
#include "FrameResource.h"
#include "GDescriptor.h"
#include "GDevice.h"
#include "GCommandList.h"
#include "GModel.h"
#include "GraphicPSO.h"
#include "Light.h"
#include <DirectXCollision.h>
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

class Camera;
class GameObject;
class Renderer;
class Transform;

namespace Common
{

    // Scene resources deliberately belong to exactly one adapter.  In particular,
    // models and descriptors must not be used by the other GPU.
    class Scene
    {
    public:
        static constexpr UINT ReflectionProbeCount = 4;

        explicit Scene(const std::shared_ptr<PEPEngine::Graphics::GDevice>& device);

        void Initialize(float aspectRatio, const DirectX::SimpleMath::Vector3& directionalLightDirection);
        void Update();
        void ResetBenchmarkAnimation();
        void UpdateMaterials(FrameResource* frameResource, bool useSecondGpuBuffer = false);
        void Draw(const std::shared_ptr<PEPEngine::Graphics::GCommandList>& cmdList, PEPEngine::Graphics::RenderMode mode) const;
        void DrawReflectionProbe(const std::shared_ptr<PEPEngine::Graphics::GCommandList>& cmdList,
                                 UINT probeIndex) const;

        GDescriptor* GetSrvHeap();
        std::shared_ptr<Camera> GetCamera() const;
        const std::vector<Light*>& GetLights() const;
        UINT GetLightCount(LightType type) const;
        DirectX::BoundingSphere GetBounds() const;
        size_t GetObjectCount() const;
        size_t GetMaterialCount();
        size_t GetTextureCount();
        UINT GetTextureIndex(const std::wstring& name);
        std::array<DirectX::SimpleMath::Vector3, ReflectionProbeCount> GetReflectionProbePositions() const;

    private:
        void LoadTextures(const std::shared_ptr<PEPEngine::Graphics::GCommandList>& cmdList);
        void LoadModels(const std::shared_ptr<PEPEngine::Graphics::GCommandList>& cmdList);
        void GenerateMipMaps();
        void BuildTextureHeap();
        void BuildMaterials();
        void AssignMaterialIndices();
        void BuildObjects(float aspectRatio, const DirectX::SimpleMath::Vector3& directionalLightDirection);
        void SortObjects();
        std::shared_ptr<Renderer> CreateRenderer(const std::wstring& modelName) const;
        void AddObjectRenderer(GameObject* object, const std::wstring& modelName,
                               PEPEngine::Graphics::RenderMode mode);

        std::shared_ptr<PEPEngine::Graphics::GDevice> device;
        AssetsLoader loader;
        GDescriptor srvHeap;
        std::unordered_map<std::wstring, std::shared_ptr<GModel>> models;
        std::vector<std::unique_ptr<GameObject>> gameObjects;
        std::vector<std::vector<std::shared_ptr<Renderer>>> typedRenderers;
        std::vector<Light*> lights;
        std::shared_ptr<Camera> sceneCamera;
        GameObject* benchmarkCamera = nullptr;
        GameObject* benchmarkCameraOrbit = nullptr;
        GameObject* benchmarkMovingObject = nullptr;
        DirectX::SimpleMath::Matrix benchmarkCameraMatrix = DirectX::SimpleMath::Matrix::Identity;
        DirectX::SimpleMath::Matrix benchmarkCameraOrbitMatrix = DirectX::SimpleMath::Matrix::Identity;
        DirectX::SimpleMath::Matrix benchmarkMovingObjectMatrix = DirectX::SimpleMath::Matrix::Identity;
        std::array<std::shared_ptr<Transform>, ReflectionProbeCount> reflectionProbeTransforms;
        DirectX::BoundingSphere bounds;
    };
}
