#include "ASECM3/ClothTethers.h"
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
namespace ASECM3 {
std::vector<ClothTether> BuildClothTethers(const std::vector<Vec3>& points,const std::vector<std::array<uint32_t,2>>& edges,const std::vector<uint8_t>& pinned){
    if(points.size()!=pinned.size()||points.size()>100000||edges.size()>600000)throw Error("Invalid cloth tether graph budget/domain");
    std::vector<std::vector<std::pair<uint32_t,double>>> graph(points.size());
    for(const auto& p:points)if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw Error("Nonfinite cloth tether bind point");
    for(const auto& edge:edges){
        if(edge[0]>=points.size()||edge[1]>=points.size())throw Error("Cloth tether edge exceeds vertex domain");
        const auto& a=points[edge[0]];const auto& b=points[edge[1]];
        const double x=double(a.x)-b.x,y=double(a.y)-b.y,z=double(a.z)-b.z,length=std::sqrt(x*x+y*y+z*z);
        graph[edge[0]].push_back({edge[1],length});graph[edge[1]].push_back({edge[0],length});
    }
    std::vector<double> distance(points.size(),std::numeric_limits<double>::infinity());
    std::vector<uint32_t> anchor(points.size(),UINT32_MAX);
    using Entry=std::pair<double,uint32_t>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> pending;
    for(uint32_t i=0;i<pinned.size();++i){if(pinned[i]>1)throw Error("Invalid cloth tether pin flag");if(pinned[i]){distance[i]=0;anchor[i]=i;pending.push({0,i});}}
    while(!pending.empty()){
        const auto next=pending.top();pending.pop();if(next.first!=distance[next.second])continue;
        for(const auto& edge:graph[next.second]){const auto candidate=next.first+edge.second;if(candidate<distance[edge.first]){distance[edge.first]=candidate;anchor[edge.first]=anchor[next.second];pending.push({candidate,edge.first});}}
    }
    std::vector<ClothTether> out;out.reserve(points.size());
    for(uint32_t i=0;i<points.size();++i)if(!pinned[i]&&anchor[i]!=UINT32_MAX)out.push_back({i,anchor[i],distance[i]});
    return out;
}
}
