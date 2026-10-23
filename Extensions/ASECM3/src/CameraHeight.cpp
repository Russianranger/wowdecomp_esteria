#include "ASECM3/CameraHeight.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace ASECM3 {
float CameraPivotHeight(float lower,float upper,float scale){
    if(!std::isfinite(lower)||!std::isfinite(upper)||lower>upper||!std::isfinite(scale)||scale<0)throw Error("Invalid player camera model bounds or scale");
    const double height=std::max(0.0,double(upper)-.1*(double(upper)-lower))*scale;
    if(!std::isfinite(height)||height>std::numeric_limits<float>::max())throw Error("Player camera height exceeds finite range");return float(height);
}
bool SceneCameraBounds(const Scene& scene,float& lower,float& upper){
    bool found=false;lower=upper=0;const auto frame=scene.EvaluateGroup();const auto& helpers=scene.Physics().hiddenSimulationRegions;
    for(const auto& region:frame.regions)if(region.division==0&&!std::binary_search(helpers.begin(),helpers.end(),region.region))for(const auto& vertex:region.vertices){
        const float z=vertex.position.z;if(!std::isfinite(z))throw Error("Nonfinite player camera source vertex");if(!found){lower=upper=z;found=true;}else{lower=std::min(lower,z);upper=std::max(upper,z);}
    }
    return found;
}
}
