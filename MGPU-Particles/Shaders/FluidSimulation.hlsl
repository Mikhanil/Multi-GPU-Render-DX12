#include "./FluidMath.hlsl"
#include "./SpatialHash.hlsl"

//static const int ThreadGroupSize = 512;
//#define GROUP_SIZE 512

// Settings
//struct SimulationConstants
cbuffer SimulationConstants : register(b0)
{  
    uint numParticles;
    float gravity;
    float deltaTime;
    float simTime;
    // 16 bytes
    float collisionDamping;
    float smoothingRadius;
    float targetDensity;
    float pressureMultiplier;
    // 16 bytes
    float nearPressureMultiplier;
    float viscosityStrength;
    float2 _padding0;
    // 16 bytes
    float3 boundsSize;
    float _padding1;
    // 16 bytes

    float4x4 localToWorld;
    // 16 bytes
    float4x4 worldToLocal;
    // 16 bytes

    float2 interactionInputPoint;
    float interactionInputStrength;
    float interactionInputRadius;
    // 16 bytes
};

// ConstantBuffer<SimulationConstants> constants : register(b0);

// Buffers
RWStructuredBuffer<float3> Positions            : register(u0);
RWStructuredBuffer<float3> PredictedPositions   : register(u1);
RWStructuredBuffer<float3> Velocities           : register(u2);
RWStructuredBuffer<float2> Densities            : register(u3); // Density, Near Density

RWStructuredBuffer<float3> SortTarget_Positions             : register(u4);
RWStructuredBuffer<float3> SortTarget_PredictedPositions    : register(u5);
RWStructuredBuffer<float3> SortTarget_Velocities            : register(u6);

// Spatial hashing
RWStructuredBuffer<uint>    SpatialKeys    : register(u7);
RWStructuredBuffer<uint>    SpatialOffsets : register(u8);
StructuredBuffer<uint>      SortedIndices  : register(t0);

float PressureFromDensity(float density)
{
    return (density - targetDensity) * pressureMultiplier;
}

float NearPressureFromDensity(float nearDensity)
{
    return nearDensity * nearPressureMultiplier;
}


float Remap01(float val, float minVal, float maxVal)
{
    return saturate((val - minVal) / (maxVal - minVal));
}

void ResolveCollisions(inout float3 pos, inout float3 vel, float collisionDamping)
{
    // Transform position/velocity to the local space of the bounding box (scale not included)
    float3 posLocal = mul(worldToLocal, float4(pos, 1)).xyz;
    float3 velocityLocal = mul(worldToLocal, float4(vel, 0)).xyz;

    // Calculate distance from box on each axis (negative values are inside box)
    const float3 halfSize = 0.5;
    const float3 edgeDst = halfSize - abs(posLocal);

    // Resolve collisions
    if (edgeDst.x <= 0)
    {
        posLocal.x = halfSize.x * sign(posLocal.x);
        velocityLocal.x *= -1 * collisionDamping;
    }
    if (edgeDst.y <= 0)
    {
        posLocal.y = halfSize.y * sign(posLocal.y);
        velocityLocal.y *= -1 * collisionDamping;
    }
    if (edgeDst.z <= 0)
    {
        posLocal.z = halfSize.z * sign(posLocal.z);
        velocityLocal.z *= -1 * collisionDamping;
    }

    // Transform resolved position/velocity back to world space
    pos = mul(localToWorld, float4(posLocal, 1)).xyz;
    vel = mul(localToWorld, float4(velocityLocal, 0)).xyz;
}

[numthreads(GROUP_SIZE, 1, 1)]
void ExternalForces(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numParticles)
        return;

    // External forces (gravity)
    Velocities[id.x] += float3(0, gravity, 0) * deltaTime;

    // Predict
    PredictedPositions[id.x] = Positions[id.x] + Velocities[id.x] * 1 / 120.0;
}

[numthreads(GROUP_SIZE, 1, 1)]
void UpdateSpatialHash(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numParticles)
        return;

    uint index = id.x;
    int3 cell = GetCell3D(PredictedPositions[index], smoothingRadius);
    uint hash = HashCell3D(cell);
    uint key = KeyFromHash(hash, numParticles);

    SpatialKeys[id.x] = key;
}

[numthreads(GROUP_SIZE, 1, 1)]
void Reorder(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numParticles)
        return;
    uint sortedIndex = SortedIndices[id.x];
    SortTarget_Positions[id.x] = Positions[sortedIndex];
    SortTarget_PredictedPositions[id.x] = PredictedPositions[sortedIndex];
    SortTarget_Velocities[id.x] = Velocities[sortedIndex];
}

[numthreads(GROUP_SIZE, 1, 1)]
void ReorderCopyBack(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numParticles)
        return;

    Positions[id.x] = SortTarget_Positions[id.x];
    PredictedPositions[id.x] = SortTarget_PredictedPositions[id.x];
    Velocities[id.x] = SortTarget_Velocities[id.x];
}

float2 CalculateDensitiesAtPoint(float3 pos)
{
    int3 originCell = GetCell3D(pos, smoothingRadius);
    float sqrRadius = smoothingRadius * smoothingRadius;
    float density = 0;
    float nearDensity = 0;

    // Neighbour search
    for (int i = 0; i < 27; i++)
    {
        uint hash = HashCell3D(originCell + offsets3D[i]);
        uint key = KeyFromHash(hash, numParticles);
        uint currIndex = SpatialOffsets[key];

        while (currIndex < numParticles)
        {
            uint neighbourIndex = currIndex;
            currIndex++;

            uint neighbourKey = SpatialKeys[neighbourIndex];
            // Exit if no longer looking at correct bin
            if (neighbourKey != key)
                break;

            float3 neighbourPos = PredictedPositions[neighbourIndex];
            float3 offsetToNeighbour = neighbourPos - pos;
            float sqrDstToNeighbour = dot(offsetToNeighbour, offsetToNeighbour);

            // Skip if not within radius
            if (sqrDstToNeighbour > sqrRadius)
                continue;

            // Calculate density and near density
            float dst = sqrt(sqrDstToNeighbour);
            density += DensityKernel(dst, smoothingRadius);
            nearDensity += NearDensityKernel(dst, smoothingRadius);
        }
    }

    return float2(density, nearDensity);
}

[numthreads(GROUP_SIZE, 1, 1)]
void CalculateDensities(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numParticles)
        return;

    float3 pos = PredictedPositions[id.x];
    float2 densities = CalculateDensitiesAtPoint(pos);

    Densities[id.x] = densities;
}

// PCG (permuted congruential generator). Thanks to:
// www.pcg-random.org and www.shadertoy.com/view/XlGcRh
uint NextRandom(inout uint state)
{
    state = state * 747796405 + 2891336453;
    uint result = ((state >> ((state >> 28) + 4)) ^ state) * 277803737;
    result = (result >> 22) ^ result;
    return result;
}

float RandomValue(inout uint state)
{
    return NextRandom(state) / 4294967295.0; // 2^32 - 1
}

// Thanks to https://math.stackexchange.com/a/4112622
// Calculates arbitrary normalized vector that is perpendicular to the given direction
float3 CalculateOrthonormal(float3 dir)
{
    float a = sign((sign(dir.x) + 0.5) * (sign(dir.z) + 0.5));
    float b = sign((sign(dir.y) + 0.5) * (sign(dir.z) + 0.5));
    float3 orthoVec = float3(a * dir.z, b * dir.z, -a * dir.x - b * dir.y);
    return normalize(orthoVec);
}


[numthreads(GROUP_SIZE, 1, 1)]
void CalculatePressureForce(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numParticles)
        return;

    // Calculate pressure
    float density = Densities[id.x][0];
    float densityNear = Densities[id.x][1];
    float pressure = PressureFromDensity(density);
    float nearPressure = NearPressureFromDensity(densityNear);
    float3 pressureForce = 0;
    float3 velocity = Velocities[id.x];

    float3 pos = PredictedPositions[id.x];
    int3 originCell = GetCell3D(pos, smoothingRadius);
    float sqrRadius = smoothingRadius * smoothingRadius;
    int neighbourCount = 0;

    // Foam variables
    float weightedVelocityDifference = 0;


    // Neighbour search
    for (int i = 0; i < 27; i++)
    {
        uint hash = HashCell3D(originCell + offsets3D[i]);
        uint key = KeyFromHash(hash, numParticles);
        uint currIndex = SpatialOffsets[key];

        while (currIndex < numParticles)
        {
            uint neighbourIndex = currIndex;
            currIndex++;

            // Skip if looking at self
            if (neighbourIndex == id.x) continue;

            uint neighbourKey = SpatialKeys[neighbourIndex];
            // Exit if no longer looking at correct bin
            if (neighbourKey != key) break;

            float3 neighbourPos = PredictedPositions[neighbourIndex];
            float3 offsetToNeighbour = neighbourPos - pos;
            float sqrDstToNeighbour = dot(offsetToNeighbour, offsetToNeighbour);

            // Skip if not within radius
            if (sqrDstToNeighbour > sqrRadius) continue;

            // Calculate pressure force
            float densityNeighbour = Densities[neighbourIndex][0];
            float nearDensityNeighbour = Densities[neighbourIndex][1];
            float neighbourPressure = PressureFromDensity(densityNeighbour);
            float neighbourPressureNear = NearPressureFromDensity(nearDensityNeighbour);

            float sharedPressure = (pressure + neighbourPressure) / 2;
            float sharedNearPressure = (nearPressure + neighbourPressureNear) / 2;

            float dstToNeighbour = sqrt(sqrDstToNeighbour);
            float3 dirToNeighbour = dstToNeighbour > 0 ? offsetToNeighbour / dstToNeighbour : float3(0, 1, 0);
            neighbourCount++;

            // Update pressure force
            pressureForce += dirToNeighbour * DensityDerivative(dstToNeighbour, smoothingRadius) * sharedPressure / densityNeighbour;
            pressureForce += dirToNeighbour * NearDensityDerivative(dstToNeighbour, smoothingRadius) * sharedNearPressure / nearDensityNeighbour;

            // ---- White Particle 'Trapped Air' Calculation ----
            float3 relativeVelocity = velocity - Velocities[neighbourIndex];
            float relativeVelocityMagnitude = length(relativeVelocity);
            float3 relativeVelocityDir = relativeVelocity / max(0.000001, relativeVelocityMagnitude);
            // 0 if moving in opposite directions; up to 2 if moving directly toward one another
            float convergeWeight = 1 - dot(relativeVelocityDir, -dirToNeighbour);
            // 1 when distance between particles is 0, down to 0 when distance reaches the smoothing radius
            float influence = 1 - min(1, dstToNeighbour / smoothingRadius);
            // Sum up weighted velocity diff between current particle and each of its surrounding neighbours
            weightedVelocityDifference += relativeVelocityMagnitude * convergeWeight * influence;
        }
    }

    float3 acceleration = pressureForce / density;
    float3 velocityNew = velocity + acceleration * deltaTime;
    Velocities[id.x] = velocityNew;

    // Quick test -- apply some drag to airborne fluid particles
    if (neighbourCount < 8)
    {
        Velocities[id.x] -= Velocities[id.x] * deltaTime * 0.75;
    }
}

[numthreads(GROUP_SIZE, 1, 1)]
void CalculateViscosity(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numParticles)
        return;

    float3 pos = PredictedPositions[id.x];
    int3 originCell = GetCell3D(pos, smoothingRadius);
    float sqrRadius = smoothingRadius * smoothingRadius;

    float3 viscosityForce = 0;
    float3 velocity = Velocities[id.x];

    // Neighbour search
    for (int i = 0; i < 27; i++)
    {
        uint hash = HashCell3D(originCell + offsets3D[i]);
        uint key = KeyFromHash(hash, numParticles);
        uint currIndex = SpatialOffsets[key];

        while (currIndex < numParticles)
        {
            uint neighbourIndex = currIndex;
            currIndex++;

            uint neighbourKey = SpatialKeys[neighbourIndex];
            // Exit if no longer looking at correct bin
            if (neighbourKey != key)
                break;


            // Skip if looking at self
            if (neighbourIndex == id.x)
                continue;

            float3 neighbourPos = PredictedPositions[neighbourIndex];
            float3 offsetToNeighbour = neighbourPos - pos;
            float sqrDstToNeighbour = dot(offsetToNeighbour, offsetToNeighbour);

            // Skip if not within radius
            if (sqrDstToNeighbour > sqrRadius)
                continue;

            // Calculate viscosity
            float dst = sqrt(sqrDstToNeighbour);
            float3 neighbourVelocity = Velocities[neighbourIndex];
            viscosityForce += (neighbourVelocity - velocity) * SmoothingKernelPoly6(dst, smoothingRadius);
        }
    }
    Velocities[id.x] += viscosityForce * viscosityStrength * deltaTime;
}

[numthreads(GROUP_SIZE, 1, 1)]
void UpdatePositions(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= numParticles)
        return;

    float3 vel = Velocities[id.x];
    float3 pos = Positions[id.x];
    pos += vel * deltaTime;

    ResolveCollisions(pos, vel, collisionDamping);

    // Write results
    Positions[id.x] = pos;
    Velocities[id.x] = vel;
}

/*
[numthreads(8, 8, 8)]
void UpdateDensityTexture(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= densityMapSize.x || id.y >= densityMapSize.y || id.z >= densityMapSize.z)
        return;

    // Convert threadID to a world-space position, and sample the fluid density at that point
    float3 texturePos = id / (densityMapSize - 1.0);
    float3 worldPos = (texturePos - 0.5) * boundsSize;
    DensityMap[id] = CalculateDensitiesAtPoint(worldPos)[0];
}
*/

