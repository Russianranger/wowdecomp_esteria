static const float kLampBodyYards = 1.5;
static const float kShadowStartYards = 0.2;
static const float kMaxShadowStartShare = 0.5;
static const float kShadowBias = 0.1;
static const float kShadowBiasPerViewZ = 0.003;
static const float kMinOccluderYards = 1;
static const float kOccluderThicknessInSteps = 1.5;
static const float kOccluderThicknessPerViewZ = 0.02;
static const float kMinSampleViewZ = 0.3;

// Not in Lumenveil: an occluder only a few inches wide (the crossbars on a lamp post, a chain) is too thin to cast a shadow that
// looks like one. It is thin when, a small step out to both sides (or both above and below) of it on screen, the scene is
// already farther away than it.
static const float kThinOccluderYards = 0.4;
static const float kThinFarYards = 0.25;
static const float kThinMinPixels = 1.5;

bool ThinOccluder(sampler2D depthTexture, float2 pixel, float viewZ)
{
    float pixelsPerYard = 0.5 * ViewportSize().y * ViewToNdcScale().y / viewZ;
    float offset = max(0.5 * kThinOccluderYards * pixelsPerYard, kThinMinPixels);
    float2 low = ViewportOrigin() + 0.5;
    float2 high = ViewportLastPixelCentre();
    float4 z = float4(LinearDepth(SampleDepth(depthTexture, clamp(pixel + float2(offset, 0), low, high))),
                      LinearDepth(SampleDepth(depthTexture, clamp(pixel - float2(offset, 0), low, high))),
                      LinearDepth(SampleDepth(depthTexture, clamp(pixel + float2(0, offset), low, high))),
                      LinearDepth(SampleDepth(depthTexture, clamp(pixel - float2(0, offset), low, high))));
    float far = viewZ + kThinFarYards;
    return (z.x > far && z.y > far) || (z.z > far && z.w > far);
}

float LampShadow(sampler2D depthTexture, float3 start, float3 toLamp, float distance, float jitter, int steps,
                 float clearYards, float startYards, float4 bodyBase, float4 bodyUp)
{
    float travel = saturate(1 - clearYards / max(distance, 1e-3));
    float3 path = toLamp * travel;
    float startShare = min(startYards / max(distance * travel, 1e-3), kMaxShadowStartShare);
    float thickness = max(kMinOccluderYards, distance * travel / steps * kOccluderThicknessInSteps);
    float3 lamp = start + toLamp;
    float lit = 1;
    [loop] for (int k = 0; k < steps && lit > 0; k++)
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
                float3 fromLamp = occluderPosition - lamp;
                bool occluder = !BeyondFarClip(sceneDepth) &&
                                behind > kShadowBias + kShadowBiasPerViewZ * samplePosition.z &&
                                behind < max(thickness, kOccluderThicknessPerViewZ * samplePosition.z) &&
                                dot(fromLamp, fromLamp) > clearYards * clearYards &&
                                !InsideBody(occluderPosition, bodyBase, bodyUp);
                [branch] if (occluder)
                    occluder = !ThinOccluder(depthTexture, samplePixel, sceneViewZ);
                lit = occluder ? 0 : 1;
            }
        }
    }
    return lit;
}
