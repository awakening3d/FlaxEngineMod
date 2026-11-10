// Copyright (c) Wojciech Figat. All rights reserved.

#ifndef __GAMMA_CORRECTION_COMMON__
#define __GAMMA_CORRECTION_COMMON__

#include "./Flax/Math.hlsl"

// Shared configs for Color Grading LUT
#define COLOR_GRADING_LUT_LOG 1
#define COLOR_GRADING_LUT_SCALE 1.04f


// HDR reference white in nits (SDR white is typically mapped to 100~203 nits in HDR)
#ifndef HDR_REFERENCE_WHITE
#define HDR_REFERENCE_WHITE 203.0f
#endif


// Fast reversible tonemapper
// http://gpuopen.com/optimized-reversible-tonemapper-for-resolve/
float3 FastTonemap(float3 c)
{
    return c * rcp(max(max(c.r, c.g), c.b) + 1.0);
}

float4 FastTonemap(float4 c)
{
    return float4(FastTonemap(c.rgb), c.a);
}

float3 FastTonemap(float3 c, float w)
{
    return c * (w * rcp(max(max(c.r, c.g), c.b) + 1.0));
}

float4 FastTonemap(float4 c, float w)
{
    return float4(FastTonemap(c.rgb, w), c.a);
}

float3 FastTonemapInvert(float3 c)
{
    return c * rcp(1.0 - max(max(c.r, c.g), c.b));
}

float4 FastTonemapInvert(float4 c)
{
    return float4(FastTonemapInvert(c.rgb), c.a);
}

float LinearToSrgb(float linearColor)
{
    if (linearColor < 0.00313067)
        return linearColor * 12.92;
    return pow(linearColor, (1.0 / 2.4)) * 1.055 - 0.055;
}

float3 LinearToSrgb(float3 linearColor)
{
    return float3(LinearToSrgb(linearColor.r), LinearToSrgb(linearColor.g), LinearToSrgb(linearColor.b));
}

float3 sRGBToLinear(float3 color)
{
    color = max(6.10352e-5, color);
    return select(color > 0.04045, pow(color * (1.0 / 1.055) + 0.0521327, 2.4), color * (1.0 / 12.92));
}

float3 LogToLinear(float3 logColor)
{
    const float linearRange = 14.0f;
    const float linearGrey = 0.18f;
    const float exposureGrey = 444.0f;
    return exp2((logColor - exposureGrey / 1023.0) * linearRange) * linearGrey;
}

float3 LinearToLog(float3 linearColor)
{
    const float linearRange = 14.0f;
    const float linearGrey = 0.18f;
    const float exposureGrey = 444.0f;
    return saturate(log2(linearColor) / linearRange - log2(linearGrey) / linearRange + exposureGrey / 1023.0f);
}



// Linear [0,1] -> PQ (ST2084) encoded [0,1]
// Input: linear color where 1.0 = HDR_REFERENCE_WHITE nits (scaled before call)
// Use: LinearToPQ(linearColor * (HDR_REFERENCE_WHITE / 10000.0))
float LinearToPQ(float linearColor)
{
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    float y = pow(max(linearColor, 0.0), m1);
    return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

float3 LinearToPQ(float3 linearColor)
{
    return float3(LinearToPQ(linearColor.r), LinearToPQ(linearColor.g), LinearToPQ(linearColor.b));
}



//-----------------------------------------------------------------------------------
// Applies a smooth, branchless highlight compression curve.
// 
// Behavior:
//   - For x <= knee (0.5): the function returns x unchanged.
//   - For x >  knee (0.5): the excess (x - knee) is compressed by a rational
//     function so that the output approaches but never exceeds limit (0.9).
//
// The curve is C1-continuous at the knee, strictly monotonic, and asymptotically
// bounded by `limit`, which prevents hard clipping while preserving contrast
// in the shadows and midtones.
//
// Mathematical equivalence:
//   Let k = knee, L = limit, R = L - k.
//   If x <= k:  result = x
//   If x >  k:  result = k + (x - k) / (1 + (x - k) / R)
//
//   This is a Reinhard-style compression applied only to the portion above k,
//   mapped onto the range [k, L).
//
// Branchless formulation:
//   over        = max(x - k, 0)              // excess above the knee, or 0
//   compressed  = over / (1 + over / R)      // smooth compression of the excess
//   result      = min(x, k) + compressed     // passthrough below k, plus compressed excess
//
// All operations use min/max and arithmetic to avoid divergent branches,
// allowing the GPU to process all three channels in parallel via SIMD.

float3 SoftClip(float3 x)
{
    const float knee = 0.5;                 // Threshold above which compression begins
    const float limit = 0.9;                // Asymptotic maximum output value
    const float range = limit - knee;       // Available headroom above the knee
    float3 over = max(x - knee, 0.0);       // Excess above knee, clamped to zero
    return min(x, knee) + over / (1.0 + over / range);
}

//------------------------------------------------------------------------------------------


//------------------------------------------------------------------------------------------
// Rapid-convergence soft clip: values below the knee pass through unchanged,
// while values above the knee compress exponentially toward the limit.
//
// Behavior:
//   - For x <= knee (0.5): the function returns x unchanged.
//   - For x >  knee (0.5): the excess (x - knee) is compressed by an exponential
//     curve, converging much faster toward `limit` (0.9) than the rational
//     variant `SoftClip`. This produces a more aggressive highlight roll-off,
//     flattening bright regions sooner while still avoiding hard clipping.
//
// Mathematical equivalence:
//   Let k = knee, L = limit, R = L - k.
//   If x <= k:  result = x
//   If x >  k:  result = k + R * (1 - exp(-2 * (x - k) / R))
//
//   The exponent factor 2.0 controls roll-off speed: larger values converge
//   faster, smaller values approach the rational SoftClip curve.
//
// Branchless formulation:
//   over        = max(x - k, 0)                              // excess above knee
//   t           = over / R                                   // normalized excess
//   compressed  = R * (1 - exp2(-t * 2.88539008178))         // exp(-2t) via exp2
//   result      = min(x, k) + compressed                     // passthrough + compressed excess
//
// Notes on optimization:
//   - exp(-t * 2.0) is rewritten as exp2(-t * 2.88539008178) where
//     2.88539008178 = 2.0 / log2(e). exp2 is a native GPU instruction and
//     supports float3 directly, avoiding per-channel scalar calls.
//   - All operations use min/max and arithmetic to avoid divergent branches,
//     allowing full SIMD processing across RGB channels.
//   - Unlike `SoftClip` (rational, C1-continuous at knee), this function has
//     a slope discontinuity at the knee, trading smoothness for a faster
//     convergence to the limit. Use it when highlights look too soft.

float3 SoftClipRapid(float3 x)
{
    const float knee = 0.5;                              // Compression starts above this value
    const float limit = 0.9;                             // Maximum output value
    const float range = limit - knee;                    // Available headroom above the knee
    float3 over = max(x - knee, 0.0);                    // Excess above the knee, clamped to zero
    float3 t = over / range;                             // Normalized excess in [0, inf)
    float3 compressed = range * (1.0 - exp2(-t * 2.88539008178)); // exp(-2t) via exp2
    return min(x, knee) + compressed;                    // Passthrough below knee + compressed excess
}
//----------------------------------------------------------------------------------------------------


// Simulates the visual appearance of an HDR display on a standard SDR screen.
// 1. Assumes input is a PQ-encoded signal (0~1) written to an HDR swapchain.
// 2. Decodes PQ to linear physical luminance (nits).
// 3. Simulates a screenshot tool mapping the HDR luminance back to SDR (sRGB).

float3 FakeHDR(float3 pqSignal, float maxDisplayNits = 300.0)
{

    // =========================================================================
    // Step 1: Pre-boost dark areas in PQ signal space
    // =========================================================================

    // pqKnee: The luminance threshold (in PQ signal space, 0~1) where the shadow boost begins to fade out.
    // Pixels with luminance below this value receive the full pqGain boost to lift dark details.
    // Pixels above this value smoothly transition back to a 1.0x gain (no boost), protecting highlights.
    // Lower values (e.g., 0.2) restrict the boost to very dark areas only.
    // Higher values (e.g., 0.6) apply the boost across a wider range of midtones and shadows.
    const float pqKnee = 0.55f;

    // pqGain: The maximum multiplicative factor applied to the PQ signal for the darkest pixels.
    // Because the PQ decoding curve is extremely steep in the low-luminance region, even a small gain
    // (e.g., 1.2) can dramatically amplify the decoded physical luminance of shadows.
    // Higher values (e.g., 1.9) aggressively pull dark details out of the PQ "black hole",
    // at the cost of potentially making the image look washed out or losing pure-black depth.
    const float pqGain = 1.9f;
	
    // Rec.2020 luma coefficients. Used to compute the perceived brightness of the pixel.
    // The PQ signal itself is not linear, so we use the luma to estimate the pixel's intensity.
    float pqLuma = dot(pqSignal, float3(0.2627, 0.6780, 0.0593));
    
    // Smoothly blend the boost factor based on the luma.
    // Below pqKnee: use the maximum gain (pqGain) to aggressively lift shadows.
    // Above pqKnee: gradually fall back to 1.0 (no boost) to protect midtones and highlights.
    float t = smoothstep(0.0, pqKnee, pqLuma);
    float boostFactor = lerp(pqGain, 1.0f, t);
    
    // Apply the boost to the PQ signal before decoding.
    // This is critical: boosting AFTER PQ decode is too late, because dark areas are already crushed to near-zero.
    // Boosting the PQ signal BEFORE decode takes advantage of the PQ curve's extreme steepness in the shadows,
    // allowing even small gains to dramatically amplify the final decoded physical luminance.
    float3 pqBoosted = pqSignal * boostFactor;


    // =========================================================================
    // Step 2: PQ Decode (SMPTE ST 2084)
    // =========================================================================
    // Decode the pre-boosted PQ signal into linear physical luminance (L).
    // The result L is a linear ratio where 1.0 represents 10000 nits.
    // Note: m1, m2, c1, c2, c3 are constants defined by the ST 2084 specification.
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
	
    float3 Npow = pow(pqBoosted, 1.0 / m2);
    float3 num = max(Npow - c1, 0.0);
    float3 den = c2 - c3 * Npow;
    float3 L = pow(num / den, 1.0 / m1); 
    
	
	// =========================================================================
    // Step 3: Simulate Screenshot Tool (Hard Clip Mapping)
	// =========================================================================
    // Physical luminance in nits (L * 10000).
    float3 nits = L * 10000.0;
	float3 mapped = nits / maxDisplayNits;
    // Hard clip to [0, 1] to simulate the R8G8B8 buffer limit.
    return saturate(mapped);
}


#endif
