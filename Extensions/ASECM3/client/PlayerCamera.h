#pragma once
class CGUnit_C;
namespace ASECM3::Client {
enum class PlayerCameraFallback {Native,Pending,Bounds};
PlayerCameraFallback GetPlayerCameraFallback(CGUnit_C* unit,float scale,float& height);
}
