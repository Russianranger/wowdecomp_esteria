// Screen-space distortion for M3 DIS_ materials. The scene behind the mesh was copied into a texture; the normal map
// bends where it is sampled from. The vertex shader forwards the clip position (TEXCOORD5) so the pixel shader finds its
// own screen coordinate without VPOS, which the Vulkan translator does not handle. Pixel constants start at c16
// (c14/c15 belong to the Vulkan device's fog state).
#ifdef ASEC_VERTEX
row_major float4x4 viewProjection : register(c0);
struct Input { float3 position:POSITION;float3 normal:NORMAL;float2 uv0:TEXCOORD0;float2 uv1:TEXCOORD1;float2 uv2:TEXCOORD2;float2 uv3:TEXCOORD3; };
struct Varying { float4 position:POSITION;float3 normal:TEXCOORD0;float4 uv01:TEXCOORD1;float4 uv23:TEXCOORD2;float3 eyePosition:TEXCOORD4;float4 clip:TEXCOORD5; };
Varying Main(Input v){Varying o;o.position=mul(float4(v.position,1),viewProjection);o.normal=v.normal;o.uv01=float4(v.uv0,v.uv1);o.uv23=float4(v.uv2,v.uv3);o.eyePosition=v.position;o.clip=o.position;return o;}
#else
float4 settings : register(c16);  // opacity, strength factor, 1/texture width, 1/texture height
float4 controls : register(c17);  // displacement scale, normal is DXT5N, normal uv source, strength uv source
float4 normalTransform : register(c18);   // tiling xy, offset zw
float4 normalRotation : register(c19);    // cos, sin
float4 strengthTransform : register(c20);
float4 strengthRotation : register(c21);
float4 screenInfo : register(c22);        // screen width, screen height (pixels)
float4 strengthGain : register(c23);      // multiply, add, brightness, color channel operation
float4 fresnel : register(c24);           // type, exponent, minimum, maximum offset
sampler2D screen : register(s0);
sampler2D normalMap : register(s1);
sampler2D strengthMap : register(s2);
float2 Source(float source,float4 uv01,float4 uv23){return source<.5?uv01.xy:source<1.5?uv01.zw:source<2.5?uv23.xy:uv23.zw;}
float2 Transform(float2 uv,float4 transform,float4 rotation){
    uv*=transform.xy;
    return float2(rotation.x*uv.x-rotation.y*uv.y,rotation.y*uv.x+rotation.x*uv.y)+transform.zw;
}
float4 Main(float3 normal:TEXCOORD0,float4 uv01:TEXCOORD1,float4 uv23:TEXCOORD2,float3 eyePosition:TEXCOORD4,float4 clip:TEXCOORD5):COLOR0{
    float4 n=tex2D(normalMap,Transform(Source(controls.z,uv01,uv23),normalTransform,normalRotation));
    float2 xy=(controls.y>.5?n.ag:n.rg)*2-1;
    float4 s=tex2D(strengthMap,Transform(Source(controls.w,uv01,uv23),strengthTransform,strengthRotation));
    float op=strengthGain.w;
    float strength=op<1.5?max(max(s.r,s.g),s.b):op<2.5?s.a:op<3.5?s.r:op<4.5?s.g:s.b;
    strength=saturate((strength*strengthGain.x+strengthGain.y)*strengthGain.z);
    if(fresnel.x>.5){
        // The strength layer's Fresnel term: the distortion follows the view angle, as the surface material's does.
        float3 N=normalize(normal+1e-20);float3 V=normalize(-eyePosition+1e-20);
        float f=fresnel.y==0?1:pow(saturate(1-abs(dot(N,V))),fresnel.y);
        f=saturate(fresnel.z+fresnel.w*f);
        strength*=fresnel.x>1.5?1-f:f;
    }
    float2 ndc=clip.xy/clip.w;
    float2 pixel=float2(ndc.x*.5+.5,.5-ndc.y*.5)*screenInfo.xy;
    float2 uv=pixel*settings.zw+xy*strength*settings.y*controls.x;
    return float4(tex2D(screen,uv).rgb,saturate(settings.x));
}
#endif
