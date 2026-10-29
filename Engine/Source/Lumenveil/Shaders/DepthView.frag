// lumenveilDepthView: the world's depth copy as grey, white at the camera, black at the far clip (on a log scale). The check
// the depth copy, the projection and the viewport the effects read agree with the picture.
#version 450

layout(location = 0) in vec2 uv;
layout(set = 0, binding = 0) uniform sampler2D depthCopy;
layout(set = 0, binding = 16) uniform Constants { vec4 c[256]; };
layout(location = 0) out vec4 colour;

void main()
{
    // c[0]: projection _33, _43, the far clip, the viewport's MinZ; c[1].x: 1 / (MaxZ - MinZ);
    // c[2]: the viewport's offset and size in the target, as fractions.
    float stored = texture(depthCopy, c[2].xy + uv * c[2].zw).r;
    // The engine's projection is GL style (z in -1..1); the device maps it onto the viewport's z range.
    float d = (stored - c[0].w) * c[1].x * 2.0 - 1.0;
    // Both terms are negative (_43 < 0, d < _33): z = _43 / (d - _33), kept off the pole at d = _33.
    float z = c[0].y / min(d - c[0].x, -1e-6);
    // Logarithmic: most of what the camera sees is within a hundred yards of a 2000-yard far clip.
    float shade = 1.0 - clamp(log2(1.0 + max(z, 0.0)) / log2(1.0 + c[0].z), 0.0, 1.0);
    colour = vec4(vec3(shade), 1.0);
}
