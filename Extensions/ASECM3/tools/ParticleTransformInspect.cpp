#include "ASECM3/ParticleTransformInterval.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <limits>
using namespace ASECM3;
namespace {
Matrix Actor(double time,const Vec3& axis,double base,bool reflected){const double length=std::sqrt(double(axis.x)*axis.x+double(axis.y)*axis.y+double(axis.z)*axis.z),angle=base+time*.0003;const float s=float((1+time*.0001)*(reflected?-1:1));return Compose({float(3+time*.002),float(-2+time*.0005),float(8-time*.001),0},{float(axis.x/length*std::sin(angle)),float(axis.y/length*std::sin(angle)),float(axis.z/length*std::sin(angle)),float(std::cos(angle))},{s,s,s,0});}
double Difference(const Matrix& a,const Matrix& b){double error=0;for(unsigned i=0;i<16;++i)error=std::max(error,std::abs(a.values[i]-b.values[i]));return error;}
template<class F>void Reject(F&& action,size_t& count){bool rejected=false;try{action();}catch(const Error&){rejected=true;}if(!rejected)throw Error("Invalid actor history input was accepted");++count;}
}
int main(){try{
 size_t samples=0,rejections=0;double maximumError=0,partitionError=0;
 for(const Vec3 axis:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1},Vec3{.2f,.3f,.4f}})for(double base:{0.0,1.4,2.0})for(bool reflected:{false,true}){
  const auto first=Actor(0,axis,base,reflected),middle=Actor(500,axis,base,reflected),last=Actor(1000,axis,base,reflected);
  const ParticleTransformInterval whole(0,1000,first,last),left(0,500,first,middle),right(500,1000,middle,last);
  if(Difference(whole.At(0),first)||Difference(whole.At(1000),last))throw Error("Actor history changed exact observed endpoints");
  for(double time:{0.0,100.0,233.0,500.0,731.0,999.0,1000.0}){const auto got=whole.At(time);maximumError=std::max(maximumError,Difference(got,Actor(time,axis,base,reflected)));partitionError=std::max(partitionError,Difference(got,time<=500?left.At(time):right.At(time)));ParticleUniformScale(got);++samples;}
 }
 if(maximumError>3e-6||partitionError>3e-6)throw Error("Actor transform interval differs from analytic translation/rotation/scale");
 const auto identity=Matrix::Identity();auto invalid=identity;invalid.At(0,0)=2;Reject([&]{ParticleTransformInterval(0,1,identity,invalid);},rejections);
 invalid=identity;invalid.At(0,1)=.5;Reject([&]{ParticleTransformInterval(0,1,identity,invalid);},rejections);
 invalid=identity;invalid.At(0,0)=0;Reject([&]{ParticleTransformInterval(0,1,identity,invalid);},rejections);
 invalid=identity;invalid.At(0,3)=std::numeric_limits<double>::infinity();Reject([&]{ParticleTransformInterval(0,1,identity,invalid);},rejections);
 invalid=Compose({0,0,0,0},{0,0,0,1},{-1,-1,-1,0});Reject([&]{ParticleTransformInterval(0,1,identity,invalid);},rejections);
 Reject([&]{ParticleTransformInterval(1,0,identity,identity);},rejections);
 const ParticleTransformInterval interval(0,1,identity,identity);Reject([&]{interval.At(-1);},rejections);Reject([&]{interval.At(2);},rejections);Reject([&]{interval.At(std::numeric_limits<double>::quiet_NaN());},rejections);
 const auto moved=Compose({3,4,5,0},{0,0,0,1},{1,1,1,0});if(Difference(ParticleTransformInterval(0,0,identity,moved).At(0),moved))throw Error("Zero-duration observation did not retain newest transform");
 std::cout<<std::setprecision(12)<<"{\"ok\":true,\"samples\":"<<samples<<",\"maximumAnalyticError\":"<<maximumError<<",\"maximumPartitionError\":"<<partitionError<<",\"rejections\":"<<rejections<<",\"scope\":\"Uniform actor TRS interval math; not observed game motion, physics history or source-engine parity\"}\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
