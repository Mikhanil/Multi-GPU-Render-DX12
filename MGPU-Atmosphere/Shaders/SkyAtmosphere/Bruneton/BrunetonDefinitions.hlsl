// BrunetonDefinitions.hlsl
// HLSL (SM5.1) translation of atmosphere/definitions.glsl
// Based on the provided GLSL sources.

#ifndef BRUNETON_DEFINITIONS_HLSL
#define BRUNETON_DEFINITIONS_HLSL

// --- Physical quantity aliases ---
#define Length float
#define Wavelength float
#define Angle float
#define SolidAngle float
#define Power float
#define LuminousPower float

#define Number float
#define InverseLength float
#define Area float
#define Volume float
#define NumberDensity float
#define Irradiance float
#define Radiance float
#define SpectralPower float
#define SpectralIrradiance float
#define SpectralRadiance float
#define SpectralRadianceDensity float
#define ScatteringCoefficient float
#define InverseSolidAngle float
#define LuminousIntensity float
#define Luminance float
#define Illuminance float

// Vector / spectrum aliases
#define AbstractSpectrum float3
#define DimensionlessSpectrum float3
#define PowerSpectrum float3
#define IrradianceSpectrum float3
#define RadianceSpectrum float3
#define RadianceDensitySpectrum float3
#define ScatteringSpectrum float3

#define Position float3
#define Direction float3
#define Luminance3 float3
#define Illuminance3 float3

// --- Texture / sampler aliases ---
// In GLSL these were sampler2D/sampler3D types. In HLSL we use typed textures.
// Choose float4 storage for textures (RGB in .xyz or .rgb mapping). Adjust if needed.
#define TransmittanceTexture Texture2D<float4>
#define AbstractScatteringTexture Texture3D<float4>
#define ReducedScatteringTexture Texture3D<float4>
#define ScatteringTexture Texture3D<float4>
#define ScatteringDensityTexture Texture3D<float4>
#define IrradianceTexture Texture2D<float4>

// Sampler - user should bind a sampler (e.g. linear) in the effect code / API.
// Provide a default name here; bind/register from host application / root signature.
SamplerState g_samplerLinear : register(s0);

// --- Physical constants ---
static const float PI = 3.14159265358979323846f;

static const Length m = 1.0f;
static const Wavelength nm = 1.0f;
static const Angle rad = 1.0f;
static const SolidAngle sr = 1.0f;
static const Power watt = 1.0f;
static const LuminousPower lm = 1.0f;

static const Length km = 1000.0f * m;
static const Area m2 = m * m;
static const Volume m3 = m * m * m;
static const Angle pi = PI * rad;
static const Angle deg = pi / 180.0f;
static const Irradiance watt_per_square_meter = watt / m2;
static const Radiance watt_per_square_meter_per_sr = watt / (m2 * sr);
static const SpectralIrradiance watt_per_square_meter_per_nm = watt / (m2 * nm);
static const SpectralRadiance watt_per_square_meter_per_sr_per_nm =
    watt / (m2 * sr * nm);
static const SpectralRadianceDensity watt_per_cubic_meter_per_sr_per_nm =
    watt / (m3 * sr * nm);
static const LuminousIntensity cd = lm / sr;
static const LuminousIntensity kcd = 1000.0f * cd;
static const Luminance cd_per_square_meter = cd / m2;
static const Luminance kcd_per_square_meter = kcd / m2;

// --- Density profile layer / profile / atmosphere parameter structs ---
// Mirror the GLSL structs; HLSL supports arrays in structs.
struct DensityProfileLayer
{
    Length width;
    Number exp_term;
    InverseLength exp_scale;
    InverseLength linear_term;
    Number constant_term;
};

struct DensityProfile
{
    // two layers as in GLSL
    DensityProfileLayer layers[2];
};

struct AtmosphereParameters
{
    // Solar irradiance at top of atmosphere (spectral, stored as float3)
    IrradianceSpectrum solar_irradiance;
    // Sun angular radius (radians)
    Angle sun_angular_radius;
    // Planet center to bottom/top of atmosphere
    Length bottom_radius;
    Length top_radius;
    // Density profiles
    DensityProfile rayleigh_density;
    ScatteringSpectrum rayleigh_scattering; // spectral scattering at bottom
    DensityProfile mie_density;
    ScatteringSpectrum mie_scattering;      // spectral scattering at bottom
    ScatteringSpectrum mie_extinction;      // spectral extinction at bottom
    Number mie_phase_function_g;            // asymmetry parameter g
    DensityProfile absorption_density;
    ScatteringSpectrum absorption_extinction;
    DimensionlessSpectrum ground_albedo;
    Number mu_s_min; // cosine of max Sun zenith angle to precompute
};


// Moved from Brunenot.hlsl
// MUST match SKYATMOSPHERE_BUFFER in SkyAtmosphereCommon.hlsl
cbuffer SKYATMOSPHERE_BUFFER : register(b1)
{
	//
	// From AtmosphereParameters
	//

	/*float3*/	IrradianceSpectrum solar_irradiance;
	/*float*/	Angle sun_angular_radius;

	/*float3*/	ScatteringSpectrum absorption_extinction;
	/*float*/	Number mu_s_min;

	/*float3*/	ScatteringSpectrum rayleigh_scattering;
	/*float*/	Number mie_phase_function_g;

	/*float3*/	ScatteringSpectrum mie_scattering;
	/*float*/	Length bottom_radius;

	/*float3*/	ScatteringSpectrum mie_extinction;
	/*float*/	Length top_radius;

    float3 mie_absorption;
    float pad00;

	/*float3*/	DimensionlessSpectrum ground_albedo;
    float pad0;

	/*float10*/	//DensityProfile rayleigh_density;
	/*float10*/	//DensityProfile mie_density;
	/*float10*/	//DensityProfile absorption_density;
    float4 rayleigh_density[3];
    float4 mie_density[3];
    float4 absorption_density[3];

	//
	// Add generated static header constant
	//

    int TRANSMITTANCE_TEXTURE_WIDTH;
    int TRANSMITTANCE_TEXTURE_HEIGHT;
    int IRRADIANCE_TEXTURE_WIDTH;
    int IRRADIANCE_TEXTURE_HEIGHT;

    int SCATTERING_TEXTURE_R_SIZE;
    int SCATTERING_TEXTURE_MU_SIZE;
    int SCATTERING_TEXTURE_MU_S_SIZE;
    int SCATTERING_TEXTURE_NU_SIZE;

    float3 SKY_SPECTRAL_RADIANCE_TO_LUMINANCE;
    float pad3;
    float3 SUN_SPECTRAL_RADIANCE_TO_LUMINANCE;
    float pad4;

	//
	// Other globals
	//
    float4x4 gSkyViewProjMat;
    float4x4 gSkyInvViewProjMat;
    float4x4 gSkyInvProjMat;
    float4x4 gSkyInvViewMat;
    float4x4 gShadowmapViewProjMat;

    float3 camera;
    float pad5;
    float3 sun_direction;
    float pad6;
    float3 view_ray;
    float pad7;
};



// --- Utility macros / small helpers ---
// HLSL has no clamp overload ambiguity problems, but keep typedef wrappers.
// If you want to place these in a namespace-like prefix, do so in your code base.

// Note: assert() not standard in HLSL runtime; omit ASSERTs from GLSL. Use debug-time validation on CPU.

// End include guard
#endif // BRUNETON_DEFINITIONS_HLSL
