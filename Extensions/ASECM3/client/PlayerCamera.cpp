#include "PlayerCamera.h"
#include "UnitWorldBinding.h"
#include "ASECM3/CameraHeight.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/lua-5.1/src/asecDiagnostics.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
namespace ASECM3::Client {
namespace {void Record(const char* mode,bool module,float lower,float upper,float scale,float height){try{std::ofstream file(std::filesystem::path(SFile::m_basePath)/"Logs"/"ASECM3-camera.log",std::ios::app);if(file)file<<"mode="<<mode<<" model="<<(module?"m3":"m2")<<" lowerZ="<<lower<<" upperZ="<<upper<<" scale="<<scale<<" height="<<height<<'\n';}catch(...) {}}}
PlayerCameraFallback GetPlayerCameraFallback(CGUnit_C* unit,float scale,float& height){
    height=0;if(!unit||!unit->IsActivePlayer()||!unit->m_worldModel)return PlayerCameraFallback::Native;
    float lower=0,upper=0;bool pending=false;const bool module=UnitCameraBounds(unit,lower,upper,pending);
    if(pending)return PlayerCameraFallback::Pending;
    auto* model=unit->m_worldModel;if(!model->IsLoaded(0,0))return PlayerCameraFallback::Pending;
    if(model->HasAttachment(17)){const auto pivot=model->GetAttachmentPivot(17);if(std::isfinite(pivot.z)&&pivot.z>0){Record("attachment",module,0,pivot.z,scale,pivot.z*scale);return PlayerCameraFallback::Native;}}
    try{
        if(!module){
            if(!model->m_shared||!model->m_shared->m_data)throw Error("Player camera model data is unavailable");
            const auto& vertices=model->m_shared->m_data->m_vertices;const auto* data=vertices.Data();
            if(!vertices.m_count||vertices.m_count>2000000||!data)throw Error("Player camera vertex count is empty or exceeds budget");
            lower=upper=data[0].m_position.z;
            for(uint32_t i=0;i<vertices.m_count;++i){const float z=data[i].m_position.z;if(!std::isfinite(z))throw Error("Player camera vertex is nonfinite");lower=std::min(lower,z);upper=std::max(upper,z);}
        }
        height=CameraPivotHeight(lower,upper,scale);
    }catch(const std::exception& error){ASEC_LuaDiagnostic(nullptr,error.what(),0,0,0,__FILE__,__LINE__);height=0;}
    Record("bounds",module,lower,upper,scale,height);return PlayerCameraFallback::Bounds;
}
}
