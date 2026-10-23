#pragma once
#include "MaterialEvaluation.h"
#include "Scene.h"
namespace ASECM3 {
// A source RIB_ owner simulated as a camera-facing trail behind its bone. This is an approximation: the source ribbon
// types (planar, cylinder, star), twist, spline and the force fields are not interpreted. It follows the bone through
// world space, moves each point along the bone's Z axis at the source speed, ages points over the source lifespan and
// shades them by the source scale/colour curves.
class RibbonTrail final {
    struct Point {double birthMs=0;Vec3 position,direction;};
    std::shared_ptr<const Scene> m_scene;
    uint32_t m_ribbon=0;
    std::vector<Point> m_points;   // oldest first
    Point m_head;bool m_hasHead=false;
    double m_lastMs=0,m_lastEmitMs=-1e18;
    bool m_active=false;
    double m_scale=1;
public:
    RibbonTrail(std::shared_ptr<const Scene> scene,uint32_t ribbon);
    // elapsedMs: effect clock. group/sourceTime: the effect clip's animation group and timeline position. boneToWorld
    // places the ribbon's bone in world space (uniform scale only).
    void Advance(uint64_t elapsedMs,int32_t group,int32_t sourceTime,const Matrix& boneToWorld);
    // Triangle strip as an indexed region in the space of worldToCamera, facing the origin. Empty when there is nothing
    // to draw.
    SceneRegion Build(const Matrix& worldToCamera,int32_t group,int32_t sourceTime)const;
    // World-space box of the trail and its width.
    bool WorldBounds(Vec3& lower,Vec3& upper)const;
    uint32_t SourceRibbon()const{return m_ribbon;}
    size_t Points()const{return m_points.size()+(m_hasHead?1:0);}
};
}
