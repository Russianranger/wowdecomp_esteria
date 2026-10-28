#include "WoW/Source/Ui/SpeedFlying.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/Frame/CSimpleEditBox.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Os/Input.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/WorldClient/FrameTime.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/TalentUI.h"

#include <cstdarg>

// The engine builds CMSG_MESSAGECHAT for addon chat inside Script_SendAddonMessage, and exposes no C++
// equivalent, so sending goes through a Lua helper the same addon provides. See SpeedFlying.lua.
#define SF_LUA_SEND "Manastorm_SendAddon"

namespace
{
    // Diagnostics: Logs\speedflying.log, one line per notable step with a millisecond tick. Main thread only.
    void Log(const char* format, ...)
    {
        FILE* file = std::fopen("Logs/speedflying.log", "a");
        if (!file) return;
        std::fprintf(file, "%08lu ", static_cast<unsigned long>(GetTickCount()));
        va_list args;
        va_start(args, format);
        std::vfprintf(file, format, args);
        va_end(args);
        std::fputc('\n', file);
        std::fclose(file);
    }

    // Link state letters, from the server module's README and its `default:` branch. An unknown letter is
    // treated by the server as plain level flight, so a wrong one degrades rather than breaks.
    constexpr char kStateLevel    = 'L';
    constexpr char kStateClimb    = 'C';
    constexpr char kStateDive     = 'D';
    constexpr char kStateHover    = 'H';
    constexpr char kStateBoost    = 'B';

    // The server throttles nothing but drops floods; it asks for roughly one message per 200 ms plus a 1 s
    // heartbeat. Sending faster than that is pure spam.
    constexpr uint32_t kSendIntervalMs = 200;
    constexpr uint32_t kHeartbeatMs    = 1000;

    // How long a freshly seen flight state must persist before it is applied. Every change restarts the
    // mount's clip and the server's speed blend, so following single jittery frames is what made both
    // stutter. The reference uses the same 60 ms for the same reason.
    constexpr uint32_t kHoldMs = 60;

    // The reference's DebugHUD setting: when on, the HUD adds its debug readout (currently the live speed
    // percentage). Off by default, exactly like the reference's INI. Flip this to 1 to show it.
    constexpr bool g_debugHud = false;

    // The reference's FlightStateText setting: the state words ("Leveling", "Gliding", the Diving callout).
    // Off by default there too -- the speed bar already carries the state, the words are clutter.
    constexpr bool g_flightStateText = false;

    // A spent stack is mirrored locally at once; an announce minted before the server processed the press
    // still shows it as held, so a raise inside this window is ignored (the reference's kSpendGraceMs).
    constexpr uint32_t kSpendGraceMs = 500;

    // Two SPACE taps this close are a take-off, not a second boost.
    constexpr uint32_t kDoubleTapMs = 300;

    // What the server announced about itself. Until the first S: arrives these are the reference client's
    // own documented fallbacks, so the bar and shake have sane ends even with no server module.
    struct ServerSpeeds
    {
        float    m_levelPct   = 280.0f;
        float    m_divePct    = 600.0f;
        float    m_boostPct   = 1200.0f;
        uint32_t m_maxStacks  = 3;
        uint32_t m_stacks     = 3;
        uint32_t m_stackMs      = 9000;
        uint32_t m_stackDiveMs  = 3000;
        uint32_t m_stackAccumMs = 0;
        uint32_t m_stackIntervalMs = 9000;
        uint32_t m_boostFadeMs = 4000;
        bool     m_received   = false;
    };

    struct State
    {
        ServerSpeeds m_speeds;

        // The held stack count: the server's last announce, less anything spent here since. The server
        // announces every grant and every spend at once, so no recharge is extrapolated -- a guess that
        // ran ahead of the server only got a press refused by it.
        int32_t  m_stacksBase = 0;
        uint32_t m_lastSpendMs = 0;
        uint32_t m_spaceTapMs = 0;

        // GetTime() (seconds) stamp the Listener put on the last WXLMS announce; the orb row anchors to it.
        double   m_announceTime = 0.0;

        // The last link message sent, so a steady state does not repeat itself.
        bool     m_sentAdvFly   = false;
        char     m_sentState    = 0;
        uint32_t m_lastSendMs   = 0;
        uint32_t m_lastAnyMs    = 0;
        uint32_t m_lastBoostMs  = 0;

        // What we last pushed into the addon globals, so a per-frame tick is not also per-frame Lua churn.
        char     m_uiState[16] = {};
        char     m_uiSpeedPct[16] = {};
        float    m_uiFill = -1.0f;
        float    m_uiDiveFrac = -1.0f;
        int32_t  m_uiStacks = -1;
        int32_t  m_uiMaxStacks = -1;
        bool     m_uiFrameOn = false;
        bool     m_uiDiving = false;
        bool     m_uiGlideOn = false;
        bool     m_uiSpaceBound = false;

        // Client-side view of the two angle talents: 140 Still Air (climb threshold) and 141 Tucked Wings (dive
        // threshold). Every other talent is applied entirely server-side and never needs parsing here.
        uint32_t m_rankStillAir = 0;
        uint32_t m_rankTuckedWings = 0;

        // Parsed tally bookkeeping, only used to feed the addon's own panel globals.
        char     m_talentBody[512] = {};
        char     m_talentBar[64]   = {};
        char     m_refusal[64]     = {};

        // Re-scan the inbound Lua globals on a timer rather than every frame: they are strings the Listener
        // writes, and reading them means crossing into Lua.
        uint32_t m_lastPollMs = 0;
        uint32_t m_lastTalentQueryMs = 0;
        char     m_seenServerMsg[192] = {};
        char     m_seenTalentBody[512] = {};
        char     m_seenTalentBar[64] = {};

        // The orb recharge anchor came with a fresh server announce and has not been published yet.
        bool     m_anchorDirty = false;

        bool     m_initialised = false;
    };

    State s_state;

    // Defined with the boost send, but PublishHud needs it (the orb row shows the same estimate a press
    // is judged on, so the two cannot disagree).
    int32_t EffectiveStacks(uint32_t nowMs);

    lua_State* L()
    {
        return FrameScript_GetContext();
    }

    // A UI reload (/reload, or a reload of the frame script context) makes a brand-new lua_State with none of
    // the globals this module publishes. Every "already pushed" cache is therefore keyed on the state it was
    // pushed into: when that pointer changes the caches are invalid and everything is published again.
    //
    // Every publisher keeps its own `seen` pointer. One shared pointer answered "yes" to whichever site asked
    // first in a frame and "no" to the rest, so after a reload only one of them ever republished.
    bool NeedsRepublish(lua_State* lua, lua_State*& seen)
    {
        if (lua != seen)
        {
            seen = lua;
            return true;
        }

        return false;
    }

    // Reads a global as a string. Returns false when absent or not a string.
    bool ReadGlobalString(const char* name, char* out, size_t outLen)
    {
        lua_State* lua = L();

        if (!lua || !out || outLen < 2)
        {
            return false;
        }

        lua_getglobal(lua, name);

        bool ok = false;

        if (lua_isstring(lua, -1))
        {
            const char* value = lua_tolstring(lua, -1, nullptr);

            if (value)
            {
                SStrCopy(out, value, outLen);
                ok = true;
            }
        }

        lua_pop(lua, 1);
        return ok;
    }

    bool ReadGlobalNumber(const char* name, double* out)
    {
        lua_State* lua = L();

        if (!lua || !out)
        {
            return false;
        }

        lua_getglobal(lua, name);

        bool ok = false;

        if (lua_isnumber(lua, -1))
        {
            *out = lua_tonumber(lua, -1);
            ok = true;
        }

        lua_pop(lua, 1);
        return ok;
    }

    void SetGlobalNumber(const char* name, double value)
    {
        lua_State* lua = L();

        if (!lua)
        {
            return;
        }

        lua_pushnumber(lua, value);
        lua_setglobal(lua, name);
    }

    void SetGlobalString(const char* name, const char* value)
    {
        lua_State* lua = L();

        if (!lua)
        {
            return;
        }

        lua_pushstring(lua, value ? value : "");
        lua_setglobal(lua, name);
    }

    // Sends a WXLMS/WXLTT body through the addon's Lua helper. The helper prefixes nothing: the caller
    // passes prefix and body separately, matching SendAddonMessage's signature.
    void SendLink(const char* prefix, const char* body)
    {
        lua_State* lua = L();

        if (!lua || !prefix || !body)
        {
            return;
        }

        lua_getglobal(lua, SF_LUA_SEND);

        if (!lua_isfunction(lua, -1))
        {
            lua_pop(lua, 1);
            return;
        }

        lua_pushstring(lua, prefix);
        lua_pushstring(lua, body);

        if (lua_pcall(lua, 2, 0, 0) != 0)
        {
            lua_pop(lua, 1);
            Log("speedflying: send %s\t%s FAILED", prefix, body);
            return;
        }

        Log("speedflying: send %s\t%s", prefix, body);
    }

    // --- protocol parsing -------------------------------------------------------------------------

    // WXLMS\tS:<levelPct>,<divePct>,<boostPct>,<cooldownMs>,<maxStacks>,<boostFadeMs>,<stacks>,
    //         <stackMs>,<stackDiveMs>[,<accumMs>,<intervalMs>]
    void ParseServerSpeeds(const char* body)
    {
        if (!body || body[0] != 'S' || body[1] != ':')
        {
            return;
        }

        float values[11] = {};
        uint32_t count = 0;

        const char* cursor = body + 2;

        while (cursor && *cursor && count < 11)
        {
            values[count++] = static_cast<float>(atof(cursor));

            const char* comma = strchr(cursor, ',');

            if (!comma)
            {
                break;
            }

            cursor = comma + 1;
        }

        // Nine fields are mandatory: stacks, stackMs and stackDiveMs live at 6..8, and leaving them at 0
        // disabled boost entirely and handed the addon a 0 ms recharge interval to divide by.
        if (count < 9)
        {
            return;
        }

        s_state.m_speeds.m_levelPct  = values[0];
        s_state.m_speeds.m_divePct   = values[1];
        s_state.m_speeds.m_boostPct  = values[2];
        s_state.m_speeds.m_maxStacks = static_cast<uint32_t>(values[4]);
        s_state.m_speeds.m_boostFadeMs = static_cast<uint32_t>(values[5]);
        s_state.m_speeds.m_stacks    = static_cast<uint32_t>(values[6]);
        s_state.m_speeds.m_stackMs     = static_cast<uint32_t>(values[7]);
        s_state.m_speeds.m_stackDiveMs = static_cast<uint32_t>(values[8]);

        // The announce is the truth, except an echo minted before the server saw our own press: that one
        // still shows the stack we just spent as held, so a raise inside the grace window is dropped (a
        // lower count is the server agreeing with the click, and always taken).
        const uint32_t nowMs = static_cast<uint32_t>(OsGetAsyncTimeMs());
        const int32_t announced = static_cast<int32_t>(values[6]);
        const bool staleEcho = s_state.m_lastSpendMs && nowMs - s_state.m_lastSpendMs < kSpendGraceMs
            && announced > s_state.m_stacksBase;

        if (!staleEcho)
        {
            s_state.m_stacksBase = announced;
        }

        s_state.m_speeds.m_received  = true;
        s_state.m_anchorDirty = true;

        // The two trailing fields are the server's accrual anchor; an older server omits them and the addon
        // then runs its own clock.
        s_state.m_speeds.m_stackAccumMs = count >= 11 ? static_cast<uint32_t>(values[9]) : 0;
        s_state.m_speeds.m_stackIntervalMs = count >= 11 ? static_cast<uint32_t>(values[10]) : s_state.m_speeds.m_stackMs;
    }

    // T:<points>,<spent>,<unspent>,<n>,<ranks>,P<permille>,N<yards>
    //
    // The whole tree arrives in one body (the listener keeps only the last T/F body, so it cannot be split).
    // <ranks> is one character per talent in table order. The panel gets every rank through TalentUI; the two
    // angle bridges (Still Air 140, Tucked Wings 141) are also read here, by id, for the camera's state angles.
    void ParseTally(const char* body)
    {
        if (!body || body[0] != 'T' || body[1] != ':')
        {
            return;
        }

        SStrCopy(s_state.m_talentBody, body, sizeof(s_state.m_talentBody));

        const char* cursor = body + 2;

        const char* comma1 = strchr(cursor, ',');
        const char* comma2 = comma1 ? strchr(comma1 + 1, ',') : nullptr;
        const char* comma3 = comma2 ? strchr(comma2 + 1, ',') : nullptr;
        const char* comma4 = comma3 ? strchr(comma3 + 1, ',') : nullptr;

        if (!comma4)
        {
            return;
        }

        const uint32_t points  = static_cast<uint32_t>(atoi(cursor));
        const uint32_t spent   = static_cast<uint32_t>(atoi(comma1 + 1));
        const uint32_t unspent = static_cast<uint32_t>(atoi(comma2 + 1));
        const int      count   = atoi(comma3 + 1);
        const char*    ranks   = comma4 + 1;

        TalentUI::OnTally(points, spent, unspent);
        TalentUI::OnRanks(ranks, count);

        // The bar's tail rides the same body: ",P<permille>,N<yards>".
        const char* bar = strstr(ranks, ",P");

        if (bar)
        {
            const char* need = strstr(bar, ",N");
            TalentUI::OnPartial(static_cast<uint32_t>(atoi(bar + 2)),
                                need ? static_cast<uint32_t>(atoi(need + 2)) : 0u);
        }

        constexpr uint32_t kTalentStillAir     = 140;
        constexpr uint32_t kTalentTuckedWings  = 141;

        s_state.m_rankStillAir    = static_cast<uint32_t>(TalentUI::RankOf(static_cast<int>(kTalentStillAir)));
        s_state.m_rankTuckedWings = static_cast<uint32_t>(TalentUI::RankOf(static_cast<int>(kTalentTuckedWings)));
    }

    // Polls what Listener.lua stashed and reacts to anything new.
    void PollInbound(uint32_t nowMs)
    {
        if (nowMs - s_state.m_lastPollMs < 100)
        {
            return;
        }

        s_state.m_lastPollMs = nowMs;

        char buffer[512];

        // Keyed on the Listener's arrival stamp, not on the text: a heartbeat that repeats the previous
        // body is still a fresh anchor for the orb clock.
        double arrivedAt = 0.0;

        if (ReadGlobalNumber("Manastorm_MountAnimServerMsgTime", &arrivedAt) && arrivedAt != s_state.m_announceTime
            && ReadGlobalString("Manastorm_MountAnimServerMsg", buffer, sizeof(buffer)))
        {
            s_state.m_announceTime = arrivedAt;
            SStrCopy(s_state.m_seenServerMsg, buffer, sizeof(s_state.m_seenServerMsg));

            // Listener stores the whole body including the leading "S:".
            ParseServerSpeeds(buffer);
            Log("speedflying: server %s", buffer);
        }

        if (ReadGlobalString("Manastorm_TalentBody", buffer, sizeof(buffer))
            && strcmp(buffer, s_state.m_seenTalentBody) != 0)
        {
            SStrCopy(s_state.m_seenTalentBody, buffer, sizeof(s_state.m_seenTalentBody));

            if (buffer[0] == 'T' && buffer[1] == ':')
            {
                ParseTally(buffer);
                Log("talent: tally %s", buffer);
            }
            else if (buffer[0] == 'F' && buffer[1] == ':')
            {
                // A refusal is the only feedback a rejected click gets, so it is handed to the panel as its
                // status line. Without this the player saw nothing at all.
                SStrCopy(s_state.m_refusal, buffer + 2, sizeof(s_state.m_refusal));
                TalentUI::OnStatus(s_state.m_refusal);
                Log("talent: server refused: %s", s_state.m_refusal);
            }
        }

        if (ReadGlobalString("Manastorm_TalentBar", buffer, sizeof(buffer))
            && strcmp(buffer, s_state.m_seenTalentBar) != 0)
        {
            SStrCopy(s_state.m_seenTalentBar, buffer, sizeof(s_state.m_seenTalentBar));
            SStrCopy(s_state.m_talentBar, buffer, sizeof(s_state.m_talentBar));

            // "P	<permille>,<yards>" -- the partial point, on its own so the tally is not resent for it.
            if (buffer[0] == 'P' && buffer[1] == ':')
            {
                TalentUI::OnPartial(static_cast<uint32_t>(atoi(buffer + 2)),
                                    static_cast<uint32_t>(atoi(strchr(buffer + 2, ',') ? strchr(buffer + 2, ',') + 1 : "0")));
            }
        }
    }

    // --- the flight state -------------------------------------------------------------------------

    // Picks the link letter from the mount's real flight path, exactly as the animation chooser does. Kept
    // in step with CGUnit_C::ChooseMountFlightAnim deliberately: the server's speed and the mount's clip
    // must never disagree about which state the player is in.
    char ChooseState(const CGUnit_C* unit, bool* diving, bool* glideOn, uint32_t nowMs)
    {
        *diving = false;
        *glideOn = false;

        const uint32_t flags = unit->m_movementData.m_flags;

        if (!(flags & MOVEMENTFLAG_FLYING))
        {
            return kStateLevel;
        }

        if (fabsf(unit->m_movementData.m_currentSpeed) < 0.5f)
        {
            return kStateHover;
        }

        // Strafe and reverse are level travel, and they must be decided BEFORE the angle -- exactly as
        // CGUnit_C::ChooseMountFlightAnim does, or the mount would play a climb or dive clip while the server
        // was told 'L' and the two would disagree about what the player is doing.
        //
        // The link letter has no lateral value: the server only distinguishes climb / dive / hover / level,
        // and a sidestep is level flight with no vertical component worth chasing.
        if (flags & (MOVEMENTFLAG_STRAFE_LEFT | MOVEMENTFLAG_STRAFE_RIGHT | MOVEMENTFLAG_BACKWARD))
        {
            *glideOn = true;
            return kStateLevel;
        }

        const float pathSin = unit->m_movementData.m_pitchSin;

        // The mount's clip chooser reads the same two thresholds, so the state and the animation cannot disagree.
        const float climbSin = SpeedFlying::ClimbSin();
        const float diveSin  = SpeedFlying::DiveSin();

        // The flight-path angle decides; the ascend/descend bits only break a tie.
        //
        // Testing them as an OR made the state flap. The server leaves MOVEMENTFLAG_ASCENDING set across long
        // stretches while the mount is actually level or sinking, so C and L alternated on consecutive frames
        // -- the log shows C/L/C/L with the pitch sitting at -0.2 to -0.5 the whole time. Every flip restarts
        // the clip, which is why a climb or a boost never played through. The flags now only decide when the
        // angle is inside the level band, which is the case they exist for: climbing straight up leaves the
        // pitch at zero.
        char raw;

        if (pathSin > climbSin)
        {
            raw = kStateClimb;
        }
        else if (pathSin < diveSin)
        {
            raw = kStateDive;
        }
        else if (flags & MOVEMENTFLAG_ASCENDING)
        {
            raw = kStateClimb;
        }
        else if (flags & MOVEMENTFLAG_DESCENDING)
        {
            raw = kStateDive;
        }
        else
        {
            raw = kStateLevel;
        }

        // Debounce: a freshly seen state must persist for kHoldMs before it is applied. Every change
        // restarts the mount's clip and the server's blend, so reacting to single jittery frames is what
        // made the animation crawl and the speed stutter. Same idea, same 60 ms, as the reference's
        // HoldMs. Strafe/backward and hover never reach here -- they return above -- so a held key or a
        // measured stop is still immediate.
        static char candidate = 0;
        static char applied = kStateLevel;
        static uint32_t candidateSinceMs = 0;

        if (raw == applied)
        {
            candidate = raw;
            candidateSinceMs = nowMs;
        }
        else if (raw != candidate)
        {
            candidate = raw;
            candidateSinceMs = nowMs;
        }
        else if (nowMs - candidateSinceMs >= kHoldMs)
        {
            applied = candidate;
        }

        if (applied == kStateDive)
        {
            *diving = true;
        }

        *glideOn = (applied == kStateLevel);
        return applied;
    }

    // --- the HUD globals --------------------------------------------------------------------------

    void PublishHud(const CGUnit_C* unit, char stateLetter, bool advFly, bool diving, uint32_t nowMs)
    {
        const ServerSpeeds& speeds = s_state.m_speeds;

        // A reload leaves none of what these caches say was published; forget them so it all goes out again.
        static lua_State* seen = nullptr;

        if (NeedsRepublish(L(), seen))
        {
            s_state.m_uiState[0] = '\0';
            s_state.m_uiSpeedPct[0] = '\0';
            s_state.m_uiFill = -1.0f;
            s_state.m_uiDiveFrac = -1.0f;
            s_state.m_uiStacks = -1;
            s_state.m_uiMaxStacks = -1;
            s_state.m_uiFrameOn = false;
            s_state.m_uiDiving = false;
            s_state.m_uiGlideOn = false;
            s_state.m_uiSpaceBound = false;
            s_state.m_anchorDirty = true;
        }

        const bool flying = (unit->m_movementData.m_flags & MOVEMENTFLAG_FLYING) != 0;

        // The whole HUD is for AdvFly mounts only, like the reference: a stock flyer or a flight form gets
        // no readout, bar or glow. (The raw flying bit used to gate this, which lit it for every flyer.)
        if (advFly != s_state.m_uiFrameOn)
        {
            s_state.m_uiFrameOn = advFly;
            SetGlobalNumber("Manastorm_MountAnimFrameOn", advFly ? 1 : 0);
        }

        // The Diving callout and the state word are FlightStateText's, and the word is DebugHUD's on top.
        const bool calloutOn = g_debugHud && g_flightStateText && advFly && diving;

        if (calloutOn != s_state.m_uiDiving)
        {
            s_state.m_uiDiving = calloutOn;
            SetGlobalNumber("Manastorm_MountAnimDiving", calloutOn ? 1 : 0);
        }

        const char* word = "";

        if (g_debugHud && g_flightStateText && advFly)
        {
            switch (stateLetter)
            {
                case kStateClimb: word = "Climbing"; break;
                case kStateDive:  word = "Diving";   break;
                case kStateHover: word = "Hovering"; break;
                default:          word = "Leveling"; break;
            }
        }

        if (strcmp(word, s_state.m_uiState) != 0)
        {
            SStrCopy(s_state.m_uiState, word, sizeof(s_state.m_uiState));
            SetGlobalString("Manastorm_MountAnimState", word);
        }

        // Live speed as a percentage of unbuffed run speed, the same scale the server's own keys use.
        //
        // m_currentSpeed is yards/second, while the server's keys (280 / 600 / 1200) are percentages of
        // the 7 yd/s unbuffed run speed -- which is exactly what the reference divides by, a hard 7.0.
        // Using m_runSpeed here was wrong: that field carries the character's BASE run rate after
        // mount/aura modifiers, so dividing by it silently re-normalised the answer and a 600% dive could
        // read as 280.
        constexpr float kBaseRunSpeed = 7.0f;

        const float pct = (fabsf(unit->m_movementData.m_currentSpeed) / kBaseRunSpeed) * 100.0f;

        {
            static float lastLogged = -999.0f;

            if (fabsf(pct - lastLogged) > 25.0f)
            {
                lastLogged = pct;
                Log("speedflying: speed cur=%.2f run=%.2f flight=%.2f pct=%.0f",
                                       unit->m_movementData.m_currentSpeed, unit->m_movementData.m_runSpeed,
                                       unit->m_movementData.m_flightSpeed, pct);
            }
        }

        // The speed line is DebugHUD's; blank otherwise, and the addon draws whatever it is given.
        char pctText[16] = {};

        if (g_debugHud && advFly)
        {
            SStrPrintf(pctText, sizeof(pctText), "%.0f%%", pct);
        }

        if (strcmp(pctText, s_state.m_uiSpeedPct) != 0)
        {
            SStrCopy(s_state.m_uiSpeedPct, pctText, sizeof(s_state.m_uiSpeedPct));
            SetGlobalString("Manastorm_MountAnimSpeedPct", pctText);
        }

        // Bar fill: the left half spans the plain glide up to the dive ceiling, the right half the dive
        // ceiling to the boost ceiling -- exactly the reference's split, so a normal dive fills half.
        //
        // Three details that decide whether the bar moves at all, all taken from the reference's
        // BarFracForPct rather than guessed:
        //   - The empty end is levelPct PLUS a dead band. Without it the bar sits on zero through the
        //     whole ordinary glide, because the glide IS the level speed.
        //   - Under the dead band the fill is a true 0, and only above it does it climb.
        //   - The split point is the dive ceiling (levelPct is the empty end, not the middle).
        constexpr float kBarDeadbandPct = 25.0f;

        const float level = speeds.m_levelPct > 1.0f ? speeds.m_levelPct : 280.0f;
        const float dive  = speeds.m_divePct  > level ? speeds.m_divePct : level;
        const float boost = speeds.m_boostPct > dive ? speeds.m_boostPct : dive;
        const float zero  = level + kBarDeadbandPct;

        float fill = 0.0f;

        if (dive > zero && boost > dive)
        {
            if (pct > zero)
            {
                fill = (pct <= dive)
                    ? 0.5f * ((pct - zero) / (dive - zero))
                    : 0.5f + 0.5f * ((pct - dive) / (boost - dive));
            }
        }
        else
        {
            fill = pct > zero ? 1.0f : 0.0f;
        }

        if (fill < 0.0f)
        {
            fill = 0.0f;
        }

        if (fill > 1.0f)
        {
            fill = 1.0f;
        }

        // Nothing to show off an AdvFly mount or off the wing: a stock flyer at 310% would otherwise light the
        // bar, and the speed left over after landing would hold it up.
        if (!advFly || !flying)
        {
            fill = 0.0f;
        }

        // "Gliding with bonus speed": level travel faster than the plain glide, i.e. still carrying speed out
        // of a dive. It drives the label and the bar's glow pulse, so it must be false (not merely quiet) the
        // rest of the time.
        const bool glideOn = fill > 0.0f && stateLetter == kStateLevel;

        if (glideOn != s_state.m_uiGlideOn)
        {
            s_state.m_uiGlideOn = glideOn;
            SetGlobalNumber("Manastorm_MountAnimGlideOn", glideOn ? 1 : 0);
        }

        if (fabsf(fill - s_state.m_uiFill) > 0.005f || (fill == 0.0f && s_state.m_uiFill != 0.0f))
        {
            s_state.m_uiFill = fill;
            SetGlobalNumber("Manastorm_MountAnimFill", fill);
            Log("hud: fill=%.3f pct=%.0f level=%.0f dive=%.0f boost=%.0f",
                                   fill, pct, level, dive, boost);
        }

        // Where the boost tint starts: the dive ceiling's position on the same curve. The dive ceiling
        // sits at exactly half by construction, so the tint begins at the bar's midpoint.
        if (fabsf(0.5f - s_state.m_uiDiveFrac) > 0.005f)
        {
            s_state.m_uiDiveFrac = 0.5f;
            SetGlobalNumber("Manastorm_MountAnimDiveFrac", 0.5);
        }

        // The orb row shows the same count a SPACE press is judged on, so the two cannot disagree.
        const int32_t shownStacks = EffectiveStacks(nowMs);

        if (shownStacks != s_state.m_uiStacks)
        {
            s_state.m_uiStacks = shownStacks;
            SetGlobalNumber("Manastorm_MountAnimStacks", s_state.m_uiStacks);
        }

        if (static_cast<int32_t>(speeds.m_maxStacks) != s_state.m_uiMaxStacks)
        {
            s_state.m_uiMaxStacks = static_cast<int32_t>(speeds.m_maxStacks);
            SetGlobalNumber("Manastorm_MountAnimMaxStacks", s_state.m_uiMaxStacks);
        }
    }

    // The HUD's own settings, published once. These are the reference INI's Hud / HudX / HudY / DebugHUD
    // keys; the addon reads them back and hides the frame when Hud is off.
    void PublishHudSettings()
    {
        static lua_State* seen = nullptr;

        if (!NeedsRepublish(L(), seen))
        {
            return;
        }

        SetGlobalNumber("Manastorm_MountAnimHudOn", 1);   // Hud=1
        SetGlobalNumber("Manastorm_MountAnimHudX", 0);    // HudX=0
        SetGlobalNumber("Manastorm_MountAnimHudY", -200); // HudY=-200, a little below centre
        SetGlobalNumber("Manastorm_MountAnimDbgHud", g_debugHud ? 1 : 0);
        SetGlobalNumber("Manastorm_MountAnimWords", g_flightStateText ? 1 : 0);
        SetGlobalNumber("Manastorm_SF", 1);
    }

    // Pushes the server-owned constants the addon needs once, and again whenever they change.
    void PublishConstants()
    {
        static uint32_t pushedMaxStacks = 0;
        static uint32_t pushedStackMs = 0;
        static uint32_t pushedStackDiveMs = 0;
        static lua_State* seen = nullptr;

        if (NeedsRepublish(L(), seen))
        {
            pushedMaxStacks = 0;
            pushedStackMs = 0;
            pushedStackDiveMs = 0;
        }

        const ServerSpeeds& speeds = s_state.m_speeds;

        if (speeds.m_maxStacks != pushedMaxStacks)
        {
            pushedMaxStacks = speeds.m_maxStacks;
            SetGlobalNumber("Manastorm_MountAnimMaxStacks", static_cast<double>(speeds.m_maxStacks));
        }

        if (speeds.m_stackMs != pushedStackMs)
        {
            pushedStackMs = speeds.m_stackMs;
            SetGlobalNumber("Manastorm_MountAnimStackMs", static_cast<double>(speeds.m_stackMs));
        }

        if (speeds.m_stackDiveMs != pushedStackDiveMs)
        {
            pushedStackDiveMs = speeds.m_stackDiveMs;
            SetGlobalNumber("Manastorm_MountAnimStackDiveMs", static_cast<double>(speeds.m_stackDiveMs));
        }

        // The accrual anchor belongs to the SERVER's clock and is pushed only when an announce arrives --
        // rewriting it every frame reset the addon's interpolation and the recharge bar never advanced.
        if (s_state.m_anchorDirty && s_state.m_announceTime > 0.0)
        {
            s_state.m_anchorDirty = false;
            SetGlobalNumber("Manastorm_MountAnimOrbAccum", static_cast<double>(speeds.m_stackAccumMs));
            // GetTime() seconds, the clock Hud.lua measures the recharge against -- not OS milliseconds.
            SetGlobalNumber("Manastorm_MountAnimOrbAnchorAt", s_state.m_announceTime);
        }
    }

    // --- the link ---------------------------------------------------------------------------------

    void SendStateMessage(const CGUnit_C* unit, char stateLetter, bool advFly, uint32_t nowMs)
    {
        char body[16];
        SStrPrintf(body, sizeof(body), "%c%c", advFly ? '1' : '0', stateLetter);

        SendLink("WXLMS", body);

        s_state.m_sentAdvFly = advFly;
        s_state.m_sentState = stateLetter;
        s_state.m_lastSendMs = nowMs;
        s_state.m_lastAnyMs = nowMs;

        (void)unit;
    }

    // A boost is a one-shot: the server spends a stack and decides the speed, so the client only asks.
    void SendBoost(bool advFly, uint32_t nowMs)
    {
        if (!advFly)
        {
            return;
        }

        // No 200 ms floor here. A boost is not a state reiteration -- the server deliberately exempts it
        // from its own flood guard because each press spends a stack and must be answered. Throttling it
        // client-side is what made a held-stack press silently do nothing.
        SendLink("WXLMS", "1B");

        s_state.m_lastSendMs = nowMs;
        s_state.m_lastAnyMs = nowMs;
    }

    // The stacks held right now: the server's last announce less anything spent here since. The server
    // announces on every grant, every spend and every ceiling change, so the count is never extrapolated.
    int32_t EffectiveStacks(uint32_t /*nowMs*/)
    {
        const ServerSpeeds& speeds = s_state.m_speeds;

        if (!speeds.m_received || speeds.m_maxStacks == 0)
        {
            return 0;
        }

        int32_t stacks = s_state.m_stacksBase;

        if (stacks < 0)
        {
            stacks = 0;
        }

        if (stacks > static_cast<int32_t>(speeds.m_maxStacks))
        {
            stacks = static_cast<int32_t>(speeds.m_maxStacks);
        }

        return stacks;
    }

    // Records a local spend so the count drops at once instead of waiting for the server's answer, and
    // arms the window in which an echo minted before that answer is ignored.
    void NoteLocalSpend(uint32_t nowMs)
    {
        const int32_t stacks = EffectiveStacks(nowMs);

        s_state.m_stacksBase = stacks > 0 ? stacks - 1 : 0;
        s_state.m_lastSpendMs = nowMs ? nowMs : 1;
    }

    // The talent panel's requests, raised by the addon through Manastorm_TalentToggle / Manastorm_TalentConfirm / etc.
    // The panel writes the intent into a global; this picks it up so the addon needs no outbound path of
    // its own.
    // Shows or hides the talent panel. Talents.lua builds Manastorm_TalentFrame on its first Update once
    // Manastorm_TalentD exists, and wires the close button, but nothing in it ever OPENS the frame -- that was the
    // module's job and it was missing.
    // Calls a zero-argument global function, returning false if it is absent or errors. Used to hand the
    // toggle to the addon so it decides between the talent window's Riding tab and the standalone panel.
    bool CallGlobalFunction(const char* name)
    {        lua_State* lua = L();

        if (!lua || !name)
        {
            return false;
        }

        lua_getglobal(lua, name);

        if (!lua_isfunction(lua, -1))
        {
            lua_pop(lua, 1);
            return false;
        }

        if (lua_pcall(lua, 0, 0, 0) != 0)
        {
            lua_pop(lua, 1);
            return false;
        }

        return true;
    }

    void ToggleTalentPanel(bool forceOpen)
    {
        lua_State* lua = L();

        if (!lua)
        {
            return;
        }

        // The addon exposes Manastorm_OpenRiding, which opens the talent window on the Riding tab when that tab
        // exists and falls back to the standalone panel otherwise. Doing the decision there keeps the frame
        // handling in Lua, where the frames actually live.
        if (CallGlobalFunction(forceOpen ? "Manastorm_OpenRiding" : "Manastorm_ToggleRiding"))
        {
            return;
        }

        lua_getglobal(lua, "Manastorm_TalentFrame");

        if (!lua_istable(lua, -1) && !lua_isuserdata(lua, -1))
        {
            lua_pop(lua, 1);
            return;
        }

        bool shown = false;

        // lua_pcall, never lua_call: an error inside Show/Hide IsShown longjmps, and there is nothing here to
        // unwind it, so an unprotected call would corrupt the Lua stack and take the client down.
        lua_getfield(lua, -1, "IsShown");

        if (lua_isfunction(lua, -1))
        {
            lua_pushvalue(lua, -2);

            if (lua_pcall(lua, 1, 1, 0) == 0)
            {
                shown = lua_toboolean(lua, -1) != 0;
            }

            lua_pop(lua, 1);
        }
        else
        {
            lua_pop(lua, 1);
        }

        const char* method = (forceOpen || !shown) ? "Show" : "Hide";

        lua_getfield(lua, -1, method);

        if (lua_isfunction(lua, -1))
        {
            lua_pushvalue(lua, -2);

            if (lua_pcall(lua, 1, 0, 0) != 0)
            {
                lua_pop(lua, 1);
            }
        }
        else
        {
            lua_pop(lua, 1);
        }

        lua_pop(lua, 1);
    }

    // Calls a zero-argument global function for its side effect only (AscendStop). An absent global is not
    // an error: an older UI simply has nothing to stop.
    void CallGlobalVoid(const char* name)
    {
        lua_State* lua = L();

        if (!lua || !name)
        {
            return;
        }

        lua_getglobal(lua, name);

        if (!lua_isfunction(lua, -1))
        {
            lua_pop(lua, 1);
            return;
        }

        if (lua_pcall(lua, 0, 0, 0) != 0)
        {
            lua_pop(lua, 1);
        }
    }

    // OsInputIsKeyDown wraps GetAsyncKeyState, so it reports a key held with the client unfocused or while
    // an edit box has focus. Anything keyed off it (SHIFT+T, SPACE) is gated on this, otherwise a space
    // typed into chat spends a real server-side stack.
    bool GameHasKeyInput()
    {
        return OsInputIsAppActive() != 0 && CSimpleEditBox::s_currentFocus == nullptr;
    }

    void PollTalentRequests(uint32_t nowMs)
    {
        static uint32_t lastToggle = 0;

        double flag = 0.0;

        if (ReadGlobalNumber("Manastorm_TalentToggle", &flag) && flag != 0.0)
        {
            // Only consumed when it is acted on: clearing it inside the throttle window dropped the click
            // entirely, so the button appeared dead.
            if (nowMs - lastToggle > 250)
            {
                lastToggle = nowMs;
                SetGlobalNumber("Manastorm_TalentToggle", 0);
                SendLink("WXLTT", "Q");
                ToggleTalentPanel(false);
            }
        }

        // SHIFT+T, the reference's binding. Edge-triggered so holding it does not strobe the panel.
        static bool comboDown = false;
        const bool shift = (OsInputIsKeyDown(KEY_LSHIFT) != 0) || (OsInputIsKeyDown(KEY_RSHIFT) != 0);
        const bool t = OsInputIsKeyDown(KEY_T) != 0;
        const bool combo = shift && t && GameHasKeyInput();

        if (combo && !comboDown && nowMs - lastToggle > 250)
        {
            lastToggle = nowMs;
            SetGlobalNumber("Manastorm_TalentToggle", 0);
            SendLink("WXLTT", "Q");
            ToggleTalentPanel(false);
        }

        comboDown = combo;

        // The panel's Reset / Confirm clicks and the staged ranks belong to TalentUI, which queues what it needs
        // sent in Manastorm_TalentSend (Q, B\t..., R\tA); this puts it on the wire.
        char buffer[512];

        if (ReadGlobalString("Manastorm_TalentSend", buffer, sizeof(buffer)) && buffer[0])
        {
            // Rate-limited like everything else, and cleared only once sent: the addon may leave the global
            // set, and without this a non-empty value resent every frame.
            if (nowMs - lastToggle > 200)
            {
                lastToggle = nowMs;
                SendLink("WXLTT", buffer);
                Log("talent: send %s", buffer);
                SetGlobalString("Manastorm_TalentSend", "");
            }
        }
    }

    // SPACE is the boost key -- but only while a stack is held and the mount is link-eligible. Jumping is the
    // same key, so the order matters: the reference consumes it while flying, which is what this does.
    //
    // Runs every frame, mounted or not: the key edges must be tracked through a dismount (a stale "down" made
    // the next mount's first press look like a hold), and the override binding must be able to come OFF on
    // foot -- returning early for a non-AdvFly mount left SPACE swallowed, so the player could not jump.
    void UpdateSpace(CGUnit_C* unit, bool advFly, uint32_t timeMs)
    {
        const bool acceptInput = GameHasKeyInput();

        static bool spaceDown = false;
        const bool spaceNow = OsInputIsKeyDown(KEY_SPACE) != 0;

        const bool spacePressed = spaceNow && !spaceDown && acceptInput;
        const bool spaceReleased = !spaceNow && spaceDown && acceptInput;
        spaceDown = spaceNow;

        // SPACE must boost, never ascend. The reference swallows the key with an override binding, which
        // is the only thing that stops the base game's own ascend from firing -- polling the key alone
        // cannot, because the stock binding fires on the down-edge before this code runs. The binding may
        // only be swapped while the key is UP: changing it mid-press latches the ascend on permanently.
        const bool flying = (unit->m_movementData.m_flags & MOVEMENTFLAG_FLYING) != 0;
        const int32_t stacks = EffectiveStacks(timeMs);
        const bool wantSpaceBound = advFly && flying && stacks > 0;

        if (!spaceNow && wantSpaceBound != s_state.m_uiSpaceBound)
        {
            s_state.m_uiSpaceBound = wantSpaceBound;
            SetGlobalNumber("Manastorm_MountAnimSpaceBound", wantSpaceBound ? 1 : 0);
        }

        if (spacePressed && advFly && flying)
        {
            if (s_state.m_uiSpaceBound && s_state.m_spaceTapMs && timeMs - s_state.m_spaceTapMs <= kDoubleTapMs)
            {
                // A quick second tap is a take-off, not a second boost: hand the held key to the mount's
                // own ascend. The release below stops it again.
                s_state.m_spaceTapMs = 0;
                CallGlobalVoid("JumpOrAscendStart");
            }
            else
            {
                s_state.m_spaceTapMs = timeMs ? timeMs : 1;

                if (stacks > 0)
                {
                    SendBoost(advFly, timeMs);
                    s_state.m_lastBoostMs = timeMs;

                    // Force the flap clip from its start. Without this a chained burst plays nothing new,
                    // and a burst during a climb or dive never shows at all.
                    unit->m_flgtOneShot = 0;
                    unit->m_flgtBoostRequestMs = timeMs ? timeMs : 1;

                    NoteLocalSpend(timeMs);
                }
            }
        }

        // Every SPACE release stops the ascend, even when this module did not start it. While the override
        // owns the key the base game never sees the key-up that calls AscendStop, and a bind swap under a
        // press can drop that key-up too. AscendStop is idempotent. Only while the override can be
        // involved, so plain jumping on foot is left to the stock bindings.
        if (spaceReleased && (advFly || s_state.m_uiSpaceBound))
        {
            CallGlobalVoid("AscendStop");
        }

        // D5 Aftershock: releasing the boost key while the burst is still fading lets a diver commit it at
        // once. The server checks the rank itself, so sending it unranked costs nothing.
        if (spaceReleased && advFly && flying && s_state.m_lastBoostMs
            && timeMs - s_state.m_lastBoostMs < s_state.m_speeds.m_boostFadeMs)
        {
            SendLink("WXLMS", "1X");
            s_state.m_lastSendMs = timeMs;
            s_state.m_lastAnyMs = timeMs;
        }
    }

    // --- Esteria: skyriding abilities and visuals -----------------------------------------------------
    // Carried over from the old Esteria skyriding (tag skyriding-final, Extensions/Skyriding). The four abilities
    // are action-bar spells the server checks and pays for (mod-mana-speedflying spell_speedflying_ability); its
    // "A:<letter>" reply lands in Manastorm_AbilityQueue. The lifts hold the stock ascend for a set time, so the
    // climb is normal flight the server sees as such; Whirling Surge's speed is the server's boost.
    constexpr uint32_t kLiftOffMs = 1100;     // Lift Off: ascend from the ground for this long
    constexpr uint32_t kAscentMs  = 1200;     // Skyward Ascent
    constexpr uint32_t kFlapMs    = 300;      // Flap
    constexpr uint32_t kLaunchWaitMs = 500;   // Lift Off waits this long for the flying flag before it gives up
    constexpr uint32_t kRollMs    = 900;      // one full Whirling Surge barrel roll
    constexpr uint8_t  kOneShotAscent = 1, kOneShotRoll = 2, kOneShotFlap = 3; // CGUnit_C::m_flgtOneShot

    // Retail's speed aura (visual kit 159809, docs/archive/SKYRIDING_RETAIL_ASSETS.md) and Surge Forward's impact.
    struct WindPart
    {
        uint32_t fileId;
        uint32_t loopAnim;
        bool     camera;                      // positioner 1797: between camera and rider, facing the view
        float    scale, yaw, pitch, roll, raise;
    };

    constexpr float kPi = 3.1415927f;
    constexpr uint32_t kWindEndAnim = 159;
    constexpr uint32_t kWindLingerMs = 800;   // the end animation plays this long before the models go
    constexpr WindPart kWindParts[] = {
        { 4579907, 158, true, 1.25f, 0.0f, 0.0f, 0.0f, 0.0f },              // FX_Speed4_Aura: the screen tunnel
        { 4579908, 214, true, 1.25f, 0.0f, 0.0f, 0.0f, 0.0f },              // FX_Speed4_Aura01: its particles
        // FX_Speed4_Aura02 streamers. shortcut: retail places them between body points 60-63; they ride the mount
        // root with the kit's own angles instead.
        { 4640283, 0, false, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f },
        { 4640283, 0, false, 1.0f, 0.0f, 0.0f, kPi, 0.16f },
        { 4640283, 0, false, 1.0f, -0.3490658f, -0.0872665f, 0.0f, 0.0f },
        { 4640283, 0, false, 1.0f, 0.3490658f, -0.0872665f, kPi, 0.16f },
    };
    constexpr size_t kWindCount = sizeof(kWindParts) / sizeof(kWindParts[0]);
    constexpr uint32_t kImpactFileId = 4579917, kImpactAttachment = 17; // FX_SurgeForward_Impact
    constexpr uint32_t kImpactMs = 1500;      // shortcut: fixed lifetime, the model is not loaded when it starts

    struct Skyriding
    {
        uint32_t  ascendUntil = 0;            // holding the stock ascend until then; 0 = not
        uint32_t  ascendPendingMs = 0;        // a lift waits for the flying flag; its length
        uint32_t  launchAt = 0;               // when Lift Off asked for flight
        uint32_t  rollStart = 0;              // Whirling Surge spin start; 0 = not spinning
        bool      windWanted = false;         // set by Update, applied by OnLocalTransform
        CM2Model* wind[kWindCount] = {};
        CM2Model* windParent = nullptr;
        uint32_t  windEndAt = 0;
        bool      impactPending = false;
        CM2Model* impact = nullptr;
        uint32_t  impactEnd = 0;
        float     bank = 0.0f;                // lean into turns, radians, positive = left wing down
        float     lastFacing = 0.0f;
        uint32_t  lastMs = 0;
    };

    Skyriding s_sky;

    C44Matrix WindOffset(const WindPart& part)
    {
        const float cy = cosf(part.yaw), sy = sinf(part.yaw);
        const float cp = cosf(part.pitch), sp = sinf(part.pitch);
        const float cr = cosf(part.roll), sr = sinf(part.roll);
        // Row vectors (v * M): roll about forward (x), then pitch about left (y), then yaw about up (z).
        const float k = part.scale;
        C44Matrix m;
        m.Identity();
        m.a0 = cp * cy * k;                   m.a1 = cp * sy * k;                   m.a2 = -sp * k;
        m.b0 = (sr * sp * cy - cr * sy) * k;  m.b1 = (sr * sp * sy + cr * cy) * k;  m.b2 = sr * cp * k;
        m.c0 = (cr * sp * cy + sr * sy) * k;  m.c1 = (cr * sp * sy - sr * cy) * k;  m.c2 = cr * cp * k;
        m.d2 = part.raise;
        return m;
    }

    void ReleaseModel(CM2Model*& model)
    {
        if (!model)
        {
            return;
        }

        if (model->m_attachParent)
        {
            model->DetachFromParent();
        }

        model->Release();
        model = nullptr;
    }

    void DestroyWind()
    {
        for (CM2Model*& model : s_sky.wind)
        {
            ReleaseModel(model);
        }

        s_sky.windParent = nullptr;
        s_sky.windEndAt = 0;
    }

    void UpdateWind(CM2Model* parent, bool on, uint32_t now)
    {
        // A new mount model (remount, display change) takes fresh effects.
        if (s_sky.windParent && s_sky.windParent != parent)
        {
            DestroyWind();
        }

        if (on && parent && !s_sky.windParent)
        {
            char path[32];

            for (size_t i = 0; i < kWindCount; ++i)
            {
                SStrPrintf(path, sizeof(path), "retail\\%u.m2", kWindParts[i].fileId);
                CM2Model* model = parent->m_scene->CreateModel(path, 0);

                if (!model)
                {
                    continue;
                }

                model->AttachToParent(parent, 0, nullptr, 1);
                // An attached model's world transform is its offset from the parent; camera parts are placed
                // every frame by PlaceCameraWind.
                model->m_worldTransform = WindOffset(kWindParts[i]);
                model->SetBoneSequence(0xFFFFFFFF, kWindParts[i].loopAnim, 0xFFFFFFFF, 0, 1.0f, 0, 1);
                s_sky.wind[i] = model;
            }

            s_sky.windParent = parent;
        }
        else if (on && s_sky.windEndAt)
        {
            for (size_t i = 0; i < kWindCount; ++i)
            {
                if (s_sky.wind[i])
                {
                    s_sky.wind[i]->SetBoneSequence(0xFFFFFFFF, kWindParts[i].loopAnim, 0xFFFFFFFF, 0, 1.0f, 1, 1);
                }
            }

            s_sky.windEndAt = 0;
        }
        else if (!on && s_sky.windParent && !s_sky.windEndAt)
        {
            for (CM2Model* model : s_sky.wind)
            {
                if (model)
                {
                    model->SetBoneSequence(0xFFFFFFFF, kWindEndAnim, 0xFFFFFFFF, 0, 1.0f, 1, 1);
                }
            }

            s_sky.windEndAt = now + kWindLingerMs;
        }
        else if (!on && s_sky.windEndAt && static_cast<int32_t>(now - s_sky.windEndAt) >= 0)
        {
            DestroyWind();
        }
    }

    // Positioner 1797: halfway between the camera and the rider, turned with the camera, so the tunnel fills the
    // view. The offset is expressed in the mount's frame, after its world transform is final for the frame.
    void PlaceCameraWind(CGUnit_C* unit)
    {
        CGCamera* camera = CGWorldFrame::GetActiveCamera();

        if (!s_sky.windParent || !camera)
        {
            return;
        }

        const C3Vector eye = camera->m_position, rider = unit->GetPosition();
        const C3Vector forward = camera->Forward(), right = camera->Right(), up = camera->Up();

        for (size_t i = 0; i < kWindCount; ++i)
        {
            if (!s_sky.wind[i] || !kWindParts[i].camera)
            {
                continue;
            }

            const float k = kWindParts[i].scale;
            C44Matrix world;
            world.Identity();
            world.a0 = forward.x * k; world.a1 = forward.y * k; world.a2 = forward.z * k;
            world.b0 = -right.x * k;  world.b1 = -right.y * k;  world.b2 = -right.z * k;
            world.c0 = up.x * k;      world.c1 = up.y * k;      world.c2 = up.z * k;
            world.d0 = (eye.x + rider.x) * 0.5f; world.d1 = (eye.y + rider.y) * 0.5f; world.d2 = (eye.z + rider.z) * 0.5f;
            s_sky.wind[i]->m_worldTransform = world * s_sky.windParent->m_worldTransform.Inverse();
        }
    }

    void UpdateImpact(CM2Model* mount, uint32_t now)
    {
        if (s_sky.impact && (static_cast<int32_t>(now - s_sky.impactEnd) >= 0 || s_sky.impact->m_attachParent != mount))
        {
            ReleaseModel(s_sky.impact);
        }

        if (!s_sky.impactPending || !mount)
        {
            return;
        }

        s_sky.impactPending = false;
        ReleaseModel(s_sky.impact);

        char path[32];
        SStrPrintf(path, sizeof(path), "retail\\%u.m2", kImpactFileId);

        if (!(s_sky.impact = mount->m_scene->CreateModel(path, 0)))
        {
            return;
        }

        s_sky.impact->AttachToParent(mount, kImpactAttachment, nullptr, 1);
        s_sky.impact->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 0, 1);
        s_sky.impactEnd = now + kImpactMs;
    }

    // Turns the model about its own forward axis (rows b = left and c = up turn about row a).
    void RollModel(C44Matrix& m, float angle)
    {
        const float c = cosf(angle), s = sinf(angle);
        const C3Vector left(m.b0, m.b1, m.b2), up(m.c0, m.c1, m.c2);
        m.b0 = left.x * c - up.x * s; m.b1 = left.y * c - up.y * s; m.b2 = left.z * c - up.z * s;
        m.c0 = up.x * c + left.x * s; m.c1 = up.y * c + left.y * s; m.c2 = up.z * c + left.z * s;
    }

    void StopAscend()
    {
        if (s_sky.ascendUntil)
        {
            s_sky.ascendUntil = 0;
            CallGlobalVoid("AscendStop");
        }
    }

    void RequestOneShot(CGUnit_C* unit, uint8_t oneShot, uint32_t timeMs)
    {
        unit->m_flgtOneShot = oneShot;
        unit->m_flgtBoostRequestMs = timeMs ? timeMs : 1;
    }

    // One granted ability, from the server's "A:<letter>".
    void UseAbility(CGUnit_C* unit, char letter, uint32_t timeMs)
    {
        Log("skyriding: ability %c", letter);

        switch (letter)
        {
            case 'L':
                // The flying flag first; the ascend starts once it is set (a press before then would jump).
                unit->OnFlightLocal(timeMs, 1);
                s_sky.launchAt = timeMs ? timeMs : 1;
                s_sky.ascendPendingMs = kLiftOffMs;
                break;
            case 'U':
                s_sky.ascendPendingMs = kAscentMs;
                RequestOneShot(unit, kOneShotAscent, timeMs);
                break;
            case 'W':
                s_sky.rollStart = timeMs ? timeMs : 1;
                s_sky.impactPending = true;
                RequestOneShot(unit, kOneShotRoll, timeMs);
                break;
            case 'F':
                s_sky.ascendPendingMs = kFlapMs;
                RequestOneShot(unit, kOneShotFlap, timeMs);
                break;
            default:
                break;
        }
    }

    void UpdateSkyriding(CGUnit_C* unit, bool advFly, char stateLetter, uint32_t timeMs)
    {
        char queue[32];

        if (ReadGlobalString("Manastorm_AbilityQueue", queue, sizeof(queue)) && queue[0])
        {
            SetGlobalString("Manastorm_AbilityQueue", "");

            for (const char* c = queue; *c; ++c)
            {
                UseAbility(unit, *c, timeMs);
            }
        }

        const bool flying = (unit->m_movementData.m_flags & MOVEMENTFLAG_FLYING) != 0;
        const bool mounted = unit->m_mountModel != nullptr;

        // The HUD's Surge Forward button (Skyriding.lua): the same boost a SPACE press spends a stack on.
        double surge = 0.0;

        if (ReadGlobalNumber("Manastorm_SurgeRequest", &surge) && surge != 0.0)
        {
            SetGlobalNumber("Manastorm_SurgeRequest", 0);

            if (advFly && flying && EffectiveStacks(timeMs) > 0)
            {
                SendBoost(advFly, timeMs);
                s_state.m_lastBoostMs = timeMs;
                RequestOneShot(unit, 0, timeMs);
                NoteLocalSpend(timeMs);
            }
        }

        if (s_sky.ascendPendingMs)
        {
            if (flying && mounted)
            {
                CallGlobalVoid("JumpOrAscendStart");
                s_sky.ascendUntil = timeMs + s_sky.ascendPendingMs;
                s_sky.ascendPendingMs = 0;
                s_sky.launchAt = 0;
            }
            else if (!mounted || !s_sky.launchAt || timeMs - s_sky.launchAt > kLaunchWaitMs)
            {
                s_sky.ascendPendingMs = 0; // a lift asked for on the ground, or Lift Off never got its flag
                s_sky.launchAt = 0;
            }
        }

        if (s_sky.ascendUntil)
        {
            // The native auto-land drops flight under 1.5 yd once flight is 2 s old; a lift keeps its clock fresh.
            CGUnit_C::s_flightStatusTime = timeMs;

            if (!flying || !mounted || static_cast<int32_t>(timeMs - s_sky.ascendUntil) >= 0)
            {
                StopAscend();
            }
        }

        // The wind tunnel shows at speed: a dive, or a boost past the dive ceiling.
        const float speedPct = (fabsf(unit->m_movementData.m_currentSpeed) / 7.0f) * 100.0f;
        const float dive = s_state.m_speeds.m_divePct > 1.0f ? s_state.m_speeds.m_divePct : 600.0f;
        s_sky.windWanted = advFly && flying && mounted && (stateLetter == kStateDive || speedPct >= dive * 0.9f);
    }
}

namespace SpeedFlying
{
    void OnLocalTransform(CGUnit_C* unit)
    {
        const uint32_t now = FrameTime::s_curTimeMs;
        float dt = s_sky.lastMs ? static_cast<float>(now - s_sky.lastMs) * 0.001f : 0.0f;
        s_sky.lastMs = now;
        dt = dt > 0.1f ? 0.1f : dt;

        CM2Model* mount = unit->m_mountModel;
        const bool flying = mount && (unit->m_movementData.m_flags & MOVEMENTFLAG_FLYING) != 0;

        UpdateWind(mount, s_sky.windWanted && flying, now);
        UpdateImpact(mount, now);

        CM2Model* model = unit->GetObjectModel();

        if (!flying || !model)
        {
            s_sky.bank = 0.0f;
            s_sky.rollStart = 0;
            s_sky.lastFacing = unit->m_movementData.m_facing;
            return;
        }

        // Lean into turns: the turn rate of the facing, more at speed, eased.
        float turn = unit->m_movementData.m_facing - s_sky.lastFacing;
        turn -= 6.2831853f * floorf((turn + kPi) / 6.2831853f);
        s_sky.lastFacing = unit->m_movementData.m_facing;
        const float speed = fabsf(unit->m_movementData.m_currentSpeed);
        float target = dt > 0.0f ? turn / dt * 0.4f * (speed < 40.0f ? speed / 40.0f : 1.0f) : 0.0f;
        target = target < -0.7f ? -0.7f : (target > 0.7f ? 0.7f : target);
        s_sky.bank += (target - s_sky.bank) * (1.0f - expf(-5.0f * dt));

        float roll = s_sky.bank;

        if (s_sky.rollStart)
        {
            const uint32_t into = now - s_sky.rollStart;

            if (into >= kRollMs)
            {
                s_sky.rollStart = 0;
            }
            else
            {
                // Ease in and out of one full turn so it starts and lands level.
                const float t = static_cast<float>(into) / static_cast<float>(kRollMs);
                roll += 2.0f * kPi * (t * t * (3.0f - 2.0f * t));
            }
        }

        if (fabsf(roll) >= 0.001f)
        {
            RollModel(model->m_worldTransform, roll);
        }

        PlaceCameraWind(unit);
    }
}

namespace
{
    // --- camera effects -----------------------------------------------------------------------------
    // The reference's dive field-of-view swell and dive screen shake, with the reference's default tuning
    // (its INI's FovBoostDeg / FovRampMs / ShakePct / ShakeFadeMs). The camera reads the two results through
    // SpeedFlying::CameraFovBoost / CameraShake; this only eases them toward their targets once per frame.
    constexpr float kFovBoostDeg = 25.0f;
    constexpr float kFovRampSec  = 1.0f;
    constexpr float kShakePct    = 0.03f;
    constexpr float kShakeFadeSec = 0.7f;
    constexpr float kShakeSwellSec = 0.25f;
    constexpr float kDegToRad    = 0.017453292f;

    struct CameraFx
    {
        float    m_fovDeg = 0.0f;    // current swell, degrees above the camera's own field of view
        float    m_shake = 0.0f;     // 0..1: how fast the dive is, eased
        uint32_t m_lastMs = 0;
    };

    CameraFx s_fx;

    float Clamp01(float v)
    {
        return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    }

    // Exponential ease toward target with time constant tau seconds, frame-rate independent.
    float EaseToward(float value, float target, float tau, float dt)
    {
        const float k = 1.0f - expf(-dt / (tau > 0.001f ? tau : 0.001f));
        return value + (target - value) * k;
    }

    void UpdateCameraFx(const CGUnit_C* unit, bool advFly, char stateLetter, uint32_t timeMs)
    {
        float dt = s_fx.m_lastMs ? static_cast<float>(timeMs - s_fx.m_lastMs) * 0.001f : 0.0f;
        s_fx.m_lastMs = timeMs;

        if (dt > 0.1f)
        {
            dt = 0.1f; // a hitch must not jump the ease
        }

        const bool flying = advFly && (unit->m_movementData.m_flags & MOVEMENTFLAG_FLYING) != 0;

        // 7 yards/s is the unbuffed run speed, the base the server's percentages are multiples of.
        const float speedPct = (fabsf(unit->m_movementData.m_currentSpeed) / 7.0f) * 100.0f;
        const float level = s_state.m_speeds.m_levelPct > 1.0f ? s_state.m_speeds.m_levelPct : 280.0f;
        const float dive  = s_state.m_speeds.m_divePct > level ? s_state.m_speeds.m_divePct : level;
        const float boost = s_state.m_speeds.m_boostPct;

        float fovTarget = 0.0f;
        float shakeTarget = 0.0f;

        if (flying)
        {
            // Steeper is faster, so the swell tracks how vertical the dive is: a shallow dive nudges it, a
            // near-vertical drop opens it all the way.
            if (stateLetter == kStateDive)
            {
                float sinP = unit->m_movementData.m_pitchSin;
                sinP = sinP < -1.0f ? -1.0f : (sinP > 1.0f ? 1.0f : sinP);
                const float pitchDeg = asinf(sinP) * 57.29578f;
                fovTarget = kFovBoostDeg * Clamp01(-pitchDeg / 60.0f);

                // Shake follows how FAST the mount falls, not how steep: nothing at the plain glide, all of
                // it at the dive ceiling, eased in so the low end stays calm.
                const float span = dive - level;

                if (span > 0.0f)
                {
                    const float t = Clamp01((speedPct - level) / span);
                    shakeTarget = t * t;
                }
            }

            // A boost is speed no plain state reaches: it opens the view like a steep dive, by how far it
            // carries the speed past the dive ceiling, and closes again as the burst fades.
            if (boost > dive)
            {
                const float bt = kFovBoostDeg * Clamp01((speedPct - dive) / (boost - dive));

                if (bt > fovTarget)
                {
                    fovTarget = bt;
                }
            }
        }

        s_fx.m_fovDeg = EaseToward(s_fx.m_fovDeg, fovTarget, kFovRampSec, dt);

        // Ease in fast, out slowly, so leaving the dive lets the tremble linger rather than cutting off.
        s_fx.m_shake = EaseToward(s_fx.m_shake, shakeTarget,
                                  shakeTarget < s_fx.m_shake ? kShakeFadeSec : kShakeSwellSec, dt);

        // The ease never reaches zero; snap the tail so the camera stops being touched at all.
        if (fovTarget == 0.0f && s_fx.m_fovDeg < 0.01f)
        {
            s_fx.m_fovDeg = 0.0f;
        }

        if (shakeTarget == 0.0f && s_fx.m_shake < 0.01f)
        {
            s_fx.m_shake = 0.0f;
        }
    }
}

namespace SpeedFlying
{
    // The two angle bridges are buffs. Still Air (140) lifts the climb threshold 15 -> 25 degrees, so a glide may
    // be steeper before it reads as a climb and the fast level state lasts longer. Tucked Wings (141) lowers the
    // dive threshold 75 -> 65 degrees, so a dive, the fastest state, starts sooner. Both ranks ride the tally.
    float ClimbSin()
    {
        return s_state.m_rankStillAir ? 0.4226f : 0.2588f;     // sin(25) : sin(15)
    }

    float DiveSin()
    {
        return s_state.m_rankTuckedWings ? -0.9063f : -0.9659f; // sin(-65) : sin(-75)
    }

    float CameraFovBoost()
    {
        return s_fx.m_fovDeg * kDegToRad;
    }

    // Full strength (ShakePct 1) is 0.2 rad, about 11.5 degrees, at the top of the speed range.
    float CameraShake()
    {
        return s_fx.m_shake * kShakePct * 0.2f;
    }

    void Initialize()
    {
        if (s_state.m_initialised)
        {
            return;
        }

        s_state.m_initialised = true;

        // The panel's tree is rendered from what the server announced, so ask for it once the world is up.
        // A Q before the player exists is answered with nothing, so this is repeated by Update until a
        // tally arrives.
    }

    void Update(uint32_t timeMs)
    {
        lua_State* lua = L();

        if (!lua)
        {
            return;
        }

        PollInbound(timeMs);
        PollTalentRequests(timeMs);
        TalentUI::Update(timeMs);

        CGPlayer_C* player = static_cast<CGPlayer_C*>(
            ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPEMASK_PLAYER, __FILE__, __LINE__));

        if (!player)
        {
            s_fx = CameraFx();
            return;
        }

        CGUnit_C* unit = static_cast<CGUnit_C*>(player);

        // Ask for the tree until the first tally arrives: the server answers a Q with a whole tree, and a Q
        // sent before the world was ready is simply answered with nothing.
        // A separate clock: SendStateMessage also touches m_lastAnyMs once a second, so sharing it meant this
        // condition could never come true while the flight link was live and a missing tally was never asked
        // for again.
        if (!s_state.m_talentBody[0] && timeMs - s_state.m_lastTalentQueryMs > 2000)
        {
            s_state.m_lastTalentQueryMs = timeMs;
            SendLink("WXLTT", "Q");
        }

        PublishConstants();

        // The link only drives a mount whose model carries the AdvFly block; that is what keeps the speed
        // change off stock mounts and druid flight forms. The animation side already resolved that, so the
        // cheapest honest test is whether the mount's flight chooser picked an AdvFly id.
        const bool onMount = unit->m_mountModel != nullptr;

        bool advFly = false;

        if (onMount)
        {
            const M2Data* data = unit->m_mountModel->m_shared ? unit->m_mountModel->m_shared->m_data : nullptr;

            if (data)
            {
                const uint32_t advFlyIds[] = { 1532, 1722, 1724, 1524, 1520, 1522, 1530, 1704, 1678 };

                for (uint32_t id : advFlyIds)
                {
                    if (unit->m_mountModel->HasSequence(data, id))
                    {
                        advFly = true;
                        break;
                    }
                }
            }
        }

        bool diving = false;
        bool unusedGlide = false; // the HUD derives "gliding with bonus speed" from the bar, not from the state
        const char stateLetter = ChooseState(unit, &diving, &unusedGlide, timeMs);

        PublishHudSettings();

        // Manastorm_MountAnimAdvFly gates every speed effect in the HUD, and DiveNow switches the orb row to the
        // faster dive recharge cadence.
        static bool pushedAdvFly = false;
        static bool pushedDiveNow = false;
        static lua_State* seen = nullptr;

        if (NeedsRepublish(L(), seen))
        {
            pushedAdvFly = false;
            pushedDiveNow = false;
        }

        if (advFly != pushedAdvFly)
        {
            pushedAdvFly = advFly;
            SetGlobalNumber("Manastorm_MountAnimAdvFly", advFly ? 1 : 0);
        }

        if (diving != pushedDiveNow)
        {
            pushedDiveNow = diving;
            SetGlobalNumber("Manastorm_MountAnimDiveNow", diving ? 1 : 0);
        }

        // One line per state change: proves whether this whole path runs (it is gated on the active player
        // and on MOUNTED), what the mount resolved as, and what the HUD was told.
        {
            static char lastLetter = 0;
            static bool lastAdvFly = false;

            if (stateLetter != lastLetter || advFly != lastAdvFly)
            {
                lastLetter = stateLetter;
                lastAdvFly = advFly;

                Log("speedflying: mounted=%d advfly=%d state=%c speed=%.2f pitch=%.3f flags=0x%x",
                                       onMount ? 1 : 0, advFly ? 1 : 0, stateLetter,
                                       unit->m_movementData.m_currentSpeed,
                                       unit->m_movementData.m_pitchSin,
                                       unit->m_movementData.m_flags);
            }
        }

        PublishHud(unit, stateLetter, advFly, diving, timeMs);
        UpdateSpace(unit, advFly, timeMs);
        UpdateCameraFx(unit, advFly, stateLetter, timeMs);
        UpdateSkyriding(unit, advFly, stateLetter, timeMs);

        // A '0' link tells the server to restore the mount's own speed and stand down, so it is sent as soon
        // as the player is not on an AdvFly mount rather than being withheld.
        if (!advFly)
        {
            if (s_state.m_sentAdvFly && timeMs - s_state.m_lastSendMs >= kSendIntervalMs)
            {
                SendStateMessage(unit, kStateLevel, false, timeMs);
            }

            return;
        }

        if (stateLetter != s_state.m_sentState || timeMs - s_state.m_lastSendMs >= kHeartbeatMs)
        {
            if (timeMs - s_state.m_lastSendMs >= kSendIntervalMs)
            {
                SendStateMessage(unit, stateLetter, true, timeMs);
            }
        }

    }
}
