// Fixed-function state shares the unused high pixel registers with translated companions.
layout(set = 1, binding = 0, std140) uniform FixedFog
{
    vec4 p[16];
} ff;

float FogFactor(float eyeDepth)
{
    return ff.p[15].w > 0.5
        ? clamp((ff.p[14].y - eyeDepth) / (ff.p[14].y - ff.p[14].x), 0.0, 1.0)
        : 1.0;
}
