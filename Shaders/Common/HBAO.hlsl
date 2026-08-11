static const int BlockSizeX = 32;
static const int BlockSizeY = 32;
static const int NumDirections = 16;
static const int NumSamples = 4;

struct Params {
	float4x4   ProjMatrix;                 // offset:    0
	float4x4   InvProjMatrix;                 // offset:    0
	float4     Resolution;                    // offset:   64
	float2     ClipInfo;                      // offset:   80
	float      TraceRadius;                   // offset:   88
	float      MaxRadiusPixels;               // offset:   92
	float      DiscardDistance;               // offset:   96
	float3     Padding;                       // offset:  100
};

cbuffer __buffer0 : register(b0) {
	Params hbaoParams : packoffset(c0);
};


Texture2D<float> 	DepthMap  : register(t0); 
Texture2D<float4>	RandomMap  : register(t1); 
RWTexture2D<float> 	OutputAOMap	: register(u0);
 
SamplerState gsamPointClamp : register(s0);
SamplerState gsamLinearClamp : register(s1);
SamplerState LinearSampler : register(s2);
SamplerState WrapLinearSampler : register(s3);

static const float PI = 3.14159265;

float2 ViewportUVToScreenPos(float2 ViewportUV)
{
    return float2(2 * ViewportUV.x - 1, 1 - 2 * ViewportUV.y);
}

float3 GetViewPos(float2 TexCord)
{
	float Depth = DepthMap.SampleLevel(LinearSampler, TexCord, 0).r;
	float2 ScreenCoord = ViewportUVToScreenPos(TexCord);
	float4 NDCPos = float4(ScreenCoord, Depth, 1.0f);
	float4 ViewPos = mul(NDCPos, hbaoParams.InvProjMatrix);
	ViewPos = ViewPos / ViewPos.w;
	return float3(ViewPos.x, ViewPos.y, ViewPos.z);
}

float3 GetMinDiff(float3 p1, float3 p2, float3 p3)
{
	float3 V1 = p2 - p1;
	float3 V2 = p1 - p3;
	return (length(V1) < length(V2)) ? V1 : V2;
}

void ComputeSteps(out float2 stepSizeUv, out int numSteps, float rayRadiusPix, float rand)
{
	numSteps = min(NumSamples, rayRadiusPix);

	float stepSizePix = rayRadiusPix / (numSteps + 1);

	float maxNumSteps = hbaoParams.MaxRadiusPixels / stepSizePix;
	if (maxNumSteps < numSteps)
	{
		numSteps = floor(maxNumSteps + rand);
		numSteps = max(numSteps, 1);
		stepSizePix = hbaoParams.MaxRadiusPixels / numSteps;
	}

	stepSizeUv = stepSizePix * hbaoParams.Resolution.zw;
}

float2 RotateDirections(float2 Dir, float2 CosSin)
{
	// https://zhuanlan.zhihu.com/p/58517426
	return float2(
		Dir.x * CosSin.x - Dir.y * CosSin.y,
		Dir.x * CosSin.y + Dir.y * CosSin.x);
}

float Length2(float3 V)
{
	return dot(V, V);
}

float InvLength(float2 V)
{
	return 1.0f / sqrt(dot(V,V));
}

float Tangent(float3 V)
{
	return V.z * InvLength(V.xy);
}

float Tangent(float3 P, float3 S)
{
    return -(P.z - S.z) * InvLength(S.xy - P.xy);
}

float BiasedTangent(float3 V)
{
    // low-tessellation problem
	const float TanBias = tan(30.0 * PI / 180.0);
	return Tangent(V) + TanBias;
}

float TanToSin(float x)
{
	return x / sqrt(x * x + 1.0);
}

float Falloff(float d2)
{
	// The farther the distance, the smaller the contribution
	return saturate(1.0f - d2 * 1.0 / (hbaoParams.TraceRadius * hbaoParams.TraceRadius));
}

float2 SnapUVOffset(float2 uv)
{
	// Rounds the specified value to the nearest integer.
	return round(uv * hbaoParams.Resolution.xy) * hbaoParams.Resolution.zw;
}

float HorizonOcclusion(
	float2	TexCord,
	float2	deltaUV,
	float3	position,
	float3	dPdu,
	float3	dPdv,
	float	randstep,
	int	numSteps)
{
	float ao = 0;

	float2 uv = TexCord + SnapUVOffset(randstep * deltaUV);

	deltaUV = SnapUVOffset(deltaUV);

	float3 T = deltaUV.x * dPdu + deltaUV.y * dPdv;

	float tanH = BiasedTangent(T);
	float sinH = TanToSin(tanH);

	for (int count = 0; count < numSteps; ++count)
	{
		uv += deltaUV;
		float3 S = GetViewPos(uv);
		
		// p as the origin of the space
		float tanS = Tangent(position, S);
		float d2 = Length2(S - position);

		// only above Tangent can make contribution
		if (d2 < (hbaoParams.TraceRadius * hbaoParams.TraceRadius) && tanS > tanH)
		{
			float sinS = TanToSin(tanS);
                         // discontinuous problem
			ao += Falloff(d2) * (sinS - sinH);
			tanH = tanS;
			sinH = sinS;
		}
	}
	return ao;
}


[numthreads(BlockSizeX,BlockSizeY,1)] 
void CSMain( uint3 DTid : SV_DispatchThreadID) 
{	
	float2 TexCord = DTid.xy * hbaoParams.Resolution.zw;

	float3 position = GetViewPos(TexCord);
	[branch]
	if (position.z > hbaoParams.DiscardDistance )
	{
		return;
	}
	
	
	// The 0.5 uv range corresponds to the camera fov angle
	// ClipInfo = 1 / tan(theta/2)
	float2 rayRadiusUV = 0.5f * hbaoParams.ClipInfo * hbaoParams.TraceRadius / abs(position.z);
	
	// radius in pixels
	float rayRadiusPix = rayRadiusUV.x * hbaoParams.Resolution.x;
	[branch]
	if (rayRadiusPix > 1.0f)
	{		
		float3 positionLeft = GetViewPos(TexCord + float2(-hbaoParams.Resolution.z, 0));
		float3 positionRight = GetViewPos(TexCord + float2(hbaoParams.Resolution.z, 0));
		float3 positionTop = GetViewPos(TexCord + float2(0, hbaoParams.Resolution.w));
		float3 positionBottom = GetViewPos(TexCord + float2(0, -hbaoParams.Resolution.w));
	
		// used to calculate tangent
		float3 dPdu = GetMinDiff(position, positionRight, positionLeft);
		float3 dPdv = GetMinDiff(position, positionTop, positionBottom) * (hbaoParams.Resolution.y * 1.0 / hbaoParams.Resolution.x);	
	
		float ao = 0.0;
		int numSteps;
		float2 stepSizeUV;
		// sample random vector need scale
		const float2 NoiseScale = float2(hbaoParams.Resolution.x * 0.25f, hbaoParams.Resolution.y * 0.25f);
		float3 RandomVec = RandomMap.SampleLevel(WrapLinearSampler, TexCord * NoiseScale, 0).rgb;
		ComputeSteps(stepSizeUV, numSteps, rayRadiusPix, RandomVec.z);

		const float alpha = 2.0 * PI / NumDirections;
		[unroll]
		for (int i = 0; i < NumDirections; ++i)
		{
			float theta = alpha * i;
			float2 dir = RotateDirections(float2(cos(theta), sin(theta)), RandomVec.xy);
			float2 deltaUV = dir * stepSizeUV;

			// accumulate occlusion
			ao += HorizonOcclusion(
				TexCord,
				deltaUV,
				position,
				dPdu,
				dPdv,
				RandomVec.z,
				numSteps);
		}
		
		ao = 1.0f - (ao / NumDirections * 1.9f);
		OutputAOMap[DTid.xy] = ao;
	}
}
