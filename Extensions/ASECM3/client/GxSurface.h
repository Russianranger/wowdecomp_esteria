#pragma once
#include "GxDDS.h"
#include "ASECM3/Scene.h"
#include "ASECM3/MaterialEvaluation.h"
#include <functional>
class CGxShader;
class CGxTex;
class C3Vector;
namespace ASECM3::Client {
class GxGeometryTransform;
struct SceneEnvironment;
class GxSurface final {
    CGxDevice* m_device=nullptr;
    CGxShader *m_vertex=nullptr,*m_pixel=nullptr;
    CGxShader* m_pixelCube=nullptr;
    CGxShader* m_distortionVertex=nullptr;
    CGxShader* m_distortionPixel=nullptr;
    CGxTex* m_screen=nullptr;
    uint32_t m_screenWidth=0,m_screenHeight=0,m_screenTexWidth=0,m_screenTexHeight=0;
    uint64_t m_distortionDraws=0,m_distortionCopies=0;
    int32_t m_copyFrame=-1,m_copyLeft=0,m_copyTop=0,m_copyRight=0,m_copyBottom=0;
    uint64_t m_draws=0;
    uint64_t m_texturedDraws=0;
    uint64_t m_stateRestores=0,m_stateFailures=0;
    uint64_t m_normalDraws=0;
    uint64_t m_specularDraws=0;
    uint64_t m_environmentDraws=0,m_cubeDraws=0,m_sphereDraws=0;
    uint64_t m_worldDraws=0,m_fogDraws=0,m_pointDraws=0;
    uint64_t m_verticesPacked=0,m_indicesSubmitted=0;
    bool m_attempted=false;
    std::string m_error;
    bool Ready();
    bool EnsureScreen();
public:
    ~GxSurface();
    bool Draw(const SceneRegion& region,const Material& material,const MaterialPass& pass,
              const MaterialEvaluator& values,const C3Vector& eye,const C3Vector& center,
              float scale,bool lit,const std::function<GxDDS*(const std::string&)>& texture,
              const GxGeometryTransform* geometry=nullptr,const SceneEnvironment* environment=nullptr);
    // DIS_ materials: copies the scene behind the mesh and bends it by the normal/strength layers.
    bool DrawDistortion(const SceneRegion& region,const Material& material,const MaterialPass& pass,
              const MaterialEvaluator& values,const C3Vector& eye,const C3Vector& center,
              float scale,const std::function<GxDDS*(const std::string&)>& texture,
              const GxGeometryTransform* geometry=nullptr,const SceneEnvironment* environment=nullptr);
    uint64_t DistortionDraws() const { return m_distortionDraws; }
    uint64_t DistortionCopies() const { return m_distortionCopies; }
    uint64_t Draws() const { return m_draws; }
    uint64_t TexturedDraws() const { return m_texturedDraws; }
    uint64_t StateRestores() const { return m_stateRestores; }
    uint64_t StateFailures() const { return m_stateFailures; }
    uint64_t NormalDraws() const { return m_normalDraws; }
    uint64_t SpecularDraws() const { return m_specularDraws; }
    uint64_t EnvironmentDraws() const { return m_environmentDraws; }
    uint64_t CubeDraws() const { return m_cubeDraws; }
    uint64_t SphereDraws() const { return m_sphereDraws; }
    uint64_t WorldDraws() const { return m_worldDraws; }
    uint64_t FogDraws() const { return m_fogDraws; }
    uint64_t PointDraws() const { return m_pointDraws; }
    uint64_t VerticesPacked() const { return m_verticesPacked; }
    uint64_t IndicesSubmitted() const { return m_indicesSubmitted; }
    const std::string& ErrorText() const { return m_error; }
};
}
