// This is the common header for all our shaders using the Bruneton model (from definitions.glsl and function.glsl)


#ifndef BRUNETON_HLSL
#define BRUNETON_HLSL


#define IN(x) const in x
#define OUT(x) out x
#define TEMPLATE(x)
#define TEMPLATE_ARGUMENT(x)
#define assert(x)


#define D_UNREAL_ENGINE_4
#define sampler3D Texture3D<float4>
#define sampler2D Texture2D<float4>
#define vec4 float4
#define vec3 float3
#define vec2 float2

//#define mod fmod
float mod(float x, float y)
{
	return x - y * floor(x / y);
}

#define COMBINED_SCATTERING_TEXTURES



#include "BrunetonDefinitions.hlsl"

/*
cbuffer SKYATMOSPHERE_BUFFER : register(b1)
{
	// ... see BrunetonDefinitions.hlsl
}
*/

cbuffer SKYATMOSPHERE_SIDE_BUFFER : register(b2)
{
	float4x4 LuminanceFromRadiance;
	int      ScatteringOrder;
	int      UseSingleMieScatteringTexture;
	float2   pad01;
};

float3x3 getLuminanceFromRadiance()
{
	float3x3 mat;
	mat[0] = LuminanceFromRadiance[0].rgb;
	mat[1] = LuminanceFromRadiance[1].rgb;
	mat[2] = LuminanceFromRadiance[2].rgb;
	return mat;
}
/*
float4 GLSLTEXTURE(sampler3D tex, float3 uvw)
{
	return tex.Sample(samplerLinearClamp, uvw);
}
float4 GLSLTEXTURE(sampler2D tex, float2 uv)
{
	return tex.Sample(samplerLinearClamp, uv);
}
*/

#include "BrunetonFunctions.hlsl"


AtmosphereParameters GetAtmosphereParameters()
{
	AtmosphereParameters Parameters;
	Parameters.solar_irradiance = solar_irradiance;
	Parameters.sun_angular_radius = sun_angular_radius;
	Parameters.absorption_extinction = absorption_extinction;
	Parameters.mu_s_min = mu_s_min;

	//Parameters.rayleigh_density = rayleigh_density;
	//Parameters.mie_density = mie_density;
	//Parameters.absorption_density = absorption_density;
	Parameters.rayleigh_density.layers[0].width = rayleigh_density[0].x;
	Parameters.rayleigh_density.layers[0].exp_term = rayleigh_density[0].y;
	Parameters.rayleigh_density.layers[0].exp_scale = rayleigh_density[0].z;
	Parameters.rayleigh_density.layers[0].linear_term = rayleigh_density[0].w;
	Parameters.rayleigh_density.layers[0].constant_term = rayleigh_density[1].x;
	Parameters.rayleigh_density.layers[1].width = rayleigh_density[1].y;
	Parameters.rayleigh_density.layers[1].exp_term = rayleigh_density[1].z;
	Parameters.rayleigh_density.layers[1].exp_scale = rayleigh_density[1].w;
	Parameters.rayleigh_density.layers[1].linear_term = rayleigh_density[2].x;
	Parameters.rayleigh_density.layers[1].constant_term = rayleigh_density[2].y;
	Parameters.mie_density.layers[0].width = mie_density[0].x;
	Parameters.mie_density.layers[0].exp_term = mie_density[0].y;
	Parameters.mie_density.layers[0].exp_scale = mie_density[0].z;
	Parameters.mie_density.layers[0].linear_term = mie_density[0].w;
	Parameters.mie_density.layers[0].constant_term = mie_density[1].x;
	Parameters.mie_density.layers[1].width = mie_density[1].y;
	Parameters.mie_density.layers[1].exp_term = mie_density[1].z;
	Parameters.mie_density.layers[1].exp_scale = mie_density[1].w;
	Parameters.mie_density.layers[1].linear_term = mie_density[2].x;
	Parameters.mie_density.layers[1].constant_term = mie_density[2].y;
	Parameters.absorption_density.layers[0].width = absorption_density[0].x;
	Parameters.absorption_density.layers[0].exp_term = absorption_density[0].y;
	Parameters.absorption_density.layers[0].exp_scale = absorption_density[0].z;
	Parameters.absorption_density.layers[0].linear_term = absorption_density[0].w;
	Parameters.absorption_density.layers[0].constant_term = absorption_density[1].x;
	Parameters.absorption_density.layers[1].width = absorption_density[1].y;
	Parameters.absorption_density.layers[1].exp_term = absorption_density[1].z;
	Parameters.absorption_density.layers[1].exp_scale = absorption_density[1].w;
	Parameters.absorption_density.layers[1].linear_term = absorption_density[2].x;
	Parameters.absorption_density.layers[1].constant_term = absorption_density[2].y;

	Parameters.mie_phase_function_g = mie_phase_function_g;
	Parameters.rayleigh_scattering = rayleigh_scattering;
	Parameters.mie_scattering = mie_scattering;
	Parameters.mie_extinction = mie_extinction;
	Parameters.ground_albedo = ground_albedo;
	Parameters.bottom_radius = bottom_radius;
	Parameters.top_radius = top_radius;
	return Parameters;
}



// https://gist.github.com/wwwtyro/beecc31d65d1004f5a9d
// - r0: ray origin
// - rd: normalized ray direction
// - s0: sphere center
// - sR: sphere radius
// - Returns distance from r0 to first intersecion with sphere,
//   or -1.0 if no intersection.
float raySphereIntersect(float3 r0, float3 rd, float3 s0, float sR)
{
	float a = dot(rd, rd);
	vec3 s0_r0 = r0 - s0;
	float b = 2.0 * dot(rd, s0_r0);
	float c = dot(s0_r0, s0_r0) - (sR * sR);
	if (b*b - 4.0*a*c < 0.0) 
	{
		return -1.0;
	}
	return (-b - sqrt((b*b) - 4.0*a*c)) / (2.0*a);
}

#endif // BRUNETON_HLSL
