#include "ASECM3/ConvexPhysics.h"
#include "LinearMath/btConvexHullComputer.h"
#include "LinearMath/btMatrix3x3.h"
#include <algorithm>
#include <cmath>
namespace ASECM3 {
namespace {
struct Point {double v[3];};
Point Add(Point a,Point b){for(unsigned i=0;i<3;++i)a.v[i]+=b.v[i];return a;}
Point Sub(Point a,Point b){for(unsigned i=0;i<3;++i)a.v[i]-=b.v[i];return a;}
Point Scale(Point a,double s){for(auto& v:a.v)v*=s;return a;}
Point Cross(Point a,Point b){return {{a.v[1]*b.v[2]-a.v[2]*b.v[1],a.v[2]*b.v[0]-a.v[0]*b.v[2],a.v[0]*b.v[1]-a.v[1]*b.v[0]}};}
double Dot(Point a,Point b){return a.v[0]*b.v[0]+a.v[1]*b.v[1]+a.v[2]*b.v[2];}
bool Finite(Vec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
}
ConvexPhysicsMesh BuildConvexPhysics(const PhysicsShape& source,const Vec3& boneScale){
    if(source.kind!=4)throw Error("Convex physics requires explicit PHSH shape 4");
    if(source.vertices.size()<4 || source.vertices.size()>8192)throw Error("Convex physics requires 4..8192 source vertices");
    if(!Finite(boneScale)||boneScale.x<=0||boneScale.y<=0||boneScale.z<=0)throw Error("Convex physics bone scale must be finite and positive");
    for(double value:source.local.values)if(!std::isfinite(value))throw Error("Convex physics local frame is not finite");
    if(std::abs(source.local.At(3,0))+std::abs(source.local.At(3,1))+std::abs(source.local.At(3,2))+std::abs(source.local.At(3,3)-1)>1e-9)throw Error("Convex physics local frame is not affine");
    const double scales[3]={boneScale.x,boneScale.y,boneScale.z};std::vector<Point> points;points.reserve(source.vertices.size());
    for(const auto& vertex:source.vertices){
        if(!Finite(vertex))throw Error("Convex physics has a non-finite source point");
        const double xyz[3]={vertex.x,vertex.y,vertex.z};Point point{};
        for(unsigned r=0;r<3;++r){double value=source.local.At(r,3);for(unsigned c=0;c<3;++c)value+=source.local.At(r,c)*xyz[c];point.v[r]=value*scales[r];if(!std::isfinite(point.v[r])||std::abs(point.v[r])>1e6)throw Error("Convex physics point exceeds finite local bounds");}
        points.push_back(point);
    }
    // Bullet's explicit convex-hull builder returns planar polygon loops. Source
    // triangle/plane buffers remain untouched and are not guessed here.
    btConvexHullComputer hull;
    if(hull.compute(points.front().v,sizeof(Point),int(points.size()),0,0)<0 || hull.vertices.size()<4 || hull.faces.size()<4)throw Error("Convex physics hull is empty or lower-dimensional");
    if(hull.vertices.size()>8192 || hull.edges.size()>65536 || hull.faces.size()>32768)throw Error("Convex physics hull exceeds bounded topology");
    if(hull.original_vertex_index.size()!=hull.vertices.size())throw Error("Convex physics hull lost source vertex ownership");
    std::vector<Point> hullPoints;hullPoints.reserve(size_t(hull.vertices.size()));
    for(int i=0;i<hull.vertices.size();++i){const int sourceIndex=hull.original_vertex_index[i];if(sourceIndex<0 || size_t(sourceIndex)>=points.size())throw Error("Convex physics source vertex index is invalid");hullPoints.push_back(points[size_t(sourceIndex)]);}
    Point reference{};for(const auto& vertex:hullPoints)reference=Add(reference,vertex);reference=Scale(reference,1.0/hullPoints.size());
    double volume=0,first[3]={},second[3][3]={};
    auto point=[&](int index){if(index<0 || size_t(index)>=hullPoints.size())throw Error("Convex physics hull index is invalid");return Sub(hullPoints[size_t(index)],reference);};
    auto tetrahedron=[&](Point a,Point b,Point c){
        const double v=Dot(a,Cross(b,c))/6;volume+=v;
        const auto sum=Add(Add(a,b),c);
        for(unsigned r=0;r<3;++r){
            first[r]+=v*sum.v[r]/4;
            for(unsigned s=0;s<3;++s)second[r][s]+=v*(sum.v[r]*sum.v[s]+a.v[r]*a.v[s]+b.v[r]*b.v[s]+c.v[r]*c.v[s])/20;
        }
    };
    for(int face=0;face<hull.faces.size();++face){
        const int startIndex=hull.faces[face];if(startIndex<0 || startIndex>=hull.edges.size())throw Error("Convex physics hull face is invalid");
        const auto* start=&hull.edges[startIndex];const auto* edge=start;
        std::vector<int> polygon;
        do{
            if(edge<&hull.edges[0] || edge>=&hull.edges[0]+hull.edges.size() || polygon.size()>=size_t(hull.edges.size()))throw Error("Convex physics hull face loop is invalid");
            polygon.push_back(edge->getSourceVertex());edge=edge->getNextEdgeOfFace();
        }while(edge!=start);
        if(polygon.size()<3)throw Error("Convex physics hull has a partial face");
        for(size_t i=1;i+1<polygon.size();++i)tetrahedron(point(polygon[0]),point(polygon[i]),point(polygon[i+1]));
    }
    if(!std::isfinite(volume)||std::abs(volume)<1e-12 || std::abs(volume)>1e12)throw Error("Convex physics hull volume is degenerate or outside bounds");
    const double sign=volume<0?-1:1;volume*=sign;
    Point center{};for(unsigned r=0;r<3;++r){center.v[r]=first[r]*sign/volume;for(unsigned s=0;s<3;++s)second[r][s]=second[r][s]*sign/volume;}
    // Central second moments per unit mass, then inertia = trace(S)*I-S.
    for(unsigned r=0;r<3;++r)for(unsigned s=0;s<3;++s)second[r][s]-=center.v[r]*center.v[s];
    const double trace=second[0][0]+second[1][1]+second[2][2];
    btMatrix3x3 inertia;for(unsigned r=0;r<3;++r)for(unsigned s=0;s<3;++s){const double value=(r==s?trace:0)-second[r][s];if(!std::isfinite(value))throw Error("Convex physics inertia is not finite");inertia[r][s]=btScalar(value);}
    const auto original=inertia;btMatrix3x3 rotation;inertia.diagonalize(rotation,btScalar(1e-8),64);
    const btMatrix3x3 diagonal(inertia[0][0],0,0,0,inertia[1][1],0,0,0,inertia[2][2]);
    double maximum=0,residual=0;const auto reconstructed=rotation*diagonal*rotation.transpose();
    for(unsigned r=0;r<3;++r)for(unsigned s=0;s<3;++s){maximum=std::max(maximum,double(btFabs(original[r][s])));residual=std::max(residual,double(btFabs(original[r][s]-reconstructed[r][s])));}
    if(!std::isfinite(residual)||residual>std::max(1e-8,maximum*1e-4))throw Error("Convex physics principal inertia residual exceeds tolerance");
    for(unsigned r=0;r<3;++r)if(!std::isfinite(double(inertia[r][r]))||inertia[r][r]<=0)throw Error("Convex physics inertia is not positive");
    ConvexPhysicsMesh result;result.volume=volume;result.inertiaPerMass={float(inertia[0][0]),float(inertia[1][1]),float(inertia[2][2])};
    const auto bodyCenter=Add(reference,center);
    for(unsigned r=0;r<3;++r){result.childFrame.At(r,3)=bodyCenter.v[r];for(unsigned c=0;c<3;++c)result.childFrame.At(r,c)=rotation[r][c];}
    for(const auto& vertex:hullPoints){
        const auto centred=Sub(vertex,bodyCenter);
        const auto local=rotation.transpose()*btVector3(btScalar(centred.v[0]),btScalar(centred.v[1]),btScalar(centred.v[2]));
        Vec3 p{local.x(),local.y(),local.z()};if(!Finite(p))throw Error("Convex physics principal vertex is not finite");result.vertices.push_back(p);
    }
    return result;
}
}
