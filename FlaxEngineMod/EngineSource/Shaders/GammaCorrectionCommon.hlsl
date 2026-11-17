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

// Decode PQ signal into linear physical luminance (L)
float3 PQtoNits(float3 pqSignal)
{
	const float m1 = 0.1593017578125;
	const float m2 = 78.84375;
	const float c1 = 0.8359375;
	const float c2 = 18.8515625;
	const float c3 = 18.6875;

	float3 Npow = pow(pqSignal, 1.0 / m2);
	float3 num = max(Npow - c1, 0.0);
	float3 den = c2 - c3 * Npow;
	float3 L_norm = pow(num / den, 1.0 / m1);

	float3 nits = L_norm * 10000.0;
	return nits;
}


	/*
    // Dave Hoskins hash (extremely stable on all GPUs)
    float3 p3 = frac(float3(gridPos.x, gridPos.y, gridPos.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    float noise = frac((p3.x + p3.y) * p3.z);	
	*/

float DistNoise(float dist, float3 dir)
{
    // Clamp distance to avoid precision loss at extreme distances
    //dist = min(dist, 400000);
    
    // Mix direction into the distance to break the static pattern in the sky
    // The direction components vary smoothly across the screen, providing spatial variation
    float mixed = dist + sin( dot(dir, float3(0.01135, 0.02719, 0.01246)));
    float noise = frac(sin(mixed) * 43758.5453);
    return (noise - 0.5);
}



// Applies a triangular-distribution dither to break up color banding.
// The noise is scaled to roughly one 8-bit quantization step (1/255).
float3 ApplyDither(float3 color, float2 screenUV)
{
    // Cheap pseudo-random based on screen coordinates
    float noise1 = frac(sin(dot(screenUV, float2(12.9898, 78.233))) * 43758.5453);
    float noise2 = frac(sin(dot(screenUV * 1.3, float2(39.346, 11.135))) * 24634.6345);
    
    // Triangular distribution in [-1, 1]
    float dither = (noise1 + noise2) - 1.0;
    
    // Scale to ~1 quantization step and apply
    return color + dither * (1.0 / 255.0);
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

float3 FakeHDR1(float3 pqSignal, float maxDisplayNits = 300.0)
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
    float3 nits = PQtoNits(pqSignal);
	
	// =========================================================================
    // Step 3: Simulate Screenshot Tool (Hard Clip Mapping)
	// =========================================================================
    // Physical luminance in nits (L * 10000).
	float3 mapped = nits / maxDisplayNits;
    // Hard clip to [0, 1] to simulate the R8G8B8 buffer limit.
    return saturate(mapped);
}

float3 FakeHDR2(float3 pqSignal, float maxDisplayNits = 300.0)
{
	// =========================================================================
    // Simulate Screenshot Tool (Hard Clip Mapping)
	// =========================================================================
    // Physical luminance in nits (L * 10000).
    float3 nits = PQtoNits(pqSignal);
	float3 mapped = nits / maxDisplayNits;
	
    const float knee = 0.8;
    const float range = 1.0 - knee;
    
    float3 over = max(mapped - knee, 0.0);
    // This is the exact same branchless formula used in SoftClip
    mapped = min(mapped, knee) + (over / (1.0 + over / range));
    
    return saturate(mapped);
}



float3 FakeHDR3(float3 pqSignal, float maxDisplayNits = 420.0)
{
	float3 nits = PQtoNits(pqSignal);

    float Y_nits = dot(nits, float3(0.2627, 0.6780, 0.0593));
    float mappedY = Y_nits / maxDisplayNits;

    // Two-stage soft shoulder: preserve mid-bright snow micro-detail
    const float kneeMid = 0.72;
    const float kneePeak = 0.86;
    float overMid = max(mappedY - kneeMid, 0.0);
    mappedY = min(mappedY, kneeMid) + overMid / (1.0 + overMid / (1.0 - kneeMid));

    float overPeak = max(mappedY - kneePeak, 0.0);
    mappedY = min(mappedY, kneePeak) + overPeak / (1.0 + overPeak * 2.2);
    mappedY = saturate(mappedY);

    // Luminance-only scaling, isolate chroma
    float safeY = max(Y_nits, 1e-6);
    float chromaR = nits.r / safeY;
    float chromaG = nits.g / safeY;
    float chromaB = nits.b / safeY;

    float chromaMag = sqrt( (chromaR - 1.0)*(chromaR - 1.0) + (chromaG - 1.0)*(chromaG - 1.0) + (chromaB - 1.0)*(chromaB - 1.0) );
    float chromaSuppress = 1.0 - smoothstep(0.84, 0.96, mappedY);
    chromaMag *= chromaSuppress;

    float3 unitChroma = normalize(float3(chromaR, chromaG, chromaB) - 1.0);
    float3 linBT2020 = (1.0 + unitChroma * chromaMag) * mappedY;
    linBT2020 = saturate(linBT2020);

    static const float3x3 BT2020_TO_BT709 = float3x3(
         1.6605, -0.5876, -0.0729,
        -0.1246,  1.1329, -0.0083,
        -0.0182, -0.1006,  1.1187
    );
    float3 linBT709 = mul(BT2020_TO_BT709, linBT2020);

    float Y709 = dot(linBT709, float3(0.2126, 0.7152, 0.0722));
    float3 chroma709 = linBT709 - Y709;
    float maxC = max(linBT709.r, max(linBT709.g, linBT709.b));
    float minC = min(linBT709.r, min(linBT709.g, linBT709.b));
    float compress = 1.0;
    if(maxC > 1.0) compress = Y709 / maxC;
    if(minC < 0.0) compress = min(compress, Y709 / (Y709 - minC));
    linBT709 = Y709 + chroma709 * compress;
    linBT709 = saturate(linBT709);

    const float contrastBase = 0.97;
    const float contrastHigh = 0.88;
    float contrastLerp = smoothstep(0.70, 0.92, Y709);
    float contrast = lerp(contrastBase, contrastHigh, contrastLerp);
    linBT709 = Y709 + contrast * (linBT709 - Y709);

    linBT709 = max(linBT709, 0.0);
    float3 sdrRGB = LinearToSrgb(linBT709);	
    return saturate(sdrRGB);
}

float3 TonemapAMD_PQToSDR(float3 pqSignal, float maxNits = 400.0f)
{
    // PQ inverse OETF
	float3 nits = PQtoNits(pqSignal);

    float Y_nits = dot(nits, float3(0.2627, 0.6780, 0.0593));
    float mappedY = Y_nits / maxNits;

    const float knee = 0.74f;
    float over = max(mappedY - knee, 0.0f);
    mappedY = min(mappedY, knee) + over / (1.0f + over / (1.0f - knee));
    mappedY = saturate(mappedY);

    float scale = mappedY / max(Y_nits, 1e-6f);
    float3 linBT2020 = nits * scale;
    linBT2020 = saturate(linBT2020);

    // BT2020 -> BT709
    static const float3x3 BT2020_TO_BT709 = float3x3(
         1.6605, -0.5876, -0.0729,
        -0.1246,  1.1329, -0.0083,
        -0.0182, -0.1006,  1.1187
    );
    float3 linBT709 = mul(BT2020_TO_BT709, linBT2020);


    float Y709 = dot(linBT709, float3(0.2126, 0.7152, 0.0722));
    float maxC = max(linBT709.r, max(linBT709.g, linBT709.b));
    float minC = min(linBT709.r, min(linBT709.g, linBT709.b));
    float compress = 1.0f;
    if(maxC > 1.0f) compress = Y709 / maxC;
    if(minC < 0.0f) compress = min(compress, Y709 / (Y709 - minC));
    linBT709 = Y709 + (linBT709 - Y709) * compress;
    linBT709 = saturate(linBT709);

    
    const float contrast = 0.96f;
    linBT709 = Y709 + contrast * (linBT709 - Y709);

    linBT709 = max(linBT709, 0.0);
    float3 sdrRGB = LinearToSrgb(linBT709);	
    return saturate(sdrRGB);
}



float3 TonemapAMD_PQToSDR_Refined(float3 pqSignal, float maxNits = 380.0f)
{
	float3 nits = PQtoNits(pqSignal);

    float Y_nits = dot(nits, float3(0.2627, 0.6780, 0.0593));
    float mappedY = Y_nits / maxNits;

    // 【修改】更平缓的肩，rolloff更贴近参考：拐点更早、压缩更柔和
    const float knee = 0.68f;
    float over = max(mappedY - knee, 0.0f);
    mappedY = min(mappedY, knee) + over / (1.0f + over * 1.45);
    mappedY = saturate(mappedY);

    float scale = mappedY / max(Y_nits, 1e-6f);
    float3 linBT2020 = nits * scale;
    linBT2020 = saturate(linBT2020);

    static const float3x3 BT2020_TO_BT709 = float3x3(
         1.6605, -0.5876, -0.0729,
        -0.1246,  1.1329, -0.0083,
        -0.0182, -0.1006,  1.1187
    );
    float3 linBT709 = mul(BT2020_TO_BT709, linBT2020);

    float Y709 = dot(linBT709, float3(0.2126, 0.7152, 0.0722));
    float maxC = max(linBT709.r, max(linBT709.g, linBT709.b));
    float minC = min(linBT709.r, min(linBT709.g, linBT709.b));
    float compress = 1.0f;
    if(maxC > 1.0f) compress = Y709 / maxC;
    if(minC < 0.0f) compress = min(compress, Y709 / (Y709 - minC));
    linBT709 = Y709 + (linBT709 - Y709) * compress;
    linBT709 = saturate(linBT709);

    // 【修改】对比度轻微分区：中灰维持，亮部进一步柔化反差，压掉雪山亮部的“跳变感”
    float contrastMid = 0.96f;
    float contrastHigh = 0.92f;
    float cLerp = smoothstep(0.65, 0.88, Y709);
    float contrast = lerp(contrastMid, contrastHigh, cLerp);
    linBT709 = Y709 + contrast * (linBT709 - Y709);

    linBT709 = max(linBT709, 0.0);
    float3 sdrRGB = LinearToSrgb(linBT709);	
    return saturate(sdrRGB);
}



float3 TonemapAMD_PQToSDR_FinalTweak(float3 pqSignal, float maxNits = 380.0f)
{
	float3 nits = PQtoNits(pqSignal);

    float Y_nits = dot(nits, float3(0.2627, 0.6780, 0.0593));
    float mappedY = Y_nits / maxNits;

    // 1. 更平缓的高光肩，减少雪顶亮边
    const float knee = 0.66f;
    float over = max(mappedY - knee, 0.0f);
    mappedY = min(mappedY, knee) + over / (1.0f + over * 1.55f);
    mappedY = saturate(mappedY);

    float scale = mappedY / max(Y_nits, 1e-6f);
    float3 linBT2020 = nits * scale;
    linBT2020 = saturate(linBT2020);

    static const float3x3 BT2020_TO_BT709 = float3x3(
         1.6605, -0.5876, -0.0729,
        -0.1246,  1.1329, -0.0083,
        -0.0182, -0.1006,  1.1187
    );
    float3 linBT709 = mul(BT2020_TO_BT709, linBT2020);

    float Y709 = dot(linBT709, float3(0.2126, 0.7152, 0.0722));
    float3 chroma709 = linBT709 - Y709;

    float maxC = max(linBT709.r, max(linBT709.g, linBT709.b));
    float minC = min(linBT709.r, min(linBT709.g, linBT709.b));
    float compress = 1.0f;
    if(maxC > 1.0f) compress = Y709 / maxC;
    if(minC < 0.0f) compress = min(compress, Y709 / (Y709 - minC));
    linBT709 = Y709 + chroma709 * compress;
    linBT709 = saturate(linBT709);

    // 2. 极高光极弱色度阻尼，让亮部青调缓慢收敛，不做人工漂白
    float chromaDamp = 1.0f - 0.12f * smoothstep(0.91f, 0.97f, Y709);
    linBT709 = Y709 + chroma709 * chromaDamp;
    linBT709 = saturate(linBT709);

    // 3. 亮部微对比进一步细分，保留雪面纹理但压掉硬亮边
    float contrastMid = 0.96f;
    float contrastHigh = 0.97f;
    float cLerp = smoothstep(0.62f, 0.86f, Y709);
    float contrast = lerp(contrastMid, contrastHigh, cLerp);
    linBT709 = Y709 + contrast * (linBT709 - Y709);

    linBT709 = max(linBT709, 0.0);
    float3 sdrRGB = LinearToSrgb(linBT709);	
    return saturate(sdrRGB);
}


/*

float3 TonemapAMD_PQToSDR_MatchFinal(float3 pqSignal, float maxNits = 380.0f, float knee=0.66f, float HighLightFall=1.55f)
{
	float3 nits = PQtoNits(pqSignal);

    float Y_nits = dot(nits, float3(0.2627, 0.6780, 0.0593));
    float mappedY = Y_nits / maxNits;

    // Shoulder curve, keep original rolloff
    float over = max(mappedY - knee, 0.0f);
    mappedY = min(mappedY, knee) + over / (1.0f + over * HighLightFall);
    mappedY = saturate(mappedY);

    float scale = mappedY / max(Y_nits, 1e-6f);
    float3 linBT2020 = nits * scale;
    linBT2020 = saturate(linBT2020);

    float Y2020 = dot(linBT2020, float3(0.2627, 0.6780, 0.0593));
    float3 chroma2020 = linBT2020 - Y2020;

    float contrastShadow = 0.94f;
    float contrastMid = 0.96f;
    float contrastHigh = 0.90f;
    float lerpShadow = smoothstep(0.05f, 0.35f, Y2020);
    float lerpHigh = smoothstep(0.62f, 0.86f, Y2020);
    float contrast = lerp(contrastShadow, contrastMid, lerpShadow);
    contrast = lerp(contrast, contrastHigh, lerpHigh);
    linBT2020 = Y2020 + contrast * chroma2020;

    // Gamut convert BT2020 -> BT709
    static const float3x3 BT2020_TO_BT709 = float3x3(
         1.6605, -0.5876, -0.0729,
        -0.1246,  1.1329, -0.0083,
        -0.0182, -0.1006,  1.1187
    );
    float3 linBT709 = mul(BT2020_TO_BT709, linBT2020);

    // ========= NEW: Luminance preserving gamut compression (critical fix for red) =========
    const float gamutCompressThreshold = 1.0f;
    float Y709 = dot(linBT709, float3(0.2126, 0.7152, 0.0722));
    float3 chroma709 = linBT709 - Y709;
    float maxRGB = max(max(linBT709.r, linBT709.g), linBT709.b);
    if(maxRGB > gamutCompressThreshold)
    {
        float compressFactor = gamutCompressThreshold / maxRGB;
        chroma709 *= compressFactor;
        linBT709 = Y709 + chroma709;
    }
    // Only remove negative values, keep compressed chroma
    linBT709 = max(linBT709, 0.0);
    
    float3 sdrRGB = LinearToSrgb(linBT709);	
    return saturate(sdrRGB);
}
*/

/*
float3 TonemapAMD_PQToSDR_MatchFinal(float3 pqSignal, float maxNits = 450.0f, float knee = 0.76f, float HighLightFall = 1.4f)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    const float pqMaxNits = 10000.0f;

    // Step1: BT.2390 EETF, operate on PQ domain (correct AMD capture path)
    float Y_pq = dot(pqSignal, float3(0.2627f, 0.6780f, 0.0593f));
    float mappedY_pq;
    if(Y_pq < knee)
    {
        mappedY_pq = Y_pq;
    }
    else
    {
        float excess = Y_pq - knee;
        mappedY_pq = knee + excess / (1.0f + excess * HighLightFall);
    }
    mappedY_pq = saturate(mappedY_pq);

    float scalePQ = mappedY_pq / max(Y_pq, 1e-6f);
    float3 pqMapped = pqSignal * scalePQ;
    pqMapped = max(pqMapped, 0.0f);

    // Step2: Decode PQ once to linear BT.2020 (0~10000 nits)
    float3 Npow = pow(pqMapped, 1.0 / m2);
    float3 num = max(Npow - c1, 0.0);
    float3 den = c2 - c3 * Npow;
    float3 linBT2020 = pow(num / den, 1.0 / m1);

    // Step3: This is where maxNits takes effect: normalize linear nits to SDR white point
    float3 linBT2020_norm = linBT2020 * (pqMaxNits / maxNits);

    // Step4: Gamut convert BT2020 -> BT709
    static const float3x3 BT2020_TO_BT709 = float3x3(
         1.6605f, -0.5876f, -0.0729f,
        -0.1246f,  1.1329f, -0.0083f,
        -0.0182f, -0.1006f,  1.1187f
    );
    float3 linBT709 = mul(BT2020_TO_BT709, linBT2020_norm);
    linBT709 = max(linBT709, 0.0f);

    // Step5: Generic hue-preserving gamut mapping, no color-specific branch
    float maxRGB = max(max(linBT709.r, linBT709.g), linBT709.b);
    float minRGB = min(min(linBT709.r, linBT709.g), linBT709.b);
    float chromaMag = maxRGB - minRGB;
    const float chromaThreshold = 0.10f;
    const float gamutTrigger = 1.03f;

    if(maxRGB > gamutTrigger)
    {
        if(chromaMag > chromaThreshold)
        {
            float clipFactor = gamutTrigger / maxRGB;
            linBT709 *= clipFactor;
        }
        else
        {
            float Y709 = dot(linBT709, float3(0.2126f, 0.7152f, 0.0722f));
            float3 chroma709 = linBT709 - Y709;
            float excess = maxRGB - gamutTrigger;
            float compress = 1.0f - 0.12f * excess;
            compress = max(compress, 0.85f);
            chroma709 *= compress;
            linBT709 = Y709 + chroma709;
            linBT709 = max(linBT709, 0.0f);
        }
    }

    float3 sdrRGB = LinearToSrgb(linBT709);
    return saturate(sdrRGB);
}*/

/*
float3 TonemapAMD_PQToSDR_MatchFinal(float3 pqSignal, float maxNits = 550.0f, float knee = 0.76f, float HighLightFall = 1.4f)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    const float pqMaxNits = 10000.0f;

    // Step1: BT.2390 style EETF, operate purely on PQ encoded domain
    float Y_pq = dot(pqSignal, float3(0.2627f, 0.6780f, 0.0593f));
    float mappedY_pq;
    if(Y_pq < knee)
    {
        mappedY_pq = Y_pq;
    }
    else
    {
        float excess = Y_pq - knee;
        mappedY_pq = knee + excess / (1.0f + excess * HighLightFall);
    }
    mappedY_pq = saturate(mappedY_pq);

    float scalePQ = mappedY_pq / max(Y_pq, 1e-6f);
    float3 pqMapped = pqSignal * scalePQ;
    pqMapped = max(pqMapped, 0.0f);

    // Step2: Single PQ decode to linear BT.2020 light
    float3 Npow = pow(pqMapped, 1.0 / m2);
    float3 num = max(Npow - c1, 0.0);
    float3 den = c2 - c3 * Npow;
    float3 linBT2020 = pow(num / den, 1.0 / m1);

    // Step3: White point normalization, maxNits controls SDR reference white
    float3 linBT2020_norm = linBT2020 * (pqMaxNits / maxNits);

    // Step4: Standard BT.2020 to BT.709 gamut conversion matrix
    static const float3x3 BT2020_TO_BT709 = float3x3(
         1.6605f, -0.5876f, -0.0729f,
        -0.1246f,  1.1329f, -0.0083f,
        -0.0182f, -0.1006f,  1.1187f
    );
    float3 linBT709 = mul(BT2020_TO_BT709, linBT2020_norm);
    linBT709 = max(linBT709, 0.0f);

    // Step5: Generic hue-preserving gamut clipping, conservative neutral chroma handling
    float maxRGB = max(max(linBT709.r, linBT709.g), linBT709.b);
    float minRGB = min(min(linBT709.r, linBT709.g), linBT709.b);
    float chromaMag = maxRGB - minRGB;
    const float chromaThreshold = 0.10f;
    const float gamutTrigger = 1.03f;

    if(maxRGB > gamutTrigger)
    {
        if(chromaMag > chromaThreshold)
        {
            float clipFactor = gamutTrigger / maxRGB;
            linBT709 *= clipFactor;
        }
        else
        {
            float Yneu = dot(linBT709, float3(0.2126f, 0.7152f, 0.0722f));
            float3 chromaNeu = linBT709 - Yneu;
            float excess = maxRGB - gamutTrigger;
            float compress = 1.0f - 0.04f * excess;
            compress = max(compress, 0.92f);
            chromaNeu *= compress;
            linBT709 = Yneu + chromaNeu;
            linBT709 = max(linBT709, 0.0f);
        }
    }

    // Step6: Linear BT709 to sRGB encoding
    float3 sdrRGB = LinearToSrgb(linBT709);
    return saturate(sdrRGB);
}

*/

/*
float3 TonemapAMD_PQToSDR_MatchFinal(float3 pqSignal, float exposure = 1.5f, float maxNits = 550.0f, float knee = 0.76f, float HighLightFall = 1.4f)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    const float pqMaxNits = 10000.0f;

    // Step1: BT.2390 style EETF, operate purely on PQ encoded domain
    float Y_pq = dot(pqSignal, float3(0.2627f, 0.6780f, 0.0593f));
    float mappedY_pq;
    if(Y_pq < knee)
    {
        mappedY_pq = Y_pq;
    }
    else
    {
        float excess = Y_pq - knee;
        mappedY_pq = knee + excess / (1.0f + excess * HighLightFall);
    }
    mappedY_pq = saturate(mappedY_pq);

    float scalePQ = mappedY_pq / max(Y_pq, 1e-6f);
    float3 pqMapped = pqSignal * scalePQ;
    pqMapped = max(pqMapped, 0.0f);

    // Step2: Single PQ decode to linear BT.2020 light
    float3 Npow = pow(pqMapped, 1.0 / m2);
    float3 num = max(Npow - c1, 0.0);
    float3 den = c2 - c3 * Npow;
    float3 linBT2020 = pow(num / den, 1.0 / m1);

    // Step3: White point normalization
    float3 linBT2020_norm = linBT2020 * (pqMaxNits / maxNits);

    // Step4: Apply exposure
    float3 linBT2020_exposed = linBT2020_norm * exposure;

    // Step5: Standard BT.2020 to BT.709 gamut conversion matrix
    static const float3x3 BT2020_TO_BT709 = float3x3(
         1.6605f, -0.5876f, -0.0729f,
        -0.1246f,  1.1329f, -0.0083f,
        -0.0182f, -0.1006f,  1.1187f
    );
    float3 linBT709 = mul(BT2020_TO_BT709, linBT2020_exposed);
    linBT709 = max(linBT709, 0.0f);

    // Step6: Generic hue-preserving gamut clipping
    float maxRGB = max(max(linBT709.r, linBT709.g), linBT709.b);
    float minRGB = min(min(linBT709.r, linBT709.g), linBT709.b);
    float chromaMag = maxRGB - minRGB;
    const float chromaThreshold = 0.10f;
    // Adaptive trigger: higher exposure relax clip limit, allow hitting 1.0
    float gamutTrigger = lerp(1.03f, 1.45f, saturate((exposure - 1.0f) / 2.0f));

    if(maxRGB > gamutTrigger)
    {
        if(chromaMag > chromaThreshold)
        {
            float clipFactor = gamutTrigger / maxRGB;
            linBT709 *= clipFactor;
        }
        else
        {
            float Yneu = dot(linBT709, float3(0.2126f, 0.7152f, 0.0722f));
            float3 chromaNeu = linBT709 - Yneu;
            float excess = maxRGB - gamutTrigger;
            float compress = 1.0f - 0.04f * excess;
            compress = max(compress, 0.92f);
            chromaNeu *= compress;
            linBT709 = Yneu + chromaNeu;
            linBT709 = max(linBT709, 0.0f);
        }
    }

    // Step7: Linear BT709 to sRGB encoding
    float3 sdrRGB = LinearToSrgb(linBT709);
    return saturate(sdrRGB);
}
*/


/*
float3 TonemapAMD_PQToSDR_MatchFinal(float3 pqSignal, float exposure = 1.5f, float maxNits = 550.0f, float knee = 0.76f, float HighLightFall = 1.4f)
{

    // Step1: BT.2390 style EETF, operate purely on PQ encoded domain
    float Y_pq = dot(pqSignal, float3(0.2627f, 0.6780f, 0.0593f));
    float mappedY_pq;
    if(Y_pq < knee)
    {
        mappedY_pq = Y_pq;
    }
    else
    {
        float excess = Y_pq - knee;
        mappedY_pq = knee + excess / (1.0f + excess * HighLightFall);
    }
    mappedY_pq = saturate(mappedY_pq);

    float scalePQ = mappedY_pq / max(Y_pq, 1e-6f);
    float3 pqMapped = pqSignal * scalePQ;
    pqMapped = max(pqMapped, 0.0f);

    // Step2: Single PQ decode to linear BT.2020 light
	float3 linBT2020 = PQtoNits(pqMapped);

    // Step3: White point normalization
    float3 linBT2020_norm = linBT2020 / maxNits;

    // Step4: Apply exposure
    float3 linBT2020_exposed = linBT2020_norm * exposure;

    // Step5: Standard BT.2020 to BT.709 gamut conversion matrix
    static const float3x3 BT2020_TO_BT709 = float3x3(
         1.6605f, -0.5876f, -0.0729f,
        -0.1246f,  1.1329f, -0.0083f,
        -0.0182f, -0.1006f,  1.1187f
    );
    float3 linBT709 = mul(BT2020_TO_BT709, linBT2020_exposed);
    linBT709 = max(linBT709, 0.0f);

    // Step6: Generic hue-preserving gamut clipping
    float maxRGB = max(max(linBT709.r, linBT709.g), linBT709.b);
    float minRGB = min(min(linBT709.r, linBT709.g), linBT709.b);
    float chromaMag = maxRGB - minRGB;
    const float chromaThreshold = 0.10f;
    // Adaptive trigger: higher exposure relax clip limit, allow hitting 1.0
    float gamutTrigger = lerp(1.03f, 1.45f, saturate((exposure - 1.0f) / 2.0f));

    if(maxRGB > gamutTrigger)
    {
        if(chromaMag > chromaThreshold)
        {
            float clipFactor = gamutTrigger / maxRGB;
            linBT709 *= clipFactor;
        }
        else
        {
            float Yneu = dot(linBT709, float3(0.2126f, 0.7152f, 0.0722f));
            float3 chromaNeu = linBT709 - Yneu;
            float excess = maxRGB - gamutTrigger;
            float compress = 1.0f - 0.04f * excess;
            compress = max(compress, 0.92f);
            chromaNeu *= compress;
            linBT709 = Yneu + chromaNeu;
            linBT709 = max(linBT709, 0.0f);
        }
    }

    // Step7: Linear BT709 to sRGB encoding
    float3 sdrRGB = LinearToSrgb(linBT709);
    return saturate(sdrRGB);
}
*/


float3 TonemapAMD_PQToSDR_MatchFinal(float3 pqSignal, float exposure = 1.5f, float maxNits = 550.0f, float knee = 0.76f, float HighLightFall = 1.4f)
{
    // --- Step 1: BT.2390 EETF (PQ encoded domain) ---
    // 提取 PQ 信号的亮度（BT.2020 权重）
    float Y_pq = dot(pqSignal, float3(0.2627f, 0.6780f, 0.0593f));
    float mappedY_pq;
    // 如果亮度低于拐点，直接保留
    if (Y_pq < knee)
    {
        mappedY_pq = Y_pq;
    }
    else
    {
        // 如果亮度高于拐点，使用双曲线软肩进行高光压缩
        float excess = Y_pq - knee;
        mappedY_pq = knee + excess / (1.0f + excess * HighLightFall);
    }
    
    // 计算亮度缩放系数，并将其应用到整个 PQ 信号上
    float scalePQ = saturate(mappedY_pq) / max(Y_pq, 1e-6f);
    float3 pqMapped = max(pqSignal * scalePQ, 0.0f);

    // --- Step 2: PQ decode to linear BT.2020 ---
    // 对压缩后的 PQ 信号进行解码，得到线性 BT.2020 物理亮度 (Nits)
    float3 linBT2020 = PQtoNits(pqMapped);

    // --- Step 3 & 4: White point normalization and Exposure ---
    // 白点归一化，并应用曝光系数
    float3 linBT2020_exposed = (linBT2020 / maxNits) * exposure;

    // --- Step 5: BT.2020 to BT.709 conversion ---
    // 使用标准矩阵将 BT.2020 色域转换到 BT.709 色域
    static const float3x3 BT2020_TO_BT709 = float3x3(
         1.6605f, -0.5876f, -0.0729f,
        -0.1246f,  1.1329f, -0.0083f,
        -0.0182f, -0.1006f,  1.1187f
    );
    // 进行色域转换，并防止出现负数
    float3 linBT709 = max(mul(BT2020_TO_BT709, linBT2020_exposed), 0.0f);

    // --- Step 6: Hue-preserving gamut clipping ---
    // 计算 RGB 的最大值、最小值和色度跨度
    float maxRGB = max(max(linBT709.r, linBT709.g), linBT709.b);
    float minRGB = min(min(linBT709.r, linBT709.g), linBT709.b);
    float chromaMag = maxRGB - minRGB;
    const float chromaThreshold = 0.10f;
    
    // 根据曝光系数动态调整色域钳位触发阈值
    float gamutTrigger = lerp(1.03f, 1.45f, saturate((exposure - 1.0f) / 2.0f));

    // 如果最大通道值超过触发阈值，进行色域压缩
    if (maxRGB > gamutTrigger)
    {
        // 高饱和度：等比例缩放 RGB，保持色相不变
        if (chromaMag > chromaThreshold)
        {
            linBT709 *= (gamutTrigger / maxRGB);
        }
        else
        {
            // 低饱和度：保持亮度，仅压缩色度
            float Yneu = dot(linBT709, float3(0.2126f, 0.7152f, 0.0722f));
            float3 chromaNeu = linBT709 - Yneu;
            float excess = maxRGB - gamutTrigger;
            float compress = max(1.0f - 0.04f * excess, 0.92f);
            
            linBT709 = max(Yneu + chromaNeu * compress, 0.0f);
        }
    }

    // --- Step 7: Linear to sRGB encoding ---
    // 将线性颜色转换为 sRGB 编码，并钳位到 [0,1] 范围
    float3 sdrRGB = LinearToSrgb(linBT709);
    return saturate(sdrRGB);
}


float3 FakeHDR(float3 pqSignal, float nitsExposure = 1.5f)
{
	//return FakeHDR3(pqSignal,maxDisplayNits);
	return TonemapAMD_PQToSDR_MatchFinal( pqSignal, nitsExposure);
}


#endif
