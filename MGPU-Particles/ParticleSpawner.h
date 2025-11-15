#pragma once
#include <cstdint>
#include <SimpleMath.h>

class ParticleSpawner
{
public:    
    ParticleSpawner() = default;

    struct SpawnRegion
    {
        DirectX::SimpleMath::Vector3 Center;
        float Size;

        float Volume() const { return Size*Size*Size; }

        int CalculateParticleCountPerAxis(uint32_t particleDensity) const
        {
            int targetParticleCount = static_cast<int>(Volume() * particleDensity);
            int particlesPerAxis = cbrt(targetParticleCount);
            return particlesPerAxis;
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

        for (auto& region : SpawnRegions)
        {
            int particlesPerAxis = region.CalculateParticleCountPerAxis(ParticleSpawnDensity);
            const auto& cube = SpawnCube(particlesPerAxis, region.Center, region.Size);

            allParticles.Points.insert(allParticles.Points.end(), cube.Points.begin(), cube.Points.end());
            allParticles.Velocities.insert(allParticles.Velocities.end(), cube.Velocities.begin(), cube.Velocities.end());
        }

        return allParticles;
    }

    SpawnData SpawnCube(int numPerAxis, DirectX::SimpleMath::Vector3 center, float size) const 
    {
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
                    //DirectX::SimpleMath::Vector3 jitter = bbbb
                    points[i] = DirectX::SimpleMath::Vector3(px, py, pz);
                    velocities[i] = InitialVelocity;
                    i++;
                }
            }
        }
        return SpawnData{ points, velocities };
    }
    
public:
    uint32_t ParticleSpawnDensity = 500;
    DirectX::SimpleMath::Vector3 InitialVelocity;
    float JitterStrength;

    std::vector<SpawnRegion> SpawnRegions; 
};
