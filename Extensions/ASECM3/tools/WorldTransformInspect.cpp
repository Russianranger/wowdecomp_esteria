#include "GxGeometryTransform.h"
#include "WorldPlacement.h"
#include "EmoteRequest.h"
#include "ASECM3/Picking.h"
#include <cmath>
#include <iostream>
#include <limits>
using namespace ASECM3::Client;
void Near(C3Vector p,C3Vector expected){if(std::abs(p.x-expected.x)>1e-5||std::abs(p.y-expected.y)>1e-5||std::abs(p.z-expected.z)>1e-5)throw std::runtime_error("World transform numeric mismatch");}
int main(){try{
 C44Matrix m;m.Identity();m.a0=2;m.b1=3;m.c2=4;m.d0=10;m.d1=-2;m.d2=5;GxGeometryTransform t(m);
 Near(t.Position(C3Vector(1,2,3)),C3Vector(12,4,17));
 const float length=std::sqrt(.25f+1.0f/9+1.0f/16);Near(t.Normal(C3Vector(1,1,1)),C3Vector(.5f/length,(1.0f/3)/length,.25f/length));
 Near(t.Tangent(C3Vector(1,0,0),C3Vector(0,0,1)),C3Vector(1,0,0));if(t.Mirrored())throw std::runtime_error("Positive determinant misclassified");
 m.a0=-2;GxGeometryTransform mirror(m);if(!mirror.Mirrored())throw std::runtime_error("Reflection was not classified");Near(mirror.Position(C3Vector(1,2,3)),C3Vector(8,4,17));
 C44Matrix rotated;rotated.Identity();rotated.a0=0;rotated.a1=2;rotated.b0=-3;rotated.b1=0;rotated.c2=4;rotated.d0=10;rotated.d1=-2;rotated.d2=5;
 GxGeometryTransform rotation(rotated);Near(rotation.Position(C3Vector(1,2,3)),C3Vector(4,0,17));Near(rotation.Normal(C3Vector(1,0,0)),C3Vector(0,1,0));Near(rotation.Tangent(C3Vector(0,1,0),C3Vector(0,1,0)),C3Vector(-1,0,0));
 const CAaBox source(C3Vector(-1,-2,-3),C3Vector(2,4,6));const auto box=rotation.Bounds(source);
 Near(box.b,C3Vector(-2,-4,-7));Near(box.t,C3Vector(16,2,29));
 const auto sphere=GxGeometryTransform::EnclosingSphere(box);Near(sphere.c,C3Vector(7,-1,11));
 for(unsigned i=0;i<8;++i){const auto p=rotation.Position(C3Vector(i&1?source.t.x:source.b.x,i&2?source.t.y:source.b.y,i&4?source.t.z:source.b.z));
  if(p.x<box.b.x||p.y<box.b.y||p.z<box.b.z||p.x>box.t.x||p.y>box.t.y||p.z>box.t.z)throw std::runtime_error("M3 world box excludes a corner");
  const double x=double(p.x)-sphere.c.x,y=double(p.y)-sphere.c.y,z=double(p.z)-sphere.c.z;
  if(x*x+y*y+z*z>double(sphere.r)*sphere.r)throw std::runtime_error("M3 world sphere excludes a corner");}
 unsigned invalidBounds=0;
 try{rotation.Bounds(CAaBox(C3Vector(1,0,0),C3Vector(0,1,1)));}catch(const std::exception&){++invalidBounds;}
 try{GxGeometryTransform::EnclosingSphere(CAaBox(C3Vector(0,0,0),C3Vector(std::numeric_limits<float>::infinity(),1,1)));}catch(const std::exception&){++invalidBounds;}
 if(invalidBounds!=2)throw std::runtime_error("Invalid bounds accepted");
 unsigned placements=0;
 const auto placement=[&](bool deferred,float alpha,bool above,bool below,float d,bool split,bool cameraBelow,unsigned mask,bool clip){const auto p=PlaceWorldPacket(deferred,alpha,above,below,d,1,split,cameraBelow);if(p.passes!=mask||p.clip!=clip)throw std::runtime_error("M3 world phase placement mismatch");++placements;};
 placement(false,1,true,true,0,true,false,1,false);placement(true,1,true,true,0,true,false,6,true);
 placement(true,1,true,true,2,true,false,2,false);placement(true,1,true,true,-2,true,false,4,false);
 placement(true,1,true,true,0,false,false,2,false);placement(true,1,true,true,0,false,true,4,false);
 placement(false,.5f,true,false,0,true,false,2,false);placement(false,0,true,true,0,true,false,0,false);
 placement(true,1,false,false,0,true,false,0,false);placement(true,1,true,true,1,true,false,6,true);
 EmoteRequest emote;emote.Set(69);
 if(emote.Choose(0,false,100,6334)!=69||emote.Choose(0,false,20000,6334)!=69)throw std::runtime_error("Idle fallback erased persistent Dance");
 if(emote.Choose(5,true,20010,6334)!=5||emote.Action()!=-1)throw std::runtime_error("Movement did not cancel Dance");
 emote.Set(74);if(emote.Choose(0,false,30000,500)!=74||emote.Choose(0,false,30500,500)!=0)throw std::runtime_error("One-shot emote clock failed");
 emote.Set(69);emote.Clear();if(emote.Choose(0,false,0,6334)!=0)throw std::runtime_error("Explicit Dance stop failed");
 unsigned rejected=0;for(unsigned kind=0;kind<3;++kind){C44Matrix bad;bad.Identity();if(kind==0)bad.a0=0;if(kind==1)bad.b1=std::numeric_limits<float>::quiet_NaN();if(kind==2)bad.a3=.5f;try{GxGeometryTransform invalid(bad);}catch(const std::exception&){++rejected;}}
 if(rejected!=3)throw std::runtime_error("Invalid transform was accepted");
 ASECM3::SceneFrame pickFrame;ASECM3::SceneRegion pickRegion;pickRegion.indices={0,1,2};
 for(const auto p:{ASECM3::Vec3{-1,-1,2},ASECM3::Vec3{1,-1,2},ASECM3::Vec3{0,1,2}}){ASECM3::SceneVertex v;v.position=p;pickRegion.vertices.push_back(v);}
 pickFrame.regions.push_back(pickRegion);ASECM3::PickingIndex pickIndex;pickIndex.Refit(pickFrame);
 unsigned rayTransforms=0;
 for(const auto& transform:{m,rotated}){
  const auto origin=C3Vector(0,0,0)*transform;
  const auto direction=TransformDirection(C3Vector(0,0,1),transform);
  const auto inverse=transform.Inverse(transform.Determinant());
  const auto localOrigin=origin*inverse,localDirection=TransformDirection(direction,inverse);
  ASECM3::PickHit hit;const ASECM3::PickRay ray{{localOrigin.x,localOrigin.y,localOrigin.z},{localDirection.x,localDirection.y,localDirection.z},10};
  if(!pickIndex.Intersect(pickFrame,{true},ray,hit)||std::abs(hit.distance-2)>1e-5)throw std::runtime_error("Affine ray parameter changed under inverse transform");
  ++rayTransforms;
 }
 std::cout<<"{\"rayTransforms\":"<<rayTransforms<<",\"ok\":true,\"scope\":\"Nonuniform/reflected and rotated affine picking rays\"}\n";
 std::cout<<"{\"ok\":true,\"rejectedTransforms\":3,\"rejectedBounds\":2,\"boundCorners\":8,\"worldPlacements\":"<<placements<<",\"scope\":\"Affine bounds and native phase placement math; no live world culling, sorting or avatar proof\"}\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
