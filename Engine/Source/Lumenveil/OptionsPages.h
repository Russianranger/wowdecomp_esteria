#ifndef ENGINE_SOURCE_LUMENVEIL_OPTIONSPAGES_H
#define ENGINE_SOURCE_LUMENVEIL_OPTIONSPAGES_H

#include <string>

// Not in the original 3.3.5a binary. Lumenveil's settings as pages of the Video options.
//
// Every setting the ported config has (Core/config.cpp's live settings) gets a cvar, "lv" and the ini key (lvDensity), kept off
// Config.wtf: CoAVolFog.ini stays the one place the values live. A cvar's callback applies its value to the running config at
// once; "lumenveilSave" writes the changed ones to the ini, comments kept (the settings window's Save), and "lumenveilSync"
// reads every cvar back from the config (the ini may have been edited, or the window reverted).
//
// The Lua that builds the pages is made from Lumenveil's own settings window (OptionsMeta.inc, written by
// Tools/LumenveilOptions/GenerateOptions.py): its sections, labels, formats and help. WoW/Source/UIUtil/DistanceOptions.cpp
// runs it after its page helpers exist.
namespace LumenveilOptions {

// The cvars; call once the config is loaded.
void RegisterCVars();

// Lua for the pages, to run after WowDecompBuildPage exists.
std::string BuildScript();

}

#endif
