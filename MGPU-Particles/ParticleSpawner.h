#pragma once
#include <cstdint>
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
        float Size;
        uint32_t TargetParticleCount = 1000;

        int CalculateParticlesPerSide() const
        {
            return static_cast<int>(std::round(std::cbrt(static_cast<float>(TargetParticleCount))));
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
            int particlesPerAxis = region.CalculateParticlesPerSide();
            if (particlesPerAxis <= 0)
            {
                continue;
            }

            const auto& cube = SpawnCube(particlesPerAxis, region.Center, region.Size);

            allParticles.Points.insert(allParticles.Points.end(), cube.Points.begin(), cube.Points.end());
            allParticles.Velocities.insert(allParticles.Velocities.end(), cube.Velocities.begin(), cube.Velocities.end());
        }

        return allParticles;
    }

    SpawnData SpawnCube(int numPerAxis, DirectX::SimpleMath::Vector3 center, float size) const 
    {
        if (numPerAxis <= 0)
        {
            return SpawnData{};
        }

        thread_local std::mt19937 randomGenerator(std::random_device{}());
        std::uniform_real_distribution<float> jitterDistribution(-JitterStrength, JitterStrength);

        if (numPerAxis == 1)
        {
            DirectX::SimpleMath::Vector3 jitter(
                jitterDistribution(randomGenerator),
                jitterDistribution(randomGenerator),
                jitterDistribution(randomGenerator));

            return SpawnData{ {center + jitter}, {InitialVelocity} };
        }

        int numPoints = numPerAxis * numPerAxis * numPerAxis;
        std::vector<DirectX::SimpleMath::Vector3> points(numPoints);
        std::vector<DirectX::SimpleMath::Vector3> velocities(numPoints);

        int i = 0;
        for (int x = 0; x < numPerAxis; x++)
        {
            for (int y = 0; y < numPerAxis; y++)
            {
                for (int z = 0; z < numPerAxis; z++)
                {
                    float tx = x / (numPerAxis - 1.f);
                    float ty = y / (numPerAxis - 1.f);
                    float tz = z / (numPerAxis - 1.f);

                    float px = (tx - 0.5f) * size + center.x;
                    float py = (ty - 0.5f) * size + center.y;
                    float pz = (tz - 0.5f) * size + center.z;

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
