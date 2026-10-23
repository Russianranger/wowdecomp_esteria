#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../client/SurfaceShaders.generated.inc"
static void CheckAt(HRESULT r,int line){if(FAILED(r))throw std::runtime_error("D3D9 call failed at line "+std::to_string(line)+": "+std::to_string(static_cast<unsigned long>(r)));}
#define Check(expr) CheckAt((expr),__LINE__)
template<class T> struct Com{T* p=nullptr;~Com(){if(p)p->Release();}T** operator&(){return &p;}T* operator->(){return p;}};
struct Vertex{float p[3],n[3];DWORD color;float uv[8];float tangent[3],sign;};
static_assert(sizeof(Vertex)==76,"Shader fixture stride mismatch");
int main(){
 HWND window=CreateWindowExA(0,"STATIC","ASECM3 GPU validation",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
 try{
  if(!window)throw std::runtime_error("GPU fixture window creation failed: "+std::to_string(GetLastError()));
  Com<IDirect3D9> api;api.p=Direct3DCreate9(D3D_SDK_VERSION);if(!api.p)throw std::runtime_error("D3D9 unavailable");
  D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=16;pp.BackBufferHeight=16;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
  Com<IDirect3DDevice9> device;Check(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device));
  Com<IDirect3DVertexShader9> vs;Com<IDirect3DPixelShader9> ps;
  Check(device->CreateVertexShader(reinterpret_cast<const DWORD*>(surfaceVS),&vs));Check(device->CreatePixelShader(reinterpret_cast<const DWORD*>(surfacePS),&ps));
  D3DVERTEXELEMENT9 declaration[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_D3DCOLOR,0,D3DDECLUSAGE_COLOR,0},{0,28,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,36,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,1},{0,44,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,2},{0,52,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,3},{0,60,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_TEXCOORD,4},{0,72,D3DDECLTYPE_FLOAT1,0,D3DDECLUSAGE_TEXCOORD,5},D3DDECL_END()};
  Com<IDirect3DVertexDeclaration9> decl;Check(device->CreateVertexDeclaration(declaration,&decl));
  Check(device->SetVertexDeclaration(decl.p));Check(device->SetVertexShader(vs.p));Check(device->SetPixelShader(ps.p));
  Check(device->SetRenderState(D3DRS_ZENABLE,FALSE));Check(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));Check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));Check(device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));Check(device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));
  float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};Check(device->SetVertexShaderConstantF(0,identity,4));
  Com<IDirect3DSurface9> target,capture;Check(device->GetRenderTarget(0,&target));Check(device->CreateOffscreenPlainSurface(16,16,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&capture,nullptr));
  Com<IDirect3DTexture9> texture;Check(device->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr));
  D3DLOCKED_RECT lock;Check(texture->LockRect(0,&lock,nullptr,0));const DWORD pixels[4]={0xffff0000,0xff00ff00,0xff0000ff,0xffffffff};for(int y=0;y<2;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,pixels+y*2,8);Check(texture->UnlockRect(0));
  for(unsigned i=0;i<13;++i){Check(device->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT));Check(device->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT));Check(device->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));Check(device->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
  Vertex vertices[6]={};const float points[12]={-1,-1,-1,1,1,1,-1,-1,1,1,1,-1};for(int i=0;i<6;++i){vertices[i].p[0]=points[i*2];vertices[i].p[1]=points[i*2+1];vertices[i].n[2]=1;vertices[i].tangent[0]=1;vertices[i].sign=1;vertices[i].color=0xffffffff;const float uv[]={.25f,.25f,.75f,.25f,.25f,.75f,.75f,.75f};std::memcpy(vertices[i].uv,uv,sizeof(uv));}
  unsigned cases=0;float maxError=0;
  auto run=[&](const char* name,const std::array<float,372>& constants,std::array<int,4> expected){
   for(unsigned indexed=0;indexed<2;++indexed){
   const Vertex shared[]={vertices[0],vertices[1],vertices[2],vertices[5]};const uint16_t sharedIndices[]={0,1,2,0,2,3};
   if(std::memcmp(&vertices[0],&vertices[3],sizeof(Vertex))||std::memcmp(&vertices[2],&vertices[4],sizeof(Vertex)))throw std::runtime_error("Indexed fixture contains inconsistent shared attributes");
   Check(device->SetPixelShaderConstantF(0,constants.data(),93));Check(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xffff00ff,1,0));Check(device->BeginScene());if(indexed)Check(device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST,0,4,2,sharedIndices,D3DFMT_INDEX16,shared,sizeof(Vertex)));else Check(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,vertices,sizeof(Vertex)));Check(device->EndScene());Check(device->GetRenderTargetData(target.p,capture.p));Check(capture->LockRect(&lock,nullptr,D3DLOCK_READONLY));DWORD pixel=*reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+8*lock.Pitch+8*4);Check(capture->UnlockRect());const int got[]={int((pixel>>16)&255),int((pixel>>8)&255),int(pixel&255),int(pixel>>24)};
   for(unsigned i=0;i<4;++i){const int error=std::abs(got[i]-expected[i]);maxError=std::max(maxError,float(error));if(error>2)throw std::runtime_error(std::string(name)+" channel "+std::to_string(i)+" got "+std::to_string(got[i])+" expected "+std::to_string(expected[i]));}++cases;
   }
  };
  auto initial=[](){std::array<float,372> c{};c[0]=1;c[3]=1;c[8]=.2f;c[9]=.4f;c[10]=.8f;return c;};
  auto layer=[](std::array<float,372>& c,unsigned slot,std::array<float,4> color,int channels=1){const unsigned b=16+slot*20;c[b]=1;c[b+2]=float(channels);for(unsigned i=0;i<4;++i)c[b+4+i]=color[i];c[b+8]=1;c[b+10]=1;c[b+12]=c[b+13]=c[b+16]=1;};
  auto c=initial();layer(c,0,{.2f,.4f,.6f,1});run("constant",c,{51,102,153,255});
  c=initial();layer(c,0,{.2f,.4f,.6f,1});layer(c,1,{1,0,0,.5f});run("decal",c,{153,51,77,255});
  c=initial();layer(c,0,{.2f,.4f,.6f,1});layer(c,2,{.1f,.2f,.1f,1});c[4]=2;run("emissive-add",c,{77,153,179,255});
  c=initial();layer(c,0,{.2f,.4f,.6f,1});layer(c,4,{1,1,1,.5f});layer(c,5,{1,1,1,.5f});run("two-masks",c,{51,102,153,64});c[2]=.5f;run("cutout",c,{255,0,255,255});
  c=initial();layer(c,0,{.2f,.4f,.6f,1});layer(c,4,{1,1,1,0});run("zero-opacity",c,{255,0,255,255});
  c=initial();layer(c,0,{.2f,.4f,.6f,1});layer(c,4,{1,1,1,.25f});c[2]=.25f;run("threshold-equality",c,{255,0,255,255});
  for(unsigned channel=0;channel<6;++channel){c=initial();layer(c,0,{.2f,.4f,.6f,.8f},channel);const int scalar=channel==2?204:channel==3?51:channel==4?102:153;run("channels",c,channel<2?std::array<int,4>{51,102,153,255}:std::array<int,4>{scalar,scalar,scalar,255});}
  c=initial();layer(c,0,{.5f,.5f,.5f,0});c[6]=1;run("team",c,{26,51,102,255});
  Check(device->SetTexture(0,texture.p));for(unsigned uv=0;uv<4;++uv){c=initial();layer(c,0,{1,1,1,1});c[17]=1;c[19]=float(uv);const std::array<int,4> expected[]={{{255,0,0,255}},{{0,255,0,255}},{{0,0,255,255}},{{255,255,255,255}}};run("source-uv",c,expected[uv]);}
  for(auto& v:vertices)v.color=D3DCOLOR_ARGB(128,64,128,192);
  c=initial();layer(c,0,{1,1,1,1});run("vertex-flags-off",c,{255,255,255,255});
  c[12]=1;run("vertex-rgb",c,{64,128,192,255});c[13]=1;run("vertex-rgba",c,{64,128,192,128});c[12]=0;run("vertex-alpha-only",c,{255,255,255,128});
  for(auto& v:vertices)v.color=0xffffffff;
  Com<IDirect3DTexture9> normalMap;Check(device->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&normalMap,nullptr));
  Check(normalMap->LockRect(0,&lock,nullptr,0));*static_cast<DWORD*>(lock.pBits)=D3DCOLOR_ARGB(192,0,220,0);Check(normalMap->UnlockRect(0));Check(device->SetTexture(8,normalMap.p));
  for(unsigned mode=0;mode<6;++mode){
   c=initial();layer(c,0,{1,1,1,1});layer(c,8,{1,1,1,1});c[177]=1;c[1]=1;
   const float sign=mode==1?-1.0f:mode==5?0.0f:1.0f;const float tangentX=mode==2?-1.0f:1.0f;
   for(auto& v:vertices){v.sign=sign;v.tangent[0]=tangentX;}
   if(mode==3){c[192]=0;c[193]=1;} // rotate normal UV by pi/2
   if(mode==4)c[188]=-1; // mirrored normal U
   double x=192/127.5-1,y=220/127.5-1,z=std::sqrt(std::max(0.0,1-x*x-y*y));
   if(mode==3){const double old=x;x=y;y=-old;}
   if(mode==4)x=-x;
   if(sign==0){x=y=0;z=1;}else{x*=tangentX;y*=tangentX*sign;}
   const double length=std::sqrt(x*x+y*y+z*z);x/=length;y/=length;z/=length;
   const double light=std::sqrt(.35*.35+.45*.45+.82*.82);const int shade=int(std::lround(255*(.3+.7*std::abs((x*.35+y*.45+z*.82)/light))));
   run("normal-basis",c,{shade,shade,shade,255});
  }
  // A distant camera-relative plane keeps the fixture view ray known to within
  // 1/1000 while projection still covers the same readback target.
  identity[10]=0;Check(device->SetVertexShaderConstantF(0,identity,4));
  for(unsigned mode=0;mode<12;++mode){
   const bool side=mode==8,lit=mode!=6,scalar=mode==4||mode==10;
   for(unsigned i=0;i<6;++i){vertices[i].p[0]=points[i*2]+(side?1000.0f:0.0f);vertices[i].p[2]=mode==11?0.0f:-1000.0f;vertices[i].sign=1;vertices[i].tangent[0]=1;}
   identity[12]=side?-1000.0f:0.0f;Check(device->SetVertexShaderConstantF(0,identity,4));
   c=initial();layer(c,0,{.1f,.1f,.1f,1});layer(c,9,{.2f,.1f,.05f,.4f},scalar?0:1);
   const float exponent=mode==1?32.0f:mode==2?512.0f:8.0f,gloss=mode==5?.25f:mode==7?0.0f:1.0f,hdr=mode==3?2.0f:1.0f,brightness=mode==10?.5f:1.0f;
   layer(c,10,{1,1,1,gloss});c[1]=lit?1.0f:0.0f;c[7]=scalar?1.0f:0.0f;c[14]=exponent;c[15]=hdr;c[16+9*20+10]=brightness;
   if(mode==9)c[16+9*20]=0;
   const double ln=std::sqrt(.35*.35+.45*.45+.82*.82),lx=.35/ln,ly=.45/ln,lz=.82/ln;
   const double vx=side?-1/std::sqrt(2.0):0,vz=side?1/std::sqrt(2.0):1;
   const double hn=std::sqrt((lx+vx)*(lx+vx)+ly*ly+(lz+vz)*(lz+vz)),ndh=(lz+vz)/hn;
   const double response=lit&&mode!=9?hdr*std::pow(ndh,std::max(1e-4,double(exponent)*gloss))*lz:0;
   const double base=.1*(lit?.3+.7*lz:1);const double tint[]={scalar?.4:.2,scalar?.4:.1,scalar?.4:.05};
   std::array<int,4> expected{0,0,0,255};for(unsigned i=0;i<3;++i)expected[i]=int(std::lround(std::clamp(base+tint[i]*brightness*response,0.0,1.0)*255));
   run("specular-gloss",c,expected);
  }
  for(unsigned i=0;i<6;++i){vertices[i].p[0]=points[i*2];vertices[i].p[2]=-1000;vertices[i].n[0]=vertices[i].n[1]=0;vertices[i].n[2]=1;}
  identity[12]=0;Check(device->SetVertexShaderConstantF(0,identity,4));
  Check(device->SetTexture(11,nullptr));Check(device->SetTexture(12,nullptr));
  for(unsigned mode=0;mode<9;++mode){
   c=initial();layer(c,0,{.1f,.1f,.1f,1});layer(c,11,{.2f,.4f,.6f,.5f});c[276]=float(mode<4?mode:2);c[277]=mode==8?2.0f:1.0f;
   double mask[3]={1,1,1};double fresnel=1;
   if(mode==4){layer(c,12,{1,.25f,1,1},4);for(auto& v:mask)v=.25;}
   if(mode==5){layer(c,12,{.25f,.5f,.75f,1},0);mask[0]=.25;mask[1]=.5;mask[2]=.75;}
   if(mode==6){c[254]=1;c[255]=4;c[279]=1;fresnel=0;}
   if(mode==7){c[254]=2;c[255]=4;c[279]=1;fresnel=1;}
   const double color[]={.2,.4,.6};std::array<int,4> expected{0,0,0,255};
   for(unsigned i=0;i<3;++i){const double w=mask[i]*.5*fresnel,env=color[i]*c[277];double rgb=.1;
    if(mode==0)rgb*=1+(env-1)*w;else if(mode==1)rgb*=1+(2*env-1)*w;else if(mode==3)rgb=rgb*(1-w)+env*w;else rgb+=env*w;
    expected[i]=int(std::lround(std::clamp(rgb,0.0,1.0)*255));}
   run("environment-blend-mask-fresnel",c,expected);
  }
  // Distinct cube-face colors verify sampler target and reflection direction.
  c=initial();layer(c,0,{.8f,.4f,.2f,1});layer(c,7,{.25f,.25f,.25f,1});
  c[24]=2;c[25]=.1f;c[27]=1;
  run("MADD-channel-clamp-before-AO",c,{64,57,32,255});
  Com<IDirect3DPixelShader9> cubePS;Check(device->CreatePixelShader(reinterpret_cast<const DWORD*>(surfacePSCube),&cubePS));
  Com<IDirect3DCubeTexture9> cube;Check(device->CreateCubeTexture(2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&cube,nullptr));
  const DWORD faceColors[]={0xffff0000,0xff00ff00,0xff0000ff,0xffffff00,0xffff00ff,0xff00ffff};
  for(unsigned face=0;face<6;++face){Check(cube->LockRect(D3DCUBEMAP_FACES(face),0,&lock,nullptr,0));for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x)*reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch+x*4)=faceColors[face];Check(cube->UnlockRect(D3DCUBEMAP_FACES(face),0));}
  Check(device->SetPixelShader(cubePS.p));Check(device->SetTexture(11,cube.p));
  for(unsigned face=0;face<6;++face){
   const float q=float(1/std::sqrt(2.0));const float normals[][3]={{q,0,q},{-q,0,q},{0,q,q},{0,-q,q},{0,0,1},{1,0,0}};
   for(auto& v:vertices)std::memcpy(v.n,normals[face],sizeof(v.n));
   c=initial();layer(c,11,{1,1,1,1},0);c[237]=1;c[239]=2;c[276]=2;c[277]=1;
   const DWORD wanted=faceColors[face];run("environment-cube-face",c,{int(wanted>>16&255),int(wanted>>8&255),int(wanted&255),255});
  }
  Check(device->SetPixelShader(ps.p));Check(device->SetTexture(11,texture.p));
  for(unsigned quadrant=0;quadrant<4;++quadrant){
   const float x=quadrant&1?.3f:-.3f,y=quadrant&2?-.4f:.4f,z=float(std::sqrt(.75));for(auto& v:vertices){v.n[0]=x;v.n[1]=y;v.n[2]=z;}
   c=initial();layer(c,11,{1,1,1,1},0);c[237]=1;c[239]=3;c[276]=2;c[277]=1;
   c[280]=c[285]=c[295]=1;c[290]=-1; // D3D camera forward axis
   const DWORD wanted=pixels[quadrant];run("environment-sphere-quadrant",c,{int(wanted>>16&255),int(wanted>>8&255),int(wanted&255),255});
  }
  Check(device->SetPixelShader(ps.p));
  for(unsigned i=0;i<6;++i){vertices[i].p[0]=points[i*2];vertices[i].p[1]=points[i*2+1];vertices[i].p[2]=-1000;vertices[i].n[0]=vertices[i].n[1]=0;vertices[i].n[2]=1;vertices[i].color=0xffffffff;}
  identity[12]=identity[13]=identity[10]=0;Check(device->SetVertexShaderConstantF(0,identity,4));
  auto world=[&](){auto value=initial();layer(value,0,{1,1,1,1});value[1]=1;value[299]=1;value[310]=1;return value;};
  c=world();c[296]=.2f;c[297]=.4f;c[298]=.6f;run("world-ambient",c,{51,102,153,255});
  c=world();c[296]=.1f;c[297]=.2f;c[298]=.3f;c[300]=.5f;c[301]=.25f;run("world-sun-front",c,{153,115,77,255});
  c[310]=-1;run("world-sun-back",c,{26,51,77,255});
  c=world();layer(c,0,{0,0,0,1});layer(c,9,{.5f,.25f,.1f,1});c[14]=8;c[15]=1;c[304]=.2f;c[305]=.4f;c[306]=.6f;
  run("world-specular-color",c,{26,26,15,255});
  c[304]=c[305]=c[306]=0;run("world-specular-zero",c,{0,0,0,255});
  c[1]=0;c[304]=c[305]=c[306]=1;run("world-unshaded-no-specular",c,{0,0,0,255});
  c=world();c[307]=1;c[322]=0;c[324]=.4f;c[325]=.2f;c[326]=.1f;c[328]=1;run("world-point",c,{102,51,26,255});
  c[328]=2;run("world-point-attenuation",c,{51,26,13,255});
  c[307]=0;run("world-point-count-zero",c,{0,0,0,255});
  c=world();c[307]=4;for(unsigned i=0;i<4;++i){c[324+i*12]=.1f;c[328+i*12]=1;}run("world-four-points",c,{102,0,0,255});
  c=world();c[1]=0;run("world-unshaded",c,{255,255,255,255});
  c[0]=.5f;run("world-object-opacity",c,{255,255,255,128});
  c=world();c[1]=0;c[312]=500;c[313]=.001f;c[314]=1;c[315]=1;c[316]=.2f;c[317]=.4f;c[318]=.6f;c[319]=1;
  run("world-fog-midpoint",c,{153,179,204,255});
  c[315]=2;run("world-fog-distance-scale",c,{51,102,153,255});c[315]=1;
  c[314]=.5f;run("world-fog-density",c,{204,217,230,255});
  c[319]=0;run("world-unfogged",c,{255,255,255,255});
  c[319]=1;c[314]=1;c[312]=1500;run("world-fog-near",c,{255,255,255,255});
  c[312]=0;c[313]=.002f;run("world-fog-far",c,{51,102,153,255});
  c[312]=500;c[313]=.001f;c[316]=c[317]=c[318]=0;run("world-additive-fog-neutral",c,{128,128,128,255});
  c[316]=c[317]=c[318]=1;run("world-modulate-fog-neutral",c,{255,255,255,255});
  c[316]=c[317]=c[318]=.5f;run("world-modulate2x-fog-neutral",c,{191,191,191,255});
  c=world();c[1]=0;c[311]=1;c[370]=1;c[371]=999;run("world-water-clip-reject",c,{255,0,255,255});
  c[371]=1001;run("world-water-clip-accept",c,{255,255,255,255});
  c[370]=-1;c[371]=-999;run("world-water-clip-reversed",c,{255,255,255,255});
  c[370]=1;c[371]=999;c[311]=0;run("world-water-clip-disabled",c,{255,255,255,255});
  std::cout<<"{\"ok\":true,\"cases\":"<<cases<<",\"maxByteError\":"<<maxError<<",\"scope\":\"D3D9 HAL shader pixels on preview and world-environment fixtures; not client source-model visual acceptance\"}\n";
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';DestroyWindow(window);return 1;}
 DestroyWindow(window);return 0;
}
