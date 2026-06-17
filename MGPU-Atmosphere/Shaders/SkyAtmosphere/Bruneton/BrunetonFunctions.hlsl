// ============================================================================
// Utilities + Transmittance Functions
// HLSL 5.1 translation of the corresponding GLSL code
// ============================================================================

#ifndef BRUNETON_FUNCTIONS_HLSL
#define BRUNETON_FUNCTIONS_HLSL

#include "BrunetonDefinitions.hlsl"

// -----------------------------------------------
// Clamp into valid range
// -----------------------------------------------
Number ClampCosine(Number mu)
{
    return clamp(mu, Number(-1.0f), Number(1.0f));
}

Length ClampDistance(Length d)
{
    return max(d, 0.0f * m);
}

Length ClampRadius(AtmosphereParameters atmosphere, Length r)
{
    return clamp(r, atmosphere.bottom_radius, atmosphere.top_radius);
}

// -----------------------------------------------
// Save sqrt to avoid negative argument issues
// -----------------------------------------------
Length SafeSqrt(Area x)
{
    return sqrt(max(x, 0.0f * m2));
}

// -----------------------------------------------
// Distance from a point at radius r to top sphere
// Ray parameterized by direction mu = cos(theta)
// -----------------------------------------------
Length DistanceToTopAtmosphereBoundary(AtmosphereParameters atmosphere,
                                      Length r,
                                      Number mu)
{
    // assert(r <= atmosphere.top_radius);
    // assert(mu >= -1.0f && mu <= 1.0f);
    Area discriminant =
        r * r * (mu * mu - 1.0f) +
        atmosphere.top_radius * atmosphere.top_radius;

    return ClampDistance(-r * mu + SafeSqrt(discriminant));
}

// -----------------------------------------------
// Distance to the bottom boundary (planet ground)
// If no intersection: returns +infinity
// -----------------------------------------------
Length DistanceToBottomAtmosphereBoundary(AtmosphereParameters atmosphere,
                                          Length r,
                                          Number mu)
{
    // assert(r <= atmosphere.top_radius);
    // assert(mu >= -1.0f && mu <= 1.0f);
    Area discriminant =
        r * r * (mu * mu - 1.0f) +
        atmosphere.bottom_radius * atmosphere.bottom_radius;

    if (discriminant >= 0.0f)
    {
        return ClampDistance(-r * mu - SafeSqrt(discriminant));
    }

    // No hit: return +inf
    return 1e30f;
}

// -----------------------------------------------
// Whether ray hits ground before going upward
// -----------------------------------------------
bool RayIntersectsGround(AtmosphereParameters atmosphere,
                         Length r,
                         Number mu)
{
    // assert(r >= atmosphere.bottom_radius);
    // assert(mu >= -1.0 && mu <= 1.0);
    return (mu < 0.0f) && (r * r * (mu * mu - 1.0f)) +
      (atmosphere.bottom_radius * atmosphere.bottom_radius >= 0.0f * m2);
    // return DistanceToBottomAtmosphereBoundary(atmosphere, r, mu) < 0.0f;
}

// -----------------------------------------------
// Compute density at given altitude
// profile.layers[0] is exponential
// profile.layers[1] is linear-exponential
// -----------------------------------------------

Number GetLayerDensity(DensityProfileLayer layer, Length altitude)
{
    Number density = layer.exp_term * exp(layer.exp_scale * altitude) +
        layer.linear_term * altitude + layer.constant_term;
    return clamp(density, Number(0.0f), Number(1.0f));
}


Number GetProfileDensity(DensityProfile profile, Length altitude)
{
    if (altitude < profile.layers[0].width)
    {
        return GetLayerDensity(profile.layers[0], altitude);
    }
    else
    {
        altitude -= profile.layers[0].width;
        return GetLayerDensity(profile.layers[1], altitude);
    }
}

// -----------------------------------------------
// Integral of density along ray to top boundary
// Used by analytic transmittance computation
// -----------------------------------------------
Length ComputeOpticalLengthToTopAtmosphereBoundary(
    AtmosphereParameters atmosphere,
    DensityProfile profile,
    Length r,
    Number mu)
{
    // assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    // assert(mu >= -1.0 && mu <= 1.0);
    // Number of integration steps (same as GLSL)
    const int SAMPLE_COUNT = 500;

    // Compute distance to top atmosphere boundary
    // Raymarch using trapezoidal rule
    Length dx = DistanceToTopAtmosphereBoundary(atmosphere, r, mu) / SAMPLE_COUNT;

    Length integral = 0.0f * m;

    for (int i = 0; i <= SAMPLE_COUNT; ++i)
    {
        Length d_i = Number(i) * dx;
        // Position along ray
        Length r_i = sqrt(d_i * d_i + 2.0f * r * mu * d_i + r * r);
        // Altitude
        Length altitude_i = r_i - atmosphere.bottom_radius;

        Number y_i = GetProfileDensity(profile, altitude_i);

        // Trapezoidal weight
        Number weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5f : 1.0f;
        integral += y_i * weight_i;
    }

    return integral * dx;
}

// -----------------------------------------------
// Direct transmittance using analytic integral
// tau = exp(- (rayleigh + mie + ozone)*optical_length )
// -----------------------------------------------
float3 ComputeTransmittanceToTopAtmosphereBoundary(
    AtmosphereParameters atmosphere,
    Length r,
    float mu)
{
    // assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    // assert(mu >= -1.0 && mu <= 1.0);
    
    float rayleigh =
        ComputeOpticalLengthToTopAtmosphereBoundary(
            atmosphere,
            atmosphere.rayleigh_density,
            r, mu);

    float mie =
        ComputeOpticalLengthToTopAtmosphereBoundary(
            atmosphere,
            atmosphere.mie_density,
            r, mu);

    float ozone =
        ComputeOpticalLengthToTopAtmosphereBoundary(
            atmosphere,
            atmosphere.absorption_density,
            r, mu);

    // Per–wavelength extinction:
    float3 tau =
        -(atmosphere.rayleigh_scattering * rayleigh +
           atmosphere.mie_extinction * mie +
           atmosphere.absorption_extinction * ozone);

    return exp(tau);
}

// ============================================================================
// Texture coordinate mapping for transmittance LUT
// ============================================================================

Number GetTextureCoordFromUnitRange(Number x, int texture_size)
{
    return 0.5 / Number(texture_size) + x * (1.0 - 1.0 / Number(texture_size));
}

Number GetUnitRangeFromTextureCoord(Number u, int texture_size)
{
    return (u - 0.5 / Number(texture_size)) / (1.0 - 1.0 / Number(texture_size));
}

// -----------------------------------------------
// r, mu → UV for 2D transmittance texture
// -----------------------------------------------
float2 GetTransmittanceTextureUvFromRMu(
    AtmosphereParameters atmosphere,
    Length r,
    Number mu)
{
    // assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    // assert(mu >= -1.0 && mu <= 1.0);
    Length H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
                   atmosphere.bottom_radius * atmosphere.bottom_radius);

    Length rho =
        SafeSqrt(r * r - atmosphere.bottom_radius * atmosphere.bottom_radius);

    float d =
        DistanceToTopAtmosphereBoundary(atmosphere, r, mu);

    float d_min = atmosphere.top_radius - r;
    float d_max = rho + H;

    float x_mu = (d - d_min) / (d_max - d_min);
    float x_r = rho / H;

    return float2(
        GetTextureCoordFromUnitRange(x_mu, TRANSMITTANCE_TEXTURE_WIDTH),
        GetTextureCoordFromUnitRange(x_r, TRANSMITTANCE_TEXTURE_HEIGHT)
    );
}

void GetRMuFromTransmittanceTextureUv(AtmosphereParameters atmosphere,
    float2 uv, out Length r, out Number mu)
{
    // assert(uv.x >= 0.0 && uv.x <= 1.0);
    // assert(uv.y >= 0.0 && uv.y <= 1.0);
    Number x_mu = GetUnitRangeFromTextureCoord(uv.x, TRANSMITTANCE_TEXTURE_WIDTH);
    Number x_r = GetUnitRangeFromTextureCoord(uv.y, TRANSMITTANCE_TEXTURE_HEIGHT);
    // Distance to top atmosphere boundary for a horizontal ray at ground level.
    Length H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
        atmosphere.bottom_radius * atmosphere.bottom_radius);
    // Distance to the horizon, from which we can compute r:
    Length rho = H * x_r;
    r = sqrt(rho * rho + atmosphere.bottom_radius * atmosphere.bottom_radius);
    // Distance to the top atmosphere boundary for the ray (r,mu), and its minimum
    // and maximum values over all mu - obtained for (r,1) and (r,mu_horizon) -
    // from which we can recover mu:
    Length d_min = atmosphere.top_radius - r;
    Length d_max = rho + H;
    Length d = d_min + x_mu * (d_max - d_min);
    mu = d == 0.0 * m ? Number(1.0) : (H * H - rho * rho - d * d) / (2.0 * r * d);
    mu = ClampCosine(mu);
}

DimensionlessSpectrum ComputeTransmittanceToTopAtmosphereBoundaryTexture(
    AtmosphereParameters atmosphere, float2 frag_coord)
{
    const float2 TRANSMITTANCE_TEXTURE_SIZE =
      float2(TRANSMITTANCE_TEXTURE_WIDTH, TRANSMITTANCE_TEXTURE_HEIGHT);
  Length r;
  Number mu;
    GetRMuFromTransmittanceTextureUv(
      atmosphere, frag_coord / TRANSMITTANCE_TEXTURE_SIZE, r, mu);
    return ComputeTransmittanceToTopAtmosphereBoundary(atmosphere, r, mu);
}

DimensionlessSpectrum GetTransmittanceToTopAtmosphereBoundary(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    Length r, Number mu)
{
    // assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    float2 uv = GetTransmittanceTextureUvFromRMu(atmosphere, r, mu);
    return DimensionlessSpectrum(transmittance_texture.Sample(g_samplerLinear, uv).rgb);
}

// ============================================================================
// Transmittance texture lookup
// ============================================================================

// Modern HLSL: SampleLevel is safer for LUTs
// Texture2D<float3>  transmittanceTex → typdef in definitions.hlsl
// Use .xyz components
float3 GetTransmittance(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    Length r,
    Number mu,
    Length d,
    bool ray_r_mu_intersects_ground)
{
    // assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    // assert(mu >= -1.0 && mu <= 1.0);
    // assert(d >= 0.0 * m);
    
    Length r_d = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
    Number mu_d = ClampCosine((r * mu + d) / r_d);

    float3 t_num, t_den, t;
    if (ray_r_mu_intersects_ground)
    {
        t_num = GetTransmittanceToTopAtmosphereBoundary(
            atmosphere, transmittance_texture, r_d, -mu_d);
        t_den = GetTransmittanceToTopAtmosphereBoundary(
            atmosphere, transmittance_texture, r, -mu);
    }
    else
    {
        t_num = GetTransmittanceToTopAtmosphereBoundary(
            atmosphere, transmittance_texture, r, mu);
        t_den = GetTransmittanceToTopAtmosphereBoundary(
            atmosphere, transmittance_texture, r_d, mu_d);
    }

    // avoid division by zero: clamp denominator components
    t = t_num / max(t_den, float3(1e-6f, 1e-6f, 1e-6f));
    return min(t, DimensionlessSpectrum(1.0, 1.0, 1.0));
}

// --- Transmittance to the Sun (integrates Sun disc fraction) --------------
// Uses smoothstep to approximate disc area.
DimensionlessSpectrum GetTransmittanceToSun(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    Length r,
    Number mu_s)
{
    // sin(theta_h) = R_bottom / r
    float sin_theta_h = atmosphere.bottom_radius / r;
    // cos(theta_h) is negative (we use the convention from GLSL: horizon)
    float cos_theta_h = -sqrt(max(1.0 - sin_theta_h * sin_theta_h, 0.0));

    float3 trans = GetTransmittanceToTopAtmosphereBoundary(
        atmosphere, transmittance_texture, r, mu_s);

    // smoothstep over fraction of sun disc above horizon
    float edge = sin_theta_h * atmosphere.sun_angular_radius / rad; // rad conversion
    float frac = smoothstep(-edge, edge, mu_s - cos_theta_h);

    return trans * frac;
}

// --- ComputeSingleScatteringIntegrand --------------------------------------
// Computes dimensionless rayleigh and mie contributions at a sample point
// (omits solar_irradiance and scattering coefficients; those are multiplied later).
void ComputeSingleScatteringIntegrand(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    Length r,
    Number mu,
    Number mu_s,
    Number nu,
    Length d,
    bool ray_r_mu_intersects_ground,
    out DimensionlessSpectrum rayleigh, // DimensionlessSpectrum
    out DimensionlessSpectrum mie)      // DimensionlessSpectrum
{
    Length r_d = ClampRadius(atmosphere, sqrt(d * d + 2.0f * r * mu * d + r * r));
    Number mu_s_d = ClampCosine((r * mu_s + d * nu) / r_d);

    // transmittance from p->q (along view) times transmittance from q->sun
    DimensionlessSpectrum trans_pq = GetTransmittance(
        atmosphere, transmittance_texture,
        r, mu, d, ray_r_mu_intersects_ground);
    DimensionlessSpectrum trans_qs = GetTransmittanceToSun(
        atmosphere, transmittance_texture,
        r_d, mu_s_d);

    DimensionlessSpectrum transmittance = trans_pq * trans_qs;

    // profile densities at q (altitude = r_d - bottom_radius)
    float altitude = r_d - atmosphere.bottom_radius;
    float rho_ray = GetProfileDensity(atmosphere.rayleigh_density, altitude);
    float rho_mie = GetProfileDensity(atmosphere.mie_density, altitude);

    rayleigh = transmittance * rho_ray;
    mie = transmittance * rho_mie;
}

// --- DistanceToNearestAtmosphereBoundary ----------------------------------
Length DistanceToNearestAtmosphereBoundary(
    AtmosphereParameters atmosphere,
    Length r,
    Number mu,
    bool ray_r_mu_intersects_ground)
{
    if (ray_r_mu_intersects_ground)
        return DistanceToBottomAtmosphereBoundary(atmosphere, r, mu);
    else
        return DistanceToTopAtmosphereBoundary(atmosphere, r, mu);
}

// --- ComputeSingleScattering (integral along ray, trapezoidal) ------------
// Returns irradiance spectra (rayleigh and mie).
// SAMPLE_COUNT = 50 as in original.
void ComputeSingleScattering(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    Length r,
    Number mu,
    Number mu_s,
    Number nu,
    bool ray_r_mu_intersects_ground,
    out float3 rayleigh, // IrradianceSpectrum
    out float3 mie)      // IrradianceSpectrum
{
    // assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    // assert(mu >= -1.0 && mu <= 1.0);
    // assert(mu_s >= -1.0 && mu_s <= 1.0);
    // assert(nu >= -1.0 && nu <= 1.0);
    
    // Validate ranges omitted (no assert in HLSL)
    const int SAMPLE_COUNT = 50;

    Length boundary_dist = DistanceToNearestAtmosphereBoundary(
        atmosphere, r, mu, ray_r_mu_intersects_ground);

    // if boundary_dist is 0 -> return zero
    if (boundary_dist <= 0.0f)
    {
        rayleigh = float3(0.0f, 0.0f, 0.0f);
        mie = float3(0.0f, 0.0f, 0.0f);
        return;
    }

    float dx = boundary_dist / Number(SAMPLE_COUNT);

    DimensionlessSpectrum rayleigh_sum = DimensionlessSpectrum(0.0f, 0.0f, 0.0f);
    DimensionlessSpectrum mie_sum = DimensionlessSpectrum(0.0f, 0.0f, 0.0f);

    for (int i = 0; i <= SAMPLE_COUNT; ++i)
    {
        Length d_i = Number(i) * dx;
        DimensionlessSpectrum ray_i;
        DimensionlessSpectrum mie_i;
        ComputeSingleScatteringIntegrand(atmosphere, transmittance_texture,
                                        r, mu, mu_s, nu, d_i,
                                        ray_r_mu_intersects_ground,
                                        ray_i, mie_i);
        Number weight = (i == 0 || i == SAMPLE_COUNT) ? 0.5f : 1.0f;
        rayleigh_sum += ray_i * weight;
        mie_sum += mie_i * weight;
    }

    // Multiply by dx, solar irradiance and scattering coefficients (per-wavelength)
    rayleigh = rayleigh_sum * dx * atmosphere.solar_irradiance * atmosphere.rayleigh_scattering;
    mie = mie_sum * dx * atmosphere.solar_irradiance * atmosphere.mie_scattering;
}

// --- Phase functions ------------------------------------------------------
// Rayleigh & Mie phase functions (return scalar factor with units InverseSolidAngle)
InverseSolidAngle RayleighPhaseFunction(Number nu)
{
    InverseSolidAngle k = 3.0f / (16.0f * PI * sr);
    return k * (1.0f + nu * nu);
}

InverseSolidAngle MiePhaseFunction(Number g, Number nu)
{
    InverseSolidAngle k = 3.0f / (8.0f * PI * sr) * (1.0f - g * g) / (2.0f + g * g);
    float denom = pow(1.0f + g * g - 2.0f * g * nu, 1.5f);
    return k * (1.0f + nu * nu) / max(denom, 1e-6f);
}

// ----------------------------
// GetScatteringTextureUvwzFromRMuMuSNu
// maps (r, mu, mu_s, nu) -> (u,v,w,z) in [0,1]^4
// ----------------------------
float4 GetScatteringTextureUvwzFromRMuMuSNu(
    AtmosphereParameters atmosphere,
    Length r,
    Number mu,
    Number mu_s,
    Number nu,
    bool ray_r_mu_intersects_ground)
{
    // assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    // assert(mu >= -1.0 && mu <= 1.0);
    // assert(mu_s >= -1.0 && mu_s <= 1.0);
    // assert(nu >= -1.0 && nu <= 1.0);
    
    // H = sqrt(top^2 - bottom^2)
    float H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
                   atmosphere.bottom_radius * atmosphere.bottom_radius);

    // rho (distance to horizon)
    Length rho = SafeSqrt(r * r - atmosphere.bottom_radius * atmosphere.bottom_radius);

    // u_r using unit-range -> tex coord
    Number u_r = GetTextureCoordFromUnitRange(rho / H, SCATTERING_TEXTURE_R_SIZE);

    // discriminant for ray(r,mu) vs ground
    Length r_mu = r * mu;
    Area discriminant = r_mu * r_mu - r * r + atmosphere.bottom_radius * atmosphere.bottom_radius;

    Number u_mu;
    if (ray_r_mu_intersects_ground)
    {
        // distance to ground for ray (r,mu)
        Length d = -r_mu - SafeSqrt(discriminant);
        Length d_min = r - atmosphere.bottom_radius;
        Length d_max = rho;
        float t = (d_max == d_min) ? 0.0f : (d - d_min) / (d_max - d_min);
        u_mu = 0.5f - 0.5f * GetTextureCoordFromUnitRange(t, SCATTERING_TEXTURE_MU_SIZE / 2);
    }
    else
    {
        // distance to top boundary for ray (r,mu)
        Length d = -r_mu + SafeSqrt(discriminant + H * H);
        Length d_min = atmosphere.top_radius - r;
        Length d_max = rho + H;
        u_mu = 0.5f + 0.5f * GetTextureCoordFromUnitRange(
            (d - d_min) / (d_max - d_min), SCATTERING_TEXTURE_MU_SIZE / 2);
    }

    // mu_s mapping
    float d = DistanceToTopAtmosphereBoundary(atmosphere, atmosphere.bottom_radius, mu_s);
    float d_min = atmosphere.top_radius - atmosphere.bottom_radius;
    float d_max = H;
    float a = (d - d_min) / (d_max - d_min);
    float A = -2.0f * atmosphere.mu_s_min * atmosphere.bottom_radius / (d_max - d_min);
    // protect division by zero of A
    float u_mu_s = GetTextureCoordFromUnitRange(
        max(1.0f - a / A, 0.0f) / (1.0f + a), SCATTERING_TEXTURE_MU_S_SIZE);

    Number u_nu = (nu + 1.0f) * 0.5f;

    return float4(u_nu, u_mu_s, u_mu, u_r);
}

// ----------------------------
// Inverse mapping: GetRMuMuSNuFromScatteringTextureUvwz
// uvwz in [0,1]^4 -> r, mu, mu_s, nu and ray_r_mu_intersects_ground
// ----------------------------
void GetRMuMuSNuFromScatteringTextureUvwz(
    AtmosphereParameters atmosphere,
    float4 uvwz,
    out Length r,
    out Number mu,
    out Number mu_s,
    out Number nu,
    out bool ray_r_mu_intersects_ground)
{
    // H
    Length H = sqrt(atmosphere.top_radius * atmosphere.top_radius -
                   atmosphere.bottom_radius * atmosphere.bottom_radius);

    // rho from uvwz.w
    Length rho = H * GetUnitRangeFromTextureCoord(uvwz.w, SCATTERING_TEXTURE_R_SIZE);
    r = sqrt(rho * rho + atmosphere.bottom_radius * atmosphere.bottom_radius);

    // mu from uvwz.z (z<0.5 -> ground case)
    if (uvwz.z < 0.5f)
    {
        Length d_min = r - atmosphere.bottom_radius;
        Length d_max = rho;
        float frac = GetUnitRangeFromTextureCoord(1.0f - 2.0f * uvwz.z, SCATTERING_TEXTURE_MU_SIZE / 2);
        Length d = d_min + (d_max - d_min) * frac;
        if (d == 0.0f * m)
        {
            mu = Number(-1.0f);
        }
        else
        {
            mu = ClampCosine(-(rho * rho + d * d) / (2.0f * r * d));
        }
        ray_r_mu_intersects_ground = true;
    }
    else
    {
        Length d_min = atmosphere.top_radius - r;
        Length d_max = rho + H;
        float frac = GetUnitRangeFromTextureCoord(2.0f * uvwz.z - 1.0f, SCATTERING_TEXTURE_MU_SIZE / 2);
        Length d = d_min + (d_max - d_min) * frac;
        if (d == 0.0f * m)
        {
            mu = Number(1.0f);
        }
        else
        {
            mu = ClampCosine((H * H - rho * rho - d * d) / (2.0f * r * d));
        }
        ray_r_mu_intersects_ground = false;
    }

    // mu_s from uvwz.y (inverse mapping)
    Number x_mu_s = GetUnitRangeFromTextureCoord(uvwz.y, SCATTERING_TEXTURE_MU_S_SIZE);
    Length d_min_ms = atmosphere.top_radius - atmosphere.bottom_radius;
    Length d_max_ms = H;
    Number A = -2.0f * atmosphere.mu_s_min * atmosphere.bottom_radius / (d_max_ms - d_min_ms);
    Number a = (A - x_mu_s * A) / (1.0f + x_mu_s * A);
    Length d_ms = d_min_ms + min(a, A) * (d_max_ms - d_min_ms);
    if (d_ms == 0.0f * m)
    {
        mu_s = Number(1.0f);
    }
    else
    {
        mu_s = ClampCosine((H * H - d_ms * d_ms) / (2.0f * atmosphere.bottom_radius * d_ms));
    }

    // nu from uvwz.x
    nu = ClampCosine(uvwz.x * 2.0f - 1.0f);
}

// ----------------------------
// GetRMuMuSNuFromScatteringTextureFragCoord
// frag_coord: float3 (x packs nu and mu_s tex indices, y=mu index, z=r index)
// ----------------------------
void GetRMuMuSNuFromScatteringTextureFragCoord(
    AtmosphereParameters atmosphere,
    float3 frag_coord,
    out Length r,
    out Number mu,
    out Number mu_s,
    inout Number nu,
    out bool ray_r_mu_intersects_ground)
{
    // SCATTERING_TEXTURE_SIZE vec4(NU-1, MU_S, MU, R)
    const float4 SCATTERING_TEXTURE_SIZE = float4(
        SCATTERING_TEXTURE_NU_SIZE - 1,
        SCATTERING_TEXTURE_MU_S_SIZE,
        SCATTERING_TEXTURE_MU_SIZE,
        SCATTERING_TEXTURE_R_SIZE);
    
    // frag_coord.x encodes (nu_index * MU_S + mu_s_index)
    float frag_coord_nu = floor(frag_coord.x / Number(SCATTERING_TEXTURE_MU_S_SIZE));
    float frag_coord_mu_s = fmod(frag_coord.x, Number(SCATTERING_TEXTURE_MU_S_SIZE));

    float4 uvwz = float4(frag_coord_nu, frag_coord_mu_s, frag_coord.y, frag_coord.z)
                  / SCATTERING_TEXTURE_SIZE;

    GetRMuMuSNuFromScatteringTextureUvwz(
        atmosphere, uvwz, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

    // clamp nu to valid range given mu and mu_s (as in GLSL)
    float tmp = sqrt((1.0f - mu * mu) * (1.0f - mu_s * mu_s));
    float nu_min = mu * mu_s - tmp;
    float nu_max = mu * mu_s + tmp;
    nu = clamp(nu, nu_min, nu_max);
}

// --- ComputeSingleScatteringTexture
// Convenience wrapper converting frag_coord -> r,mu,mu_s,nu and calling ComputeSingleScattering.
// Relies on mapping helpers.
void ComputeSingleScatteringTexture(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    float3 frag_coord,
    out IrradianceSpectrum rayleigh_out,
    out IrradianceSpectrum mie_out)
{
    Length r;
    Number mu;
    Number mu_s;
    Number nu;
    bool ray_r_mu_intersects_ground;

    // This function must be ported as well (mapping frag_coord -> r,mu,mu_s,nu).
    GetRMuMuSNuFromScatteringTextureFragCoord(atmosphere, frag_coord,
        r, mu, mu_s, nu, ray_r_mu_intersects_ground);

    ComputeSingleScattering(atmosphere, transmittance_texture,
        r, mu, mu_s, nu, ray_r_mu_intersects_ground, rayleigh_out, mie_out);
}

// ----------------------------
// GetScattering lookup
// reads two 3D texels to emulate 4D quadrilinear interpolation
// ----------------------------
float3 GetScattering_Generic(
    AtmosphereParameters atmosphere,
    Texture3D<float4> scattering_texture, // e.g. ReducedScatteringTexture or ScatteringTexture typedef
    Length r,
    Number mu,
    Number mu_s,
    Number nu,
    bool ray_r_mu_intersects_ground)
{
    float4 uvwz = GetScatteringTextureUvwzFromRMuMuSNu(
        atmosphere, r, mu, mu_s, nu, ray_r_mu_intersects_ground);

    Number tex_coord_x = uvwz.x * (SCATTERING_TEXTURE_NU_SIZE - 1);
    Number tex_x = floor(tex_coord_x);
    Number lerp = tex_coord_x - tex_x;

    // uvw0 and uvw1 are 3D texture coordinates in [0,1] for two adjacent nu slices
    float3 uvw0 = float3((tex_x + uvwz.y) / (Number) SCATTERING_TEXTURE_NU_SIZE, uvwz.z, uvwz.w);
    float3 uvw1 = float3((tex_x + 1.0f + uvwz.y) / (Number) SCATTERING_TEXTURE_NU_SIZE, uvwz.z, uvwz.w);

    float4 s0 = scattering_texture.SampleLevel(g_samplerLinear, uvw0, 0);
    float4 s1 = scattering_texture.SampleLevel(g_samplerLinear, uvw1, 0);

    float3 result = s0.xyz * (1.0f - lerp) + s1.xyz * lerp;
    return result;
}

RadianceSpectrum GetScattering(
    AtmosphereParameters atmosphere,
    ReducedScatteringTexture single_rayleigh_scattering_texture,
    ReducedScatteringTexture single_mie_scattering_texture,
    ScatteringTexture multiple_scattering_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground,
    int scattering_order)
{
    if (scattering_order == 1)
    {
    IrradianceSpectrum rayleigh = GetScattering_Generic(
        atmosphere, single_rayleigh_scattering_texture, r, mu, mu_s, nu,
        ray_r_mu_intersects_ground);
    IrradianceSpectrum mie = GetScattering_Generic(
        atmosphere, single_mie_scattering_texture, r, mu, mu_s, nu,
        ray_r_mu_intersects_ground);
        return rayleigh * RayleighPhaseFunction(nu) +
        mie * MiePhaseFunction(atmosphere.mie_phase_function_g, nu);
    }
    else
    {
        return GetScattering_Generic(
        atmosphere, multiple_scattering_texture, r, mu, mu_s, nu,
        ray_r_mu_intersects_ground);
    }
}

float2 GetIrradianceTextureUvFromRMuS(AtmosphereParameters atmosphere,
    Length r, Number mu_s)
{
    // assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    // assert(mu_s >= -1.0 && mu_s <= 1.0);
    Number x_r = (r - atmosphere.bottom_radius) /
        (atmosphere.top_radius - atmosphere.bottom_radius);
    Number x_mu_s = mu_s * 0.5 + 0.5;
    return float2(GetTextureCoordFromUnitRange(x_mu_s, IRRADIANCE_TEXTURE_WIDTH),
                GetTextureCoordFromUnitRange(x_r, IRRADIANCE_TEXTURE_HEIGHT));
}

IrradianceSpectrum GetIrradiance(
    AtmosphereParameters atmosphere,
    IrradianceTexture irradiance_texture,
    Length r, Number mu_s)
{
    float2 uv = GetIrradianceTextureUvFromRMuS(atmosphere, r, mu_s);
    return irradiance_texture.SampleLevel(g_samplerLinear, uv, 0).xyz;
}

// -----------------------------------------------------------------------------
// ComputeScatteringDensity
// Integrates incident radiance at point p over directions (theta, phi).
// See functions.glsl section "ComputeScatteringDensity". :contentReference[oaicite:9]{index=9}
// -----------------------------------------------------------------------------
float3 ComputeScatteringDensity(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    Texture3D<float4> single_rayleigh_scattering_texture, // ReducedScatteringTexture
    Texture3D<float4> single_mie_scattering_texture, // ReducedScatteringTexture
    Texture3D<float4> multiple_scattering_texture, // ScatteringTexture
    IrradianceTexture irradiance_texture,
    Length r,
    Number mu,
    Number mu_s,
    Number nu,
    int scattering_order)
{
    // assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    // assert(mu >= -1.0 && mu <= 1.0);
    // assert(mu_s >= -1.0 && mu_s <= 1.0);
    // assert(nu >= -1.0 && nu <= 1.0);
    // assert(scattering_order >= 2);
    
    // Build local directions: zenith, view (omega), sun (omega_s)
    float3 zenith = float3(0.0f, 0.0f, 1.0f);
    float sin_theta_view = sqrt(max(1.0f - mu * mu, 0.0f));
    float3 omega = float3(sin_theta_view, 0.0f, mu);

    float sun_dir_x = (omega.x == 0.0f) ? 0.0f : (nu - mu * mu_s) / omega.x;
    float sun_dir_y = sqrt(max(1.0f - sun_dir_x * sun_dir_x - mu_s * mu_s, 0.0f));
    float3 omega_s = float3(sun_dir_x, sun_dir_y, mu_s);

    const int SAMPLE_COUNT = 16;
    const Angle dphi = PI / SAMPLE_COUNT;
    const Angle dtheta = PI / SAMPLE_COUNT;

    RadianceDensitySpectrum rayleigh_mie = float3(0.0f, 0.0f, 0.0f);

    // Outer loop over polar angle theta (coarse trapezoidal on hemisphere)
    for (int l = 0; l < SAMPLE_COUNT; ++l)
    {
        float theta = (l + 0.5f) * dtheta;
        float cos_theta = cos(theta);
        float sin_theta = sin(theta);
        bool ray_r_theta_intersects_ground = RayIntersectsGround(atmosphere, r, cos_theta);

        // precompute ground-related terms if ray intersects ground
        float distance_to_ground = 0.0f;
        float3 trans_to_ground = float3(0.0f, 0.0f, 0.0f);
        float3 ground_albedo = float3(0.0f, 0.0f, 0.0f);
        if (ray_r_theta_intersects_ground)
        {
            distance_to_ground = DistanceToBottomAtmosphereBoundary(atmosphere, r, cos_theta);
            trans_to_ground = GetTransmittance(atmosphere, transmittance_texture, r, cos_theta, distance_to_ground, true);
            // ground albedo - assume atmosphere.ground_albedo is float3
            ground_albedo = atmosphere.ground_albedo;
        }

        // Inner loop over azimuth angle phi
        for (int k = 0; k < SAMPLE_COUNT; ++k)
        {
            float phi = (k + 0.5f) * dphi;
            // Incident direction omega_i in local frame
            float3 omega_i = float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);

            // Compute dot products
            float nu2 = dot(omega, omega_i);

            // Incident radiance from direction omega_i after (scattering_order - 1) bounces:
            float3 incident_radiance;
            if (scattering_order == 2)
            {
                // For second order, use single scattering textures
                float3 single_rayleigh = GetScattering_Generic(atmosphere, single_rayleigh_scattering_texture, r, cos_theta, mu_s, dot(omega_i, omega_s), ray_r_theta_intersects_ground);
                float3 single_mie = GetScattering_Generic(atmosphere, single_mie_scattering_texture, r, cos_theta, mu_s, dot(omega_i, omega_s), ray_r_theta_intersects_ground);
                incident_radiance = single_rayleigh * RayleighPhaseFunction(dot(omega_i, omega_s))
                                  + single_mie * MiePhaseFunction(atmosphere.mie_phase_function_g, dot(omega_i, omega_s));
            }
            else
            {
                // For n>2 use multiple_scattering_texture (contains previous order)
                incident_radiance = GetScattering_Generic(atmosphere, multiple_scattering_texture, r, cos_theta, mu_s, dot(omega_i, omega_s), ray_r_theta_intersects_ground);
            }

            // If the ray intersects ground, include ground reflection (using irradiance texture)
            float3 reflected = float3(0.0f, 0.0f, 0.0f);
            if (ray_r_theta_intersects_ground)
            {
                // Compute ground irradiance (precomputed irradiance texture) for horizontal ground at bottom
                // Map (r, cos_theta) -> uv (use GetIrradianceTextureUvFromRMuS in GLSL)
                // We'll assume a helper exists: GetIrradianceTextureUvFromRMuS
                float2 uv = GetIrradianceTextureUvFromRMuS(atmosphere, r, cos_theta);
                float3 ground_irradiance = irradiance_texture.SampleLevel(g_samplerLinear, uv, 0).xyz;
                reflected = ground_irradiance * ground_albedo;
            }

            // Phase functions and densities at p
            float rayleigh_density = GetProfileDensity(atmosphere.rayleigh_density, r - atmosphere.bottom_radius);
            float mie_density = GetProfileDensity(atmosphere.mie_density, r - atmosphere.bottom_radius);

            // incident_radiance already represents radiance arriving at q after previous orders
            // compute contribution to scattering density: incident * (sigma * phase) * domega_i
            // domega_i = sin_theta * dtheta * dphi
            float domega_i = sin_theta * dtheta * dphi;
            rayleigh_mie += (incident_radiance *
                (atmosphere.rayleigh_scattering * rayleigh_density * RayleighPhaseFunction(nu2) +
                 atmosphere.mie_scattering * mie_density * MiePhaseFunction(atmosphere.mie_phase_function_g, nu2))
                + reflected) * domega_i;
        }
    }

    return rayleigh_mie;
}

RadianceSpectrum ComputeMultipleScattering(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    ScatteringDensityTexture scattering_density_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground)
{
    //assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    //assert(mu >= -1.0 && mu <= 1.0);
    //assert(mu_s >= -1.0 && mu_s <= 1.0);
    //assert(nu >= -1.0 && nu <= 1.0);

    // Number of intervals for the numerical integration.
    const int SAMPLE_COUNT = 50;
    // The integration step, i.e. the length of each integration interval.
    Length dx = DistanceToNearestAtmosphereBoundary(atmosphere, r, mu, ray_r_mu_intersects_ground) /
                Number(SAMPLE_COUNT);
    // Integration loop.
    RadianceSpectrum rayleigh_mie_sum =
        RadianceSpectrum(0.0 * watt_per_square_meter_per_sr_per_nm, 0.0 * watt_per_square_meter_per_sr_per_nm, 0.0 * watt_per_square_meter_per_sr_per_nm);
    for (int i = 0; i <= SAMPLE_COUNT; ++i)
    {
        Length d_i = Number(i) * dx;

        // The r, mu and mu_s parameters at the current integration point (see the
        // single scattering section for a detailed explanation).
        Length r_i =
            ClampRadius(atmosphere, sqrt(d_i * d_i + 2.0 * r * mu * d_i + r * r));
        Number mu_i = ClampCosine((r * mu + d_i) / r_i);
        Number mu_s_i = ClampCosine((r * mu_s + d_i * nu) / r_i);

        // The Rayleigh and Mie multiple scattering at the current sample point.
        RadianceSpectrum rayleigh_mie_i =
            GetScattering_Generic(
                atmosphere, scattering_density_texture, r_i, mu_i, mu_s_i, nu,
                ray_r_mu_intersects_ground) *
            GetTransmittance(
                atmosphere, transmittance_texture, r, mu, d_i,
                ray_r_mu_intersects_ground) *
            dx;
        // Sample weight (from the trapezoidal rule).
        Number weight_i = (i == 0 || i == SAMPLE_COUNT) ? 0.5 : 1.0;
        rayleigh_mie_sum += rayleigh_mie_i * weight_i;
    }
    return rayleigh_mie_sum;
}

RadianceDensitySpectrum ComputeScatteringDensityTexture(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    ReducedScatteringTexture single_rayleigh_scattering_texture,
    ReducedScatteringTexture single_mie_scattering_texture,
    ScatteringTexture multiple_scattering_texture,
    IrradianceTexture irradiance_texture,
    float3 frag_coord,
    int scattering_order)
{
    Length r;
    Number mu;
    Number mu_s;
    Number nu;
    bool ray_r_mu_intersects_ground;
    GetRMuMuSNuFromScatteringTextureFragCoord(atmosphere, frag_coord,
        r, mu, mu_s, nu, ray_r_mu_intersects_ground);
    return ComputeScatteringDensity(atmosphere, transmittance_texture,
        single_rayleigh_scattering_texture, single_mie_scattering_texture,
        multiple_scattering_texture, irradiance_texture, r, mu, mu_s, nu,
        scattering_order);
}

RadianceSpectrum ComputeMultipleScatteringTexture(
     AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    ScatteringDensityTexture scattering_density_texture,
    float3 frag_coord, inout Number nu)
{
    Length r;
    Number mu;
    Number mu_s;
    bool ray_r_mu_intersects_ground;
    GetRMuMuSNuFromScatteringTextureFragCoord(atmosphere, frag_coord,
        r, mu, mu_s, nu, ray_r_mu_intersects_ground);
    return ComputeMultipleScattering(atmosphere, transmittance_texture,
        scattering_density_texture, r, mu, mu_s, nu,
        ray_r_mu_intersects_ground);
}


IrradianceSpectrum ComputeDirectIrradiance(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    Length r, Number mu_s)
{
    // assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    // assert(mu_s >= -1.0 && mu_s <= 1.0);

    Number alpha_s = atmosphere.sun_angular_radius / rad;
    // Approximate average of the cosine factor mu_s over the visible fraction of
    // the Sun disc.
    Number average_cosine_factor =
    mu_s < -alpha_s ? 0.0 : (mu_s > alpha_s ? mu_s :
        (mu_s + alpha_s) * (mu_s + alpha_s) / (4.0 * alpha_s));

    return atmosphere.solar_irradiance *
        GetTransmittanceToTopAtmosphereBoundary(
            atmosphere, transmittance_texture, r, mu_s) *
    average_cosine_factor;
}

IrradianceSpectrum ComputeIndirectIrradiance(
    AtmosphereParameters atmosphere,
    ReducedScatteringTexture single_rayleigh_scattering_texture,
    ReducedScatteringTexture single_mie_scattering_texture,
    ScatteringTexture multiple_scattering_texture,
    Length r, Number mu_s, int scattering_order)
{
    // assert(r >= atmosphere.bottom_radius && r <= atmosphere.top_radius);
    // assert(mu_s >= -1.0 && mu_s <= 1.0);
    // assert(scattering_order >= 1);

    const int SAMPLE_COUNT = 32;
    const Angle dphi = pi / Number(SAMPLE_COUNT);
    const Angle dtheta = pi / Number(SAMPLE_COUNT);

    IrradianceSpectrum result =
        IrradianceSpectrum(0.0 * watt_per_square_meter_per_nm, 0.0 * watt_per_square_meter_per_nm, 0.0 * watt_per_square_meter_per_nm);
    float3 omega_s = float3(sqrt(1.0 - mu_s * mu_s), 0.0, mu_s);
    for (
    int j = 0; j < SAMPLE_COUNT / 2; ++j)
    {
    Angle theta = (Number(j) + 0.5) * dtheta;
        for (
    int i = 0; i < 2 *
    SAMPLE_COUNT; ++i)
        {
    Angle phi = (Number(i) + 0.5) * dphi;
            float3 omega =
            float3(cos(phi) * sin(theta), sin(phi) * sin(theta), cos(theta));
        SolidAngle domega = (dtheta / rad) * (dphi / rad) * sin(theta) * sr;

        Number nu = dot(omega, omega_s);
            result += GetScattering(atmosphere, single_rayleigh_scattering_texture,
            single_mie_scattering_texture, multiple_scattering_texture,
            r, omega.z, mu_s, nu, false /* ray_r_theta_intersects_ground */,
            scattering_order) *
                omega.z *
    domega;
        }
    }
    return result;
}

/*
<p>The inverse mapping follows immediately:
*/

void GetRMuSFromIrradianceTextureUv(AtmosphereParameters atmosphere,
    float2 uv, out Length r, out Number mu_s)
{
    // assert(uv.x >= 0.0 && uv.x <= 1.0);
    // assert(uv.y >= 0.0 && uv.y <= 1.0);
    Number x_mu_s = GetUnitRangeFromTextureCoord(uv.x, IRRADIANCE_TEXTURE_WIDTH);
    Number x_r = GetUnitRangeFromTextureCoord(uv.y, IRRADIANCE_TEXTURE_HEIGHT);
    r = atmosphere.bottom_radius +
        x_r * (atmosphere.top_radius - atmosphere.bottom_radius);
    mu_s = ClampCosine(2.0 * x_mu_s - 1.0);
}

#define IRRADIANCE_TEXTURE_SIZE float2(IRRADIANCE_TEXTURE_WIDTH, IRRADIANCE_TEXTURE_HEIGHT)


IrradianceSpectrum ComputeDirectIrradianceTexture(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    float2 frag_coord)
{
    Length r;
    Number mu_s;
    GetRMuSFromIrradianceTextureUv(
        atmosphere, frag_coord / IRRADIANCE_TEXTURE_SIZE, r, mu_s);
    return ComputeDirectIrradiance(atmosphere, transmittance_texture, r, mu_s);
}

IrradianceSpectrum ComputeIndirectIrradianceTexture(
    AtmosphereParameters atmosphere,
    ReducedScatteringTexture single_rayleigh_scattering_texture,
    ReducedScatteringTexture single_mie_scattering_texture,
    ScatteringTexture multiple_scattering_texture,
    float2 frag_coord,
    int scattering_order)
{
    Length r;
    Number mu_s;
    GetRMuSFromIrradianceTextureUv(
        atmosphere, frag_coord / IRRADIANCE_TEXTURE_SIZE, r, mu_s);
    return ComputeIndirectIrradiance(atmosphere,
        single_rayleigh_scattering_texture, single_mie_scattering_texture,
        multiple_scattering_texture, r, mu_s, scattering_order);
}


#ifdef COMBINED_SCATTERING_TEXTURES
float3 GetExtrapolatedSingleMieScattering(
    AtmosphereParameters atmosphere, float4 scattering) {
    if (scattering.r == 0.0) {
    return float3(0.0, 0.0, 0.0);
    }
    return scattering.rgb * scattering.a / scattering.r *
	    (atmosphere.rayleigh_scattering.r / atmosphere.mie_scattering.r) *
	    (atmosphere.mie_scattering / atmosphere.rayleigh_scattering);
}
#endif



IrradianceSpectrum GetCombinedScattering(
    AtmosphereParameters atmosphere,
    ReducedScatteringTexture scattering_texture,
    ReducedScatteringTexture single_mie_scattering_texture,
    Length r, Number mu, Number mu_s, Number nu,
    bool ray_r_mu_intersects_ground,
    out IrradianceSpectrum single_mie_scattering)
{
    float4 uvwz = GetScatteringTextureUvwzFromRMuMuSNu(
        atmosphere, r, mu, mu_s, nu, ray_r_mu_intersects_ground);
    Number tex_coord_x = uvwz.x * Number(SCATTERING_TEXTURE_NU_SIZE - 1);
    Number tex_x = floor(tex_coord_x);
    Number lerp = tex_coord_x - tex_x;
    float3 uvw0 = float3((tex_x + uvwz.y) / Number(SCATTERING_TEXTURE_NU_SIZE),
        uvwz.z, uvwz.w);
    float3 uvw1 = float3((tex_x + 1.0 + uvwz.y) / Number(SCATTERING_TEXTURE_NU_SIZE),
        uvwz.z, uvwz.w);
#ifdef COMBINED_SCATTERING_TEXTURES
    // combined stored in scattering_texture (rgba) — emulate GLSL branch
    float4 combined0 = scattering_texture.SampleLevel(g_samplerLinear, uvw0, 0);
    float4 combined1 = scattering_texture.SampleLevel(g_samplerLinear, uvw1, 0);
    float4 combined = combined0 * (1.0f - lerp) + combined1 * lerp;
    float3 scattering = combined.rgb;
    if (combined.r == 0.0f)
    {
        single_mie_scattering = float3(0.0f, 0.0f, 0.0f);
    }
    else
    {
        single_mie_scattering =
            GetExtrapolatedSingleMieScattering(atmosphere, combined);
    }
#else
    // separate textures for scattering and single_mie_scattering
    float3 s0 = scattering_texture.SampleLevel(g_samplerLinear, uvw0, 0).xyz;
    float3 s1 = scattering_texture.SampleLevel(g_samplerLinear, uvw1, 0).xyz;
    IrradianceSpectrum scattering = s0 * (1.0f - lerp) + s1 * lerp;
    
    float3 m0 = single_mie_scattering_texture.SampleLevel(g_samplerLinear, uvw0, 0).xyz;
    float3 m1 = single_mie_scattering_texture.SampleLevel(g_samplerLinear, uvw1, 0).xyz;
    single_mie_scattering = m0 * (1.0f - lerp) + m1 * lerp;
#endif
    return scattering;
}

RadianceSpectrum GetSkyRadiance(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    ReducedScatteringTexture scattering_texture,
    ReducedScatteringTexture single_mie_scattering_texture,
    Position camera, Direction view_ray, Length shadow_length,
    Direction sun_direction, out DimensionlessSpectrum transmittance)
{
    // Compute the distance to the top atmosphere boundary along the view ray,
    // assuming the viewer is in space (or NaN if the view ray does not intersect
    // the atmosphere).
    Length r = length(camera);
    Length rmu = dot(camera, view_ray);
    Length distance_to_top_atmosphere_boundary = -rmu -
        sqrt(rmu * rmu - r * r + atmosphere.top_radius * atmosphere.top_radius);
    // If the viewer is in space and the view ray intersects the atmosphere, move
    // the viewer to the top atmosphere boundary (along the view ray):
    if (distance_to_top_atmosphere_boundary > 0.0 * m)
    {
        camera = camera + view_ray * distance_to_top_atmosphere_boundary;
        r = atmosphere.top_radius;
        rmu += distance_to_top_atmosphere_boundary;
    }
    else if (r > atmosphere.top_radius)
    {
        // If the view ray does not intersect the atmosphere, simply return 0.
        transmittance = DimensionlessSpectrum(1.0, 1.0, 1.0);
        return RadianceSpectrum(0.0 * watt_per_square_meter_per_sr_per_nm, 0.0 * watt_per_square_meter_per_sr_per_nm, 0.0 * watt_per_square_meter_per_sr_per_nm);
    }
    // Compute the r, mu, mu_s and nu parameters needed for the texture lookups.
    Number mu = rmu / r;
    Number mu_s = dot(camera, sun_direction) / r;
    Number nu = dot(view_ray, sun_direction);
    bool ray_r_mu_intersects_ground = RayIntersectsGround(atmosphere, r, mu);

    transmittance = ray_r_mu_intersects_ground ? DimensionlessSpectrum(0.0, 0.0, 0.0) :
        GetTransmittanceToTopAtmosphereBoundary(
            atmosphere, transmittance_texture, r, mu);
    IrradianceSpectrum single_mie_scattering;
    IrradianceSpectrum scattering;
    
    if (shadow_length == 0.0 * m)
    {
        scattering = GetCombinedScattering(
            atmosphere, scattering_texture, single_mie_scattering_texture,
            r, mu, mu_s, nu, ray_r_mu_intersects_ground,
            single_mie_scattering);
    }
    else
    {
        // Case of light shafts (shadow_length is the total length noted l in our
        // paper): we omit the scattering between the camera and the point at
        // distance l, by implementing Eq. (18) of the paper (shadow_transmittance
        // is the T(x,x_s) term, scattering is the S|x_s=x+lv term).
        Length d = shadow_length;
        Length r_p =
            ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
        Number mu_p = (r * mu + d) / r_p;
        Number mu_s_p = (r * mu_s + d * nu) / r_p;

        scattering = GetCombinedScattering(
            atmosphere, scattering_texture, single_mie_scattering_texture,
            r_p, mu_p, mu_s_p, nu, ray_r_mu_intersects_ground,
            single_mie_scattering);
        DimensionlessSpectrum shadow_transmittance =
            GetTransmittance(atmosphere, transmittance_texture,
                r, mu, shadow_length, ray_r_mu_intersects_ground);
        scattering = scattering * shadow_transmittance;
        single_mie_scattering = single_mie_scattering * shadow_transmittance;
    }
    return scattering * RayleighPhaseFunction(nu) + single_mie_scattering *
        MiePhaseFunction(atmosphere.mie_phase_function_g, nu);
}

RadianceSpectrum GetSkyRadianceToPoint(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    ReducedScatteringTexture scattering_texture,
    ReducedScatteringTexture single_mie_scattering_texture,
    Position camera, Position thePoint, Length shadow_length,
    Direction sun_direction, out DimensionlessSpectrum transmittance)
{
    // Compute the distance to the top atmosphere boundary along the view ray,
    // assuming the viewer is in space (or NaN if the view ray does not intersect
    // the atmosphere).
    Direction view_ray = normalize(thePoint - camera);
    Length r = length(camera);
    Length rmu = dot(camera, view_ray);
    Length distance_to_top_atmosphere_boundary = -rmu -
        sqrt(rmu * rmu - r * r + atmosphere.top_radius * atmosphere.top_radius);
    // If the viewer is in space and the view ray intersects the atmosphere, move
    // the viewer to the top atmosphere boundary (along the view ray):
    if (distance_to_top_atmosphere_boundary > 0.0 * m)
    {
        camera = camera + view_ray *
    distance_to_top_atmosphere_boundary;
        r = atmosphere.top_radius;
        rmu +=
    distance_to_top_atmosphere_boundary;
    }

    // Compute the r, mu, mu_s and nu parameters for the first texture lookup.
    Number mu = rmu / r;
    Number mu_s = dot(camera, sun_direction) / r;
    Number nu = dot(view_ray, sun_direction);
    Length d = length(thePoint - camera);
    bool ray_r_mu_intersects_ground = RayIntersectsGround(atmosphere, r, mu);

    transmittance = GetTransmittance(atmosphere, transmittance_texture,
        r, mu, d, ray_r_mu_intersects_ground);

    IrradianceSpectrum single_mie_scattering;
    IrradianceSpectrum scattering = GetCombinedScattering(
        atmosphere, scattering_texture, single_mie_scattering_texture,
        r, mu, mu_s, nu, ray_r_mu_intersects_ground,
        single_mie_scattering);

    // Compute the r, mu, mu_s and nu parameters for the second texture lookup.
    // If shadow_length is not 0 (case of light shafts), we want to ignore the
    // scattering along the last shadow_length meters of the view ray, which we
    // do by subtracting shadow_length from d (this way scattering_p is equal to
    // the S|x_s=x_0-lv term in Eq. (17) of our paper).
    d = max(d - shadow_length, 0.0 * m);
    Length r_p = ClampRadius(atmosphere, sqrt(d * d + 2.0 * r * mu * d + r * r));
    Number mu_p = (r * mu + d) / r_p;
    Number mu_s_p = (r * mu_s + d * nu) / r_p;

    IrradianceSpectrum single_mie_scattering_p;
    IrradianceSpectrum scattering_p = GetCombinedScattering(
        atmosphere, scattering_texture, single_mie_scattering_texture,
        r_p, mu_p, mu_s_p, nu, ray_r_mu_intersects_ground,
        single_mie_scattering_p);

    // Combine the lookup results to get the scattering between camera and point.
    DimensionlessSpectrum shadow_transmittance = transmittance;
    if (shadow_length > 0.0 * m)
    {
    // This is the T(x,x_s) term in Eq. (17) of our paper, for light shafts.
        shadow_transmittance = GetTransmittance(atmosphere, transmittance_texture,
        r, mu, d, ray_r_mu_intersects_ground);
    }
    scattering = scattering - shadow_transmittance *
    scattering_p;
    single_mie_scattering =
        single_mie_scattering - shadow_transmittance * single_mie_scattering_p;
    #ifdef COMBINED_SCATTERING_TEXTURES
    single_mie_scattering = GetExtrapolatedSingleMieScattering(
        atmosphere, float4(scattering, single_mie_scattering.r));
    #endif

    // Hack to avoid rendering artifacts when the sun is below the horizon.
    single_mie_scattering = single_mie_scattering *
        smoothstep(Number(0.0), Number(0.01), mu_s);

    return scattering * RayleighPhaseFunction(nu) + single_mie_scattering *
        MiePhaseFunction(atmosphere.mie_phase_function_g, nu);
}

IrradianceSpectrum GetSunAndSkyIrradiance(
    AtmosphereParameters atmosphere,
    TransmittanceTexture transmittance_texture,
    IrradianceTexture irradiance_texture,
    Position thePoint, Direction normal, Direction sun_direction,
    out IrradianceSpectrum sky_irradiance)
{
    Length r = length(thePoint);
    Number mu_s = dot(thePoint, sun_direction) / r;

    // Indirect irradiance (approximated if the surface is not horizontal).
    sky_irradiance = GetIrradiance(atmosphere, irradiance_texture, r, mu_s) *
        (1.0 + dot(normal, thePoint) / r) * 0.5;

    // Direct irradiance.
    return atmosphere.solar_irradiance *
        GetTransmittanceToSun(
            atmosphere, transmittance_texture, r, mu_s) *
        max(dot(normal, sun_direction), 0.0);
}

#endif // BRUNETON_FUNCTIONS_HLSL