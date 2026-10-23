// Module-owned initial standard-layer shader. Source UVs stay independent.
// Node/effect and exact source lighting paths remain separate work.
#ifdef ASEC_VERTEX
row_major float4x4 viewProjection : register(c0);
struct Input { float3 position:POSITION;float3 normal:NORMAL;float4 color:COLOR0;float2 uv0:TEXCOORD0;float2 uv1:TEXCOORD1;float2 uv2:TEXCOORD2;float2 uv3:TEXCOORD3;float3 tangent:TEXCOORD4;float sign:TEXCOORD5; };
struct Varying { float4 position:POSITION;float4 color:COLOR0;float3 normal:TEXCOORD0;float4 uv01:TEXCOORD1;float4 uv23:TEXCOORD2;float4 tangent:TEXCOORD3;float3 eyePosition:TEXCOORD4; };
Varying Main(Input v){Varying o;o.position=mul(float4(v.position,1),viewProjection);o.color=v.color;o.normal=v.normal;o.uv01=float4(v.uv0,v.uv1);o.uv23=float4(v.uv2,v.uv3);o.tangent=float4(v.tangent,v.sign);o.eyePosition=v.position;return o;}
#else
// Pixel constants start at c16: rows c14/c15 belong to the Vulkan device's fog state.
float4 settings : register(c16); // opacity, lit, alpha threshold, emissive multiplier
float4 modes : register(c17); // emissive blend modes, diffuse team-color enable, specular channel mode
float4 team : register(c18);
float4 vertexFlags : register(c19); // source vertex color/alpha enable, specular exponent/HDR
float4 layer[65] : register(c20); // 13 slots x (metadata,color,gain,UV,rotation)
sampler2D maps[11] : register(s0);
#ifdef ASEC_ENV_CUBE
samplerCUBE environmentMap : register(s11);
#else
sampler2D environmentMap : register(s11);
#endif
sampler2D environmentMask : register(s12);
float4 environmentControls : register(c85); // blend, HDR, Fresnel min/max offset
row_major float4x4 environmentView : register(c86);
float4 worldAmbient : register(c90); // RGB, native world environment enabled
float4 worldDiffuse : register(c91);
float4 worldSpecular : register(c92); // RGB, point light count
float4 worldSun : register(c93); // camera-space direction toward the light
float4 worldFog : register(c94); // start, inverse range, density, distance scale
float4 worldFogColor : register(c95); // RGB, enabled after material flags
float4 worldPoints[12] : register(c96); // position, RGB, attenuation per point
float4 worldClip : register(c108); // camera-space half-space, enabled by worldSun.w
float3 Unit(float3 value,float3 fallback){float length2=dot(value,value);return length2>1e-12?value*rsqrt(length2):fallback;}
float2 TransformUV(int slot,float2 uv){
    int base=slot*5;
    float4 transform=layer[base+3];uv*=transform.xy;
    float2 rotation=layer[base+4].xy;
    uv=float2(rotation.x*uv.x-rotation.y*uv.y,rotation.y*uv.x+rotation.x*uv.y)+transform.zw;
    return uv;
}
float2 LayerUV(int slot,float4 uv01,float4 uv23){float source=layer[slot*5].w;return TransformUV(slot,source<.5?uv01.xy:source<1.5?uv01.zw:source<2.5?uv23.xy:uv23.zw);}
float4 FinishLayer(int slot,float4 color){
    int base=slot*5;float4 meta=layer[base];
    if(slot==9&&modes.w>0.5)color=color.aaaa; // explicit scalar specular input
    else if(meta.z<0.5)color.a=1; // RGB
    else if(meta.z>1.5){float value=meta.z<2.5?color.a:meta.z<3.5?color.r:meta.z<4.5?color.g:color.b;color=value.xxxx;}
    float4 gain=layer[base+2];color=color*gain.x+gain.y;color.rgb*=gain.z;
    if(gain.w>.5)color=saturate(color); // explicit normalized MADD channel clamp
    return color;
}
float4 ReadLayer(int slot,float4 uv01,float4 uv23,float4 fallback){
    int base=slot*5;float4 meta=layer[base];if(meta.x<.5)return fallback;
    float4 color=layer[base+1];if(meta.y>.5)color=tex2D(maps[slot],LayerUV(slot,uv01,uv23));
    return FinishLayer(slot,color);
}
float4 ReadEnvironment(float3 reflection,float4 uv01,float4 uv23){
    float4 value=layer[56];
    if(layer[55].y>.5){
#ifdef ASEC_ENV_CUBE
        value=texCUBE(environmentMap,reflection);
#else
        float source=layer[55].w;float2 uv;
        if(source==3){
            // Conventional D3D camera-space sphere projection. Exact SC2
            // projection/orientation parity still requires visual evidence.
            float3 r=mul(float4(reflection,0),environmentView).xyz;
            float m=2*sqrt(max(dot(r.xy,r.xy)+(1-r.z)*(1-r.z),1e-12));
            uv=float2(.5+r.x/m,.5-r.y/m);
        }else uv=source<.5?uv01.xy:source<1.5?uv01.zw:source<9.5?uv23.xy:uv23.zw;
        value=tex2D(environmentMap,TransformUV(11,uv));
#endif
    }
    return FinishLayer(11,value);
}
float3 EnvironmentMask(float4 uv01,float4 uv23){
    if(layer[60].x<.5)return 1;
    float4 value=layer[61];if(layer[60].y>.5)value=tex2D(environmentMask,LayerUV(12,uv01,uv23));
    return saturate(FinishLayer(12,value).rgb);
}
float3 Emissive(float3 base,float4 value,float mode){
    if(mode<0.5)return base*value.rgb;
    if(mode<1.5)return 2*base*value.rgb;
    if(mode<2.5)return base+value.rgb;
    if(mode<3.5)return lerp(base,value.rgb,value.a);
    return base+value.rgb*team.rgb;
}
float4 Main(float4 color:COLOR0,float3 normal:TEXCOORD0,float4 uv01:TEXCOORD1,float4 uv23:TEXCOORD2,float4 tangent:TEXCOORD3,float3 eyePosition:TEXCOORD4):COLOR0{
    if(worldAmbient.w>.5&&worldSun.w>.5)clip(dot(float4(eyePosition,1),worldClip));
    float4 diff=ReadLayer(0,uv01,uv23,float4(0,0,0,1));
    if(modes.z>0.5)diff.rgb*=lerp(team.rgb,float3(1,1,1),saturate(diff.a));
    float3 rgb=diff.rgb;
    if(layer[5].x>0.5){float4 decal=ReadLayer(1,uv01,uv23,0);rgb=lerp(rgb,decal.rgb,saturate(decal.a));}
    float3 N=normalize(normal+1e-20);
    if(layer[40].x>0.5&&abs(tangent.w)>.5){
        // SC2 BC3N/DXT5N stores tangent X in alpha and Y in green.
        float2 xy=tex2D(maps[8],LayerUV(8,uv01,uv23)).ag*2-1;
        float z=sqrt(saturate(1-dot(xy,xy)));
        float2 rotation=layer[44].xy;
        xy=float2(rotation.x*xy.x+rotation.y*xy.y,-rotation.y*xy.x+rotation.x*xy.y)*sign(layer[43].xy);
        float3 T=normalize(tangent.xyz-N*dot(N,tangent.xyz)+1e-20);
        float3 B=normalize(cross(N,T))*sign(tangent.w);
        N=normalize(T*xy.x+B*xy.y+N*z);
    }
    float3 L=worldAmbient.w>.5?Unit(worldSun.xyz,float3(0,0,1)):normalize(float3(.35,.45,.82));
    float3 illumination=1;
    if(settings.y>.5){
        if(worldAmbient.w>.5){
            illumination=worldAmbient.rgb+worldDiffuse.rgb*saturate(dot(N,L));
            [unroll] for(int i=0;i<4;++i)if(i<worldSpecular.w){
                float3 delta=worldPoints[i*3].xyz-eyePosition;
                float distance=length(delta);float3 toward=Unit(delta,N);
                float3 attenuation=worldPoints[i*3+2].xyz;
                float denominator=dot(attenuation,float3(1,distance,distance*distance));
                illumination+=worldPoints[i*3+1].rgb*saturate(dot(N,toward))/max(denominator,1e-4);
            }
            illumination=max(illumination,0);
        }else illumination=.3+.7*abs(dot(N,L));
    }
    rgb*=illumination;
    if(layer[30].x>0.5)rgb*=ReadLayer(6,uv01,uv23,1).rgb;
    if(layer[35].x>0.5)rgb*=ReadLayer(7,uv01,uv23,1).rgb;
    if(settings.y>0.5&&layer[45].x>0.5){
        // Preview Blinn-Phong response. Source controls and layers are retained;
        // this is not a claim of SC2 lighting/tone-mapping pixel parity.
        float3 V=Unit(-eyePosition,N);
        float ndl=saturate(dot(N,L)),ndv=dot(N,V);
        if(ndl>0&&ndv>0){
            float3 H=Unit(L+V,N);
            float gloss=saturate(ReadLayer(10,uv01,uv23,1).a);
            float exponent=max(1e-4,vertexFlags.z*gloss);
            float4 spec=ReadLayer(9,uv01,uv23,0);
            float3 tint=modes.w>0.5?spec.aaa*layer[47].z:spec.rgb;
            float3 lightSpecular=worldAmbient.w>.5?max(worldSpecular.rgb,0):1;
            rgb+=max(tint,0)*lightSpecular*vertexFlags.w*pow(saturate(dot(N,H)),exponent)*ndl;
        }
    }
    if(layer[55].x>.5){
        float3 V=Unit(-eyePosition,N),R=reflect(-V,N);
        float4 env=ReadEnvironment(R,uv01,uv23);env.rgb*=environmentControls.y;
        float fresnel=1,type=layer[59].z;
        if(type>.5){float exponent=layer[59].w;float f=exponent==0?1:pow(saturate(1-abs(dot(N,V))),exponent);fresnel=saturate(environmentControls.z+environmentControls.w*f);if(type>1.5)fresnel=1-fresnel;}
        float3 weight=saturate(EnvironmentMask(uv01,uv23)*env.a*fresnel);
        float mode=environmentControls.x;
        if(mode<.5)rgb*=lerp(float3(1,1,1),env.rgb,weight);
        else if(mode<1.5)rgb*=lerp(float3(1,1,1),2*env.rgb,weight);
        else if(mode<2.5)rgb+=env.rgb*weight;
        else rgb=lerp(rgb,env.rgb,weight);
    }
    if(layer[10].x>0.5){float4 e=ReadLayer(2,uv01,uv23,0);e.rgb*=settings.w;rgb=Emissive(rgb,e,modes.x);}
    if(layer[15].x>0.5){float4 e=ReadLayer(3,uv01,uv23,0);e.rgb*=settings.w;rgb=Emissive(rgb,e,modes.y);}
    float alpha=ReadLayer(4,uv01,uv23,1).a*ReadLayer(5,uv01,uv23,1).a*settings.x*lerp(1,color.a,vertexFlags.y);
    clip(alpha-settings.z-1e-7); // zero opacity must not write opaque color/depth
    rgb*=lerp(float3(1,1,1),color.rgb,vertexFlags.x);
    // Emissive layers scaled by HDR gains overshoot 1 in every channel and clip to white; scale by the peak instead so the
    // colour survives (SC2 tone-maps these).
    float peak=max(rgb.r,max(rgb.g,rgb.b));if(peak>1)rgb/=peak;
    if(worldAmbient.w>.5&&worldFogColor.w>.5){
        float fog=worldFog.z*saturate((length(eyePosition)*worldFog.w-worldFog.x)*worldFog.y);
        rgb=lerp(rgb,worldFogColor.rgb,fog);
    }
    return float4(rgb,saturate(alpha));
}
#endif
