#ifndef WOW_SOURCE_UI_SPEEDFLYING_H
#define WOW_SOURCE_UI_SPEEDFLYING_H

#include <cstdint>

class CGUnit_C;

// Client half of the mana-speedflying link.
//
// The addon under Interface\AddOns\Manastorm_Speedflying is the presentation: it owns the HUD frames, the talent
// panel and the micro-menu button, and it reads ~45 globals back out of _G. This file is the half that was
// missing -- nothing in this client ever wrote those globals, which is why the HUD never appeared.
//
// It talks to the server module (mod-mana-speedflying) over the addon channel, which is the only channel a
// native client module has. The protocol is documented in that module's README and implemented in
// src/mod_mana_speedflying.cpp; both sides must agree exactly:
//
//   client -> server   WXLMS\t<advfly:0|1><state:L|C|D|H|B|X>
//                      WXLTT\tW\t<id>                          learn one rank
//                      WXLTT\tB\t<id>:<target>[,...]           learn to an absolute rank
//                      WXLTT\tR\t<1|2|3|A>                     refund a tree (1 dive, 2 glide, 3 wings) / everything
//                      WXLTT\tQ                                request the tree
//   server -> client   WXLMS\tS:<levelPct>,<divePct>,<boostPct>,<cooldownMs>,<maxStacks>,<boostFadeMs>,
//                             <stacks>,<stackMs>,<stackDiveMs>[,<accumMs>,<intervalMs>]
//                      WXLTT\tT:<points>,<spent>,<unspent>,<n>,<ranks>,P<permille>,N<yards>
//                             one whisper holds the whole tree: <ranks> is <n> characters, one per talent in
//                             table order ("0"-"9", then "a"-"z"). The listener keeps only the last T/F body.
//                      WXLTT\tP:<permille>,<yards>
//                      WXLTT\tF:<reason>
//
// Sending has to go through Lua: the engine builds CMSG_MESSAGECHAT in Script_SendAddonMessage and there is
// no C++ equivalent, so SpeedFlying.lua exposes Manastorm_SendAddon for this module to call. Receiving needs
// nothing from us -- Listener.lua already stashes every inbound body in a global.
namespace SpeedFlying
{
    // Called once, after the frame script context exists. Safe to call before the addon is loaded.
    void Initialize();

    // Called once per frame from the world frame tick. Cheap when not mounted.
    void Update(uint32_t timeMs);

    // sin of the flight-path angle above which the mount is climbing / below which it is diving. Talents move
    // both (Still Air, Tucked Wings); the flight state and the mount's clip chooser must both read these.
    float ClimbSin();
    float DiveSin();

    // Camera effects, read by CGCamera every frame. Both are 0 outside AdvFly flight.
    // The extra field of view of a fast dive or a boost, in radians.
    float CameraFovBoost();

    // The dive shake's amplitude, in radians of rotation about the camera's right and up axes.
    float CameraShake();
    // Esteria: called once the local player's model is placed for the frame. Leans the mount into turns, spins
    // it for Whirling Surge, and keeps the skyriding wind and impact effects on it.
    void OnLocalTransform(CGUnit_C* unit);
}

#endif
