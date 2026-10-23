#include "ASECM3/Pose.h"
#include <cmath>
#include <iostream>
int main(){
 try{
  unsigned cases=0;double error=0;
  for(int mode=0;mode<6;++mode){
   ASECM3::ModelData model;ASECM3::Vertex vertex;vertex.normal={0,0,1};vertex.tangent={1,0,0};vertex.hasTangent=vertex.hasTangentSign=true;vertex.weights[0]=255;
   const bool tilted=mode==3||mode==4,mirror=mode==1||mode==4;
   if(tilted){const float n=float(1/std::sqrt(3.0)),t=float(1/std::sqrt(2.0));vertex.normal={n,n,n};vertex.tangent={t,-t,0};}
   if(mode==5)vertex.tangentSign=0;
   model.vertices.push_back(vertex);model.skeleton.emplace_back();model.bonePalette.push_back(0);ASECM3::Region region;region.vertexCount=region.boneLookupCount=1;ASECM3::Division division;division.regions.push_back(region);model.divisions.push_back(division);
   ASECM3::Pose pose;auto skin=ASECM3::Matrix::Identity(),normal=ASECM3::Matrix::Identity();
   skin.At(0,0)=mirror?-2:2;skin.At(1,1)=3;skin.At(2,2)=4;
   normal.At(0,0)=mirror?-.5:.5;normal.At(1,1)=1.0/3;normal.At(2,2)=.25;
   if(mode==2){skin=ASECM3::Matrix{};skin.At(3,3)=1;normal=ASECM3::Matrix{};}
   pose.skin.push_back(skin);pose.normal.push_back(normal);
   const auto result=ASECM3::SkinModel(model,pose).at(0);
   const double tl=std::sqrt(13.0),nl=std::sqrt(61.0);
   const ASECM3::Vec3 wantedT=mode==2?ASECM3::Vec3{}:tilted?ASECM3::Vec3{float((mirror?-2:2)/tl),float(-3/tl),0}:ASECM3::Vec3{mirror?-1.0f:1.0f,0,0};
   const ASECM3::Vec3 wantedN=mode==2?ASECM3::Vec3{}:tilted?ASECM3::Vec3{float((mirror?-6:6)/nl),float(4/nl),float(3/nl)}:ASECM3::Vec3{0,0,1};
   const auto t=result.tangents.at(0),n=result.normals.at(0);const double values[]={std::abs(t.x-wantedT.x),std::abs(t.y-wantedT.y),std::abs(t.z-wantedT.z),std::abs(n.x-wantedN.x),std::abs(n.y-wantedN.y),std::abs(n.z-wantedN.z)};
   for(auto e:values){error=std::max(error,e);if(e>1e-5)throw ASECM3::Error("Analytical basis mismatch");}
   const float sign=mode==2?0:mirror||mode==5?-1.0f:1.0f;
   if(result.tangentSigns.at(0)!=sign)throw ASECM3::Error("Analytical reflected handedness mismatch");
   ++cases;
  }
  std::cout<<"{\"ok\":true,\"cases\":"<<cases<<",\"maximumError\":"<<error<<",\"scope\":\"Analytical nonuniform/reflected/zero-scale/source-sign skinning fixtures\"}\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
