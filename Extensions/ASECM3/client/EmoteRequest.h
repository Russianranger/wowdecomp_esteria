#pragma once
#include <cstdint>
namespace ASECM3::Client {
// Explicit emotes outlive the placeholder's idle fallback. They remain local
// visual state and are cancelled by movement/combat/casting or another action.
class EmoteRequest {
    int32_t action=-1;uint32_t started=0;bool clockStarted=false;
public:
    void Set(uint32_t value){action=value<=505?int32_t(value):-1;clockStarted=false;}
    void Clear(){action=-1;clockStarted=false;}
    int32_t Action()const{return action;}
    uint32_t Choose(uint32_t nativeAction,bool interrupted,uint32_t time,uint32_t duration){
        if(action<0)return nativeAction;
        if(interrupted||(nativeAction<=505&&nativeAction!=0&&nativeAction!=uint32_t(action))){Clear();return nativeAction;}
        if(!clockStarted){started=time;clockStarted=true;}
        if(action!=69&&uint32_t(time-started)>=duration){Clear();return nativeAction;}
        return uint32_t(action);
    }
};
}
