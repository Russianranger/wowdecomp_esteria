#include "ASECM3/RigidMassProperties.h"
#include "btBulletDynamicsCommon.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
using namespace ASECM3;
namespace {
btTransform Transform(const Matrix& m){btMatrix3x3 basis;for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)basis[r][c]=btScalar(m.At(r,c));return btTransform(basis,btVector3(btScalar(m.At(0,3)),btScalar(m.At(1,3)),btScalar(m.At(2,3))));}
template<class F>void Reject(F&& f,unsigned& count){bool rejected=false;try{f();}catch(const Error&){rejected=true;}if(!rejected)throw Error("Invalid compound mass input accepted");++count;}
}
int main(){try{
 std::cout<<std::setprecision(12)<<"{\"ok\":true,\"cases\":[";bool comma=false;double geometryError=0;
 for(bool rotated:{false,true}){
  btBoxShape box(btVector3(1,2,3));btCompoundShape compound;
  const auto frame=rotated?Compose({10,-3,5,0},{.2f,.3f,.4f,.8f},{1,1,1,0}):Matrix::Identity();
  const auto placement=Transform(frame);btTransform originals[2];
  for(unsigned i=0;i<2;++i){btTransform local;local.setIdentity();local.setOrigin(btVector3(i?2:-2,0,0));originals[i]=placement*local;compound.addChildShape(originals[i],&box);}
  const auto properties=RecenterRigidCompound(compound,{2,6});const auto principal=Transform(properties.centerInBone);
  for(unsigned i=0;i<2;++i)for(int x:{-1,1})for(int y:{-1,1})for(int z:{-1,1}){const btVector3 point(x,y*2,z*3);const double error=double((originals[i]*point-principal*compound.getChildTransform(i)*point).length());geometryError=std::max(geometryError,error);}
  if(comma)std::cout<<',';comma=true;std::cout<<"{\"rotated\":"<<(rotated?"true":"false")<<",\"mass\":"<<properties.mass<<",\"centerInBone\":[";for(unsigned i=0;i<16;++i){if(i)std::cout<<',';std::cout<<properties.centerInBone.values[i];}std::cout<<"],\"inertia\":["<<properties.inertia.x<<','<<properties.inertia.y<<','<<properties.inertia.z<<"],\"placement\":[";for(unsigned i=0;i<16;++i){if(i)std::cout<<',';std::cout<<frame.values[i];}std::cout<<"]}";
 }
 if(geometryError>1e-5)throw Error("Recentering moved authored collision geometry");
 btBoxShape box(btVector3(1,1,1));btCompoundShape compound;btTransform identity;identity.setIdentity();compound.addChildShape(identity,&box);unsigned rejected=0;
 Reject([&]{RecenterRigidCompound(compound,{});},rejected);Reject([&]{RecenterRigidCompound(compound,{0});},rejected);Reject([&]{RecenterRigidCompound(compound,{-1});},rejected);Reject([&]{RecenterRigidCompound(compound,{1,2});},rejected);Reject([&]{RecenterRigidCompound(compound,{1e10});},rejected);
 std::cout<<"],\"maximumGeometryError\":"<<geometryError<<",\"rejections\":"<<rejected<<",\"scope\":\"Compound COM/inertia and authored shape preservation; not original-engine dynamics or native visual proof\"}\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
