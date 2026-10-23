#pragma once
#include <cstdio>
// Host shim for the ASEC Lua diagnostic sink: appends to Logs\ASECM3-diagnostics.log and stderr.
inline void ASEC_LuaDiagnostic(void*, const char* text, int, int, int, const char* file, int line) {
    std::fprintf(stderr, "[ASECM3] %s (%s:%d)\n", text ? text : "", file ? file : "", line);
    if (FILE* log = std::fopen("Logs\ASECM3-diagnostics.log", "a")) {
        std::fprintf(log, "%s (%s:%d)\n", text ? text : "", file ? file : "", line);
        std::fclose(log);
    }
}
