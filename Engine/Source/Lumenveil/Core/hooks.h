#pragma once

// Replaces Lumenveil's hooks.h (see README.md). The call-site patches are gone: the client calls Lumenveil's
// controller (Engine/Source/Lumenveil/Lumenveil.h) from its world render. The far clip lift is gone too: this
// client's World::ValidateFarClip already lets the farclip cvar reach World::kMaxFarClip, so FarClipMax is read
// and ignored, and these two are no-ops kept for regions.cpp.

void RequestFarClipRefresh();
void RefreshFarClipNow();

struct FogFrameStatus
{
    bool drawn;
    const char* reason;
};

FogFrameStatus LastFogFrameStatus();
