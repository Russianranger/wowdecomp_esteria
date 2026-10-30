#ifndef ESTERIA_PLATFORM_LOCAL_OFFLINE_H
#define ESTERIA_PLATFORM_LOCAL_OFFLINE_H

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Explicit startup policy, independent of imported CVars. Header-only so asset
// preparation tools retain their normal behavior without linking the game.
namespace Esteria::LocalOffline {
inline std::atomic<bool> enabled{false};
inline bool Enabled() { return enabled.load(std::memory_order_acquire); }
inline void ConfigureAtStartup(bool value) {
    enabled.store(value, std::memory_order_release);
    if (value) std::fputs("ESTERIA_LOCAL_OFFLINE enabled; classic loopback login; remote downloads disabled\n", stderr);
}
inline bool Reject(const char* operation) {
    if (!Enabled()) return false;
    // Only controlled operation labels belong here, never URLs or credentials.
    std::fprintf(stderr, "ESTERIA_LOCAL_OFFLINE blocked: %s\n", operation);
    return true;
}
inline bool IsLiteralLoopbackHost(const char* host) {
    return host && std::strcmp(host, "127.0.0.1") == 0;
}
inline bool IsLiteralLoopbackEndpoint(const char* endpoint) {
    if (!endpoint || std::strncmp(endpoint, "127.0.0.1", 9)) return false;
    const char* port = endpoint + 9;
    if (!*port) return true;
    if (*port++ != ':' || !*port) return false;
    unsigned number = 0, digits = 0;
    for (; *port; ++port) {
        if (*port < '0' || *port > '9' || ++digits > 5) return false;
        number = number * 10 + static_cast<unsigned>(*port - '0');
    }
    return number > 0 && number <= 65535;
}
inline bool IsLoopbackAddressBytes(const void* address) {
    const auto* bytes = static_cast<const unsigned char*>(address);
    return bytes && bytes[0] == 127 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 1;
}
}
#endif
