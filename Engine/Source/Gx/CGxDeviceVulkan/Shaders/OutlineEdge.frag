// The unit outline's edge pass (Engine/Source/UnitOutline). Not a stock shader: it draws a screen-sized
// quad over the finished 3D view and puts the outline where it belongs.
//
//   tex0   a group's masks, cleared to white and stamped by erasing: R = 1 - silhouette (untested),
//          G = 1 - the silhouette that survives the depth test (what the camera can see)
//   tex1   the local player's silhouette, R = 1 - silhouette (the line is never drawn over the player)
//   vColor the line's colour in rgb and its strength in alpha
//   pc.params.x   the line's width in 1/16 pixels (the engine's alpha reference, which no pass of this
//                 program uses for a test)
//
// A pixel is a line pixel when the visible silhouette is within the width of it and it is neither inside
// the silhouette nor inside the player's. The shape comes from the visible mask's neighbours, the
// interior test from the untested one, so a wall covering part of the unit does not draw its own rim.
#version 450
#extension GL_GOOGLE_include_directive : require

#include "Push.glsl"

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vTexCoord0;
layout(location = 2) in vec2 vTexCoord1;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D tex0;
layout(set = 0, binding = 1) uniform sampler2D tex1;

void main()
{
    vec2  uv     = vTexCoord0;
    vec2  texel  = 1.0 / vec2(textureSize(tex0, 0));
    float radius = max(pc.params.x * (1.0 / 16.0), 0.5);

    float inside = 1.0 - texture(tex0, uv).r;
    float player = 1.0 - texture(tex1, uv).r;

    // Three rings of taps; the outermost fades so the line has a soft outer edge instead of a stair.
    float cover = 1.0 - texture(tex0, uv).g;
    for (int i = 0; i < 12; ++i)
    {
        float a = 6.2831853 * float(i) / 12.0;
        vec2  d = vec2(cos(a), sin(a)) * texel;
        cover = max(cover, (1.0 - texture(tex0, uv + d * radius * 0.5).g));
        cover = max(cover, (1.0 - texture(tex0, uv + d * radius * 0.8).g));
        cover = max(cover, (1.0 - texture(tex0, uv + d * radius).g) * 0.7);
    }

    float alpha = cover * (1.0 - inside) * (1.0 - player) * vColor.a;
    outColor = vec4(vColor.rgb, alpha);
}
