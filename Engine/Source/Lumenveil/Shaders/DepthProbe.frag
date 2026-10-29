// The depth copy's stored values at a grid of points over the viewport, into a small R32F target the CPU reads back:
// the log line that says what the depth copy holds when lumenveilDepthView looks wrong.
#version 450

layout(location = 0) in vec2 uv;
layout(set = 0, binding = 0) uniform sampler2D depthCopy;
layout(set = 0, binding = 16) uniform Constants { vec4 c[256]; };
layout(location = 0) out vec4 colour;

void main()
{
    // c[2]: the viewport's offset and size in the target, as fractions (as DepthView.frag).
    colour = vec4(texture(depthCopy, c[2].xy + uv * c[2].zw).r, 0.0, 0.0, 1.0);
}
