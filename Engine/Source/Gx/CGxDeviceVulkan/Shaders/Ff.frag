// The fixed-function fragment program: D3D9's texture stages 0 and 1 as the engine configures them
// (GxRs_ColorOp0 / GxRs_AlphaOp0 over the texture and the vertex colour, then GxRs_ColorOp1 /
// GxRs_AlphaOp1 over the second texture and that result -- the minimap's circular mask, the
// terrain's alpha maps), then the alpha test. Without a texture at stage 0 the vertex colour
// passes through.
#version 450
#extension GL_GOOGLE_include_directive : require

#include "Push.glsl"
#include "Stage.glsl"
#include "AlphaTest.glsl"
#include "Fog.glsl"

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord0;
layout(location = 2) in vec2 vTexCoord1;
layout(location = 3) in float vFog;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D tex0;
layout(set = 0, binding = 1) uniform sampler2D tex1;

void main()
{
    // A stage without a texture binds the backend's 1x1 white, which Stages() does not read.
    vec4 cur = Stages(vColor, texture(tex0, vTexCoord0), texture(tex1, vTexCoord1));
    AlphaTest(cur.a);
    if (ff.p[15].w > 0.5) cur.rgb = mix(ff.p[15].rgb, cur.rgb, clamp(vFog, 0.0, 1.0));
    outColor = cur;
}
