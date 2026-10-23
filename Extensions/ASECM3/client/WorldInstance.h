#pragma once
#include "GxScene.h"
#include "GxRenderGuard.h"
#include "Engine/Source/Tempest/C44Matrix.h"
namespace ASECM3::Client {
// A world pass shares these stream buffers rather than allocating them per unit.
// The owner must destroy this context on the render thread before device teardown.
class WorldRenderContext final {
    friend class WorldInstance;
    GxStreamBuffers m_buffers;
    uint64_t m_checks=0,m_failures=0;
public:
    uint64_t BoundaryChecks()const{return m_checks;}
    uint64_t BoundaryFailures()const{return m_failures;}
};
struct WorldBounds {C3Vector lower,upper;};
// No Unit/DBC/World pointers are stored. Host dispatch owns one instance per
// world object and passes its current model-to-camera matrix on each draw.
class WorldInstance final {
    GxScene m_renderer;
    int32_t m_action=-1;
    Vec3 m_lower{},m_upper{};
    Vec3 m_maxLower{},m_maxUpper{};
    bool m_hasBounds=false;
    bool m_drawDeferred=false;
    std::string m_error;
public:
    explicit WorldInstance(std::shared_ptr<const Scene> source,const std::string& path);
    bool Advance(double seconds,const std::function<std::vector<uint8_t>(const std::string&)>& provider);
    bool SetWoWAction(uint32_t action,bool restart=false);
    void SetWorldTransform(const C44Matrix& sourceToWorld);
    bool Draw(WorldRenderContext& context,const C44Matrix& modelToCamera,const C44Matrix& projection,bool lit,const SceneEnvironment* environment,int32_t packet,const C44Matrix* worldToCamera,bool modelMirrored);
    // Draw succeeds without submission while the native context is unavailable.
    // No render guard/resources are touched and no restoration failure is hidden.
    bool DrawDeferred()const{return m_drawDeferred;}
    bool Bounds(const C44Matrix& modelToWorld,WorldBounds& out,bool maximum=false)const;
    GxScene& Renderer(){return m_renderer;}
    const GxScene& Renderer()const{return m_renderer;}
    const std::string& ErrorText()const{return m_error;}
};
}
