#include "vf_common.hlsli"
#include "vf_water_surface.hlsli"
#include "vf_water_rings.hlsli"

float4 cWaveLookup[7] : register(c16);
float4 cWaveTurns : register(c23);

sampler2D sWaves0 : register(s5);
sampler2D sWaves1 : register(s6);
sampler2D sWaves2 : register(s7);

static const float kMinFootprintGrazing = 0.02;

float UnresolvedRoughness()
{
    return cWaveOrigin.w;
}

float2 LookupStep(float4 lookup, float2 step)
{
    return float2(dot(lookup.zw, step), dot(float2(-lookup.w, lookup.z), step));
}

// Not in Lumenveil, which reads a mipmapped texture with tex2Dgrad: the wave levels are stacked in one atlas (256 texels
// wide, level n starting at row 512 - 512 / 2^n), wrapped and filtered here: bilinear in two levels, blended by the
// footprint's level of detail, with up to 4 taps along the longer axis (anisotropic, level from the shorter).
static const float kWaveTexels = 256;
static const float kAtlasRows = 511;
static const float kLastWaveLevel = 8;
static const float kSlopeWaterMargin = 0.01;

float4 AtlasTexel(sampler2D atlas, float level, float2 texel)
{
    float texels = kWaveTexels / exp2(level);
    float2 wrapped = texel - texels * floor(texel / texels);
    float rowOrigin = 2 * kWaveTexels - 2 * kWaveTexels / exp2(level);
    return tex2Dlod(atlas, float4((wrapped.x + 0.5) / kWaveTexels, (rowOrigin + wrapped.y + 0.5) / kAtlasRows, 0, 0));
}

float4 AtlasBilinear(sampler2D atlas, float level, float2 uv)
{
    float texels = kWaveTexels / exp2(level);
    float2 p = uv * texels - 0.5;
    float2 base = floor(p);
    float2 f = p - base;
    float4 top = lerp(AtlasTexel(atlas, level, base), AtlasTexel(atlas, level, base + float2(1, 0)), f.x);
    float4 bottom = lerp(AtlasTexel(atlas, level, base + float2(0, 1)), AtlasTexel(atlas, level, base + float2(1, 1)), f.x);
    return lerp(top, bottom, f.y);
}

static const float kMaxAnisotropy = 4;

float4 LayerMoments(sampler2D waves, float4 lookup, float2 position, float2 dx, float2 dy)
{
    float2 uv = lookup.xy + LookupStep(lookup, position);
    float2 sx = LookupStep(lookup, dx);
    float2 sy = LookupStep(lookup, dy);
    float lx = length(sx);
    float ly = length(sy);
    float2 major = lx > ly ? sx : sy;
    float majorLength = max(lx, ly);
    float minorLength = max(min(lx, ly), majorLength / kMaxAnisotropy);
    float taps = clamp(ceil(majorLength / max(minorLength, 1e-9)), 1, kMaxAnisotropy);
    float lod = clamp(log2(max(minorLength * kWaveTexels, 1)), 0, kLastWaveLevel);
    float low = floor(lod);
    float next = min(low + 1, kLastWaveLevel);
    float4 sum = 0;
    [loop] for (int i = 0; i < 4; i++)
    {
        [branch] if (i < taps)
        {
            float2 at = uv + major * ((i + 0.5) / taps - 0.5);
            sum += lerp(AtlasBilinear(waves, low, at), AtlasBilinear(waves, next, at), lod - low);
        }
    }
    return sum / taps;
}

float2 Turn(float2 slope, float2 turn)
{
    return float2(turn.x * slope.x - turn.y * slope.y, turn.y * slope.x + turn.x * slope.y);
}

float2 LayerVariance(float4 moments)
{
    return max(moments.zw - moments.xy * moments.xy, 0);
}

float2 FootprintStep(float3 rayWorld, float3 stepWorld, float surfaceZ)
{
    float limit = kMinFootprintGrazing * length(rayWorld);
    float rise = rayWorld.z < 0 ? min(rayWorld.z, -limit) : max(rayWorld.z, limit);
    return surfaceZ * (stepWorld.xy - rayWorld.xy * (stepWorld.z / rise));
}

float4 main(float2 vpos : VPOS) : COLOR0
{
    float2 pixel = vpos + 0.5;
    // Not in Lumenveil (which draws only where the client marked water in the stencil): only where the surface the water
    // wrote is nearer than what was there before it.
    [branch] if (!(ViewZBefore(pixel) > ViewZAfter(pixel) + kSlopeWaterMargin))
        return float4(0, 0, 0, 1);
    WaterSurface s = ReadGeometry(pixel);
    float3 rayWorld = ViewToWorldDirection(s.ray);
    float2 perPixel = 2 / (ViewportSize() * ViewToNdcScale());
    float2 dx = FootprintStep(rayWorld, ViewToWorldDirection(float3(perPixel.x, 0, 0)), s.surfaceZ);
    float2 dy = FootprintStep(rayWorld, ViewToWorldDirection(float3(0, -perPixel.y, 0)), s.surfaceZ);
    float2 position = s.relative.xy;
    float4 m0 = LayerMoments(sWaves0, cWaveLookup[0], position, dx, dy);
    float4 m1 = LayerMoments(sWaves1, cWaveLookup[1], position, dx, dy);
    float4 m2 = LayerMoments(sWaves1, cWaveLookup[2], position, dx, dy);
    float4 m3 = LayerMoments(sWaves1, cWaveLookup[3], position, dx, dy);
    float4 m4 = LayerMoments(sWaves2, cWaveLookup[4], position, dx, dy);
    float4 m5 = LayerMoments(sWaves2, cWaveLookup[5], position, dx, dy);
    float4 m6 = LayerMoments(sWaves2, cWaveLookup[6], position, dx, dy);
    float2 slope = m0.xy + m1.xy + m4.xy + Turn(m2.xy + m5.xy, cWaveTurns.xy) + Turn(m3.xy + m6.xy, cWaveTurns.zw);
    float2 variance = LayerVariance(m0) + LayerVariance(m1) + LayerVariance(m2) + LayerVariance(m3) +
                      LayerVariance(m4) + LayerVariance(m5) + LayerVariance(m6);
    float roughness = UnresolvedRoughness() + variance.x + variance.y;
    float3 rings = WaterRings(s.relative, dx, dy);
    slope += rings.xy;
    roughness += rings.z;
    return float4(slope, roughness, 1);
}
