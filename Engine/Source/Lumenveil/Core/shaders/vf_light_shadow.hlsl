#include "vf_common.hlsli"
#include "vf_lamp_shadow.hlsli"

// Not in Lumenveil: the sun's or moon's shadow of what is on screen (buildings, statues, characters), marched
// through the depth buffer with the lamp shadow test, aimed at a point cLightShadow.w yards toward the light; and,
// when the shaft memory is running, of everything the camera has seen along the light (the same shadow map the
// light shafts use), so a shadow stays when its occluder leaves the screen. The two are combined: shadow if either says so.
float4 cLightShadow : register(c9);
float4 cLightShadowMemory : register(c10);

// The fine memory's lookups (a shaft memory of finer texels, its own registers and stages 3 and 4).
float4 cMemoryU : register(c60);
float4 cMemoryV : register(c61);
float4 cMemoryL : register(c62);
float4 cMemoryGrid : register(c63);
float4 cMemoryBeforeU : register(c64);
float4 cMemoryBeforeV : register(c65);
float4 cMemoryBeforeL : register(c66);
float4 cMemoryBeforeGrid : register(c67);

sampler2D sDepth : register(s0);
sampler2D sOccluderDepth : register(s2);
sampler2D sShaftOccluders : register(s3);
sampler2D sShaftMemoryBefore : register(s4);

static const int kShadowSteps = 20;
static const float kClearYards = 0.5;
static const float kEdgeFadePixels = 160;

// LampShadow (vf_lamp_shadow.hlsli) with the shadow's strength fading out as the occluder's pixel nears the edge of the
// screen: an occluder that is about to leave the depth buffer would otherwise switch its shadow off in one frame.
float EdgeFadedShadow(sampler2D depthTexture, float3 start, float3 toLight, float distance, float jitter, int steps)
{
    float travel = saturate(1 - kClearYards / max(distance, 1e-3));
    float3 path = toLight * travel;
    float startShare = min(kShadowStartYards / max(distance * travel, 1e-3), kMaxShadowStartShare);
    float thickness = max(kMinOccluderYards, distance * travel / steps * kOccluderThicknessInSteps);
    float3 light = start + toLight;
    float shadow = 0;
    [loop] for (int k = 0; k < steps && shadow < 1; k++)
    {
        float3 samplePosition = start + path * lerp(startShare, 1, (k + jitter) / steps);
        [branch] if (samplePosition.z >= kMinSampleViewZ)
        {
            float2 samplePixel = ViewToPixel(samplePosition);
            [branch] if (!OutsideViewport(samplePixel))
            {
                float sceneDepth = SampleDepth(depthTexture, samplePixel);
                float sceneViewZ = LinearDepth(sceneDepth);
                float behind = samplePosition.z - sceneViewZ;
                float3 occluderPosition = samplePosition * (sceneViewZ / samplePosition.z);
                float3 fromLight = occluderPosition - light;
                bool occluder = !BeyondFarClip(sceneDepth) && behind > kShadowBias + kShadowBiasPerViewZ * samplePosition.z &&
                                behind < max(thickness, kOccluderThicknessPerViewZ * samplePosition.z) &&
                                dot(fromLight, fromLight) > kClearYards * kClearYards;
                float2 edge = min(samplePixel - ViewportOrigin(), ViewportOrigin() + ViewportSize() - samplePixel);
                shadow = occluder ? max(shadow, saturate(min(edge.x, edge.y) / kEdgeFadePixels)) : shadow;
            }
        }
    }
    return 1 - shadow;
}
// The memory keeps depth in eighths of a yard and the surface itself is in it: a surface must not shadow itself.
static const float kMemorySurfaceBias = 0.15;
static const float kMemoryDepthSteps = 8;
static const float kMemoryCodesPerDepthStep = 256;
static const float kMemoryCodesPerSlopeClass = 16;
static const float kMemorySubTexels = 4;
static const float kMemorySlopeClassesPerOctave = 1.5;
static const float kMemorySlopeOctaveOffset = 4;

struct MemoryLookup
{
    float3 u;
    float3 v;
    float3 l;
    float2 cameraTexel;
    float limitAtCamera;
    float texelsPerYard;
    float texelUv;
    float halfWindowTexels;
};

MemoryLookup LoadMemoryLookup(float4 u, float4 v, float4 l, float4 grid)
{
    MemoryLookup m;
    m.u = u.xyz;
    m.v = v.xyz;
    m.l = l.xyz;
    m.cameraTexel = float2(u.w, v.w);
    m.limitAtCamera = l.w;
    m.texelsPerYard = grid.x;
    m.texelUv = grid.y;
    m.halfWindowTexels = grid.z;
    return m;
}

float MemoryTexelBlocks(sampler2D map, MemoryLookup m, float2 texel, float2 sampleTexel, float sampleDepth)
{
    float code = tex2Dlod(map, float4((texel + 0.5) * m.texelUv, 0, 0)).r;
    float depthStep = floor(code / kMemoryCodesPerDepthStep);
    float rest = code - depthStep * kMemoryCodesPerDepthStep;
    float slopeClass = floor(rest / kMemoryCodesPerSlopeClass);
    float subTexel = rest - slopeClass * kMemoryCodesPerSlopeClass;
    float subRow = floor(subTexel / kMemorySubTexels);
    float2 stored = texel + (float2(subTexel - subRow * kMemorySubTexels, subRow) + 0.5) / kMemorySubTexels;
    float slope = exp2(slopeClass / kMemorySlopeClassesPerOctave - kMemorySlopeOctaveOffset);
    float margin = slope * length(stored - sampleTexel) / m.texelsPerYard;
    return (depthStep + 0.5) / kMemoryDepthSteps > sampleDepth + margin ? 1 : 0;
}

float SunVisibilityInMemory(sampler2D map, MemoryLookup m, float3 offsetFromCamera)
{
    float2 texels = float2(dot(offsetFromCamera, m.u), dot(offsetFromCamera, m.v)) * m.texelsPerYard;
    float visibility = 1;
    [branch] if (all(abs(texels) < m.halfWindowTexels))
    {
        float2 sampleTexel = m.cameraTexel + texels;
        float2 base = floor(sampleTexel - 0.5);
        float2 f = sampleTexel - 0.5 - base;
        float sampleDepth = m.limitAtCamera + dot(offsetFromCamera, m.l) + kMemorySurfaceBias;
        float blocked = 0;
        [loop] for (float corner = 0; corner < 4; corner++)
        {
            float2 offset = float2(frac(corner * 0.5) * 2, floor(corner * 0.5));
            float2 weight = lerp(1 - f, f, offset);
            blocked += weight.x * weight.y * MemoryTexelBlocks(map, m, base + offset, sampleTexel, sampleDepth);
        }
        visibility = 1 - blocked;
    }
    return visibility;
}

float4 main(float2 targetPixel : VPOS) : COLOR0
{
    float2 pixel = ViewportOrigin() + targetPixel + 0.5;
    float depth = SampleDepth(sDepth, pixel);
    [branch] if (BeyondFarClip(depth))
        return float4(1, 1, 1, 1);
    float3 surface = ViewRayAtUnitDepth(pixel) * LinearDepth(depth);
    float distance = cLightShadow.w;
    float lit = EdgeFadedShadow(sOccluderDepth, surface, cLightShadow.xyz * distance, distance, InterleavedGradientNoise(pixel), kShadowSteps);
    [branch] if (cLightShadowMemory.x > 0.5)
    {
        float3 fromCamera = ViewToWorldDirection(surface);
        float remembered = SunVisibilityInMemory(sShaftOccluders, LoadMemoryLookup(cMemoryU, cMemoryV, cMemoryL, cMemoryGrid), fromCamera);
        [branch] if (cMemoryBeforeGrid.w > 0)
            remembered = lerp(remembered,
                              SunVisibilityInMemory(sShaftMemoryBefore, LoadMemoryLookup(cMemoryBeforeU, cMemoryBeforeV, cMemoryBeforeL, cMemoryBeforeGrid), fromCamera),
                              cMemoryBeforeGrid.w);
        lit = min(lit, remembered);
    }
    return float4(lit, lit, lit, 1);
}
