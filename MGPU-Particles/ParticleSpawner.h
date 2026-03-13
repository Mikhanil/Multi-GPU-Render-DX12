#pragma once
#include <cstdint>
#include <array>
#include <cmath>
#include <random>
#include <SimpleMath.h>

class ParticleSpawner
{
public:    
    ParticleSpawner() = default;

    struct SpawnRegion
    {
        DirectX::SimpleMath::Vector3 Center;
        DirectX::SimpleMath::Vector3 Size = DirectX::SimpleMath::Vector3::One;
        uint32_t TargetParticleCount = 1000;

        std::array<int, 3> CalculateParticlesPerSide() const
        {
            if (TargetParticleCount == 0)
            {
                return {0, 0, 0};
            }

            const float sizeX = std::max(Size.x, 0.0001f);
            const float sizeY = std::max(Size.y, 0.0001f);
            const float sizeZ = std::max(Size.z, 0.0001f);
            const float volume = sizeX * sizeY * sizeZ;
            const float particleScale = std::cbrt(static_cast<float>(TargetParticleCount) / volume);

            return {
                std::max(1, static_cast<int>(std::round(particleScale * sizeX))),
                std::max(1, static_cast<int>(std::round(particleScale * sizeY))),
                std::max(1, static_cast<int>(std::round(particleScale * sizeZ)))
            };
        }
    };

    struct SpawnData
    {
        std::vector<DirectX::SimpleMath::Vector3> Points;
        std::vector<DirectX::SimpleMath::Vector3> Velocities;
    };

    SpawnData GetSpawnData() const
    {
        SpawnData allParticles;

        for (const auto& region : SpawnRegions)
        {
            const auto particlesPerAxis = region.CalculateParticlesPerSide();
            if (particlesPerAxis[0] <= 0 || particlesPerAxis[1] <= 0 || particlesPerAxis[2] <= 0)
            {
                continue;
            }

            const auto& cube = SpawnCube(particlesPerAxis, region.Center, region.Size);

            allParticles.Points.insert(allParticles.Points.end(), cube.Points.begin(), cube.Points.end());
            allParticles.Velocities.insert(allParticles.Velocities.end(), cube.Velocities.begin(), cube.Velocities.end());
        }

        return allParticles;
    }

    SpawnData SpawnCube(const std::array<int, 3>& numPerAxis, DirectX::SimpleMath::Vector3 center, DirectX::SimpleMath::Vector3 size) const 
    {
        if (numPerAxis[0] <= 0 || numPerAxis[1] <= 0 || numPerAxis[2] <= 0)
        {
            return SpawnData{};
        }

        thread_local std::mt19937 randomGenerator(std::random_device{}());
        std::uniform_real_distribution<float> jitterDistribution(-JitterStrength, JitterStrength);

        if (numPerAxis[0] == 1 && numPerAxis[1] == 1 && numPerAxis[2] == 1)
        {
            DirectX::SimpleMath::Vector3 jitter(
                jitterDistribution(randomGenerator),
                jitterDistribution(randomGenerator),
                jitterDistribution(randomGenerator));

            return SpawnData{ {center + jitter}, {InitialVelocity} };
        }

        int numPoints = numPerAxis[0] * numPerAxis[1] * numPerAxis[2];
        std::vector<DirectX::SimpleMath::Vector3> points(numPoints);
        std::vector<DirectX::SimpleMath::Vector3> velocities(numPoints);

        int i = 0;
        for (int x = 0; x < numPerAxis[0]; x++)
        {
            for (int y = 0; y < numPerAxis[1]; y++)
            {
                for (int z = 0; z < numPerAxis[2]; z++)
                {
                    float tx = numPerAxis[0] == 1 ? 0.5f : x / (numPerAxis[0] - 1.f);
                    float ty = numPerAxis[1] == 1 ? 0.5f : y / (numPerAxis[1] - 1.f);
                    float tz = numPerAxis[2] == 1 ? 0.5f : z / (numPerAxis[2] - 1.f);

                    float px = (tx - 0.5f) * size.x + center.x;
                    float py = (ty - 0.5f) * size.y + center.y;
                    float pz = (tz - 0.5f) * size.z + center.z;

                    DirectX::SimpleMath::Vector3 jitter(
                        jitterDistribution(randomGenerator),
                        jitterDistribution(randomGenerator),
                        jitterDistribution(randomGenerator));

                    points[i] = DirectX::SimpleMath::Vector3(px, py, pz) + jitter;
                    velocities[i] = InitialVelocity;
                    i++;
                }
            }
        }
        return SpawnData{ points, velocities };
    }
    
public:
    DirectX::SimpleMath::Vector3 InitialVelocity;
    float JitterStrength = 0.1f;

    std::vector<SpawnRegion> SpawnRegions; 
};
