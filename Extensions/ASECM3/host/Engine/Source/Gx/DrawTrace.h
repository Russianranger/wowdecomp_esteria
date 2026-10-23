#pragma once
// Host shim: this host has no draw-source tracing, so the scope only names the caller.
enum class GxDrawSourceFamily { M3, Particle };
struct GxDrawSourceScope { explicit GxDrawSourceScope(GxDrawSourceFamily) {} };
