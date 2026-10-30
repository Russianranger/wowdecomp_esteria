#ifndef ESTERIA_PLATFORM_LOCAL_ACCOUNT_HANDOFF_H
#define ESTERIA_PLATFORM_LOCAL_ACCOUNT_HANDOFF_H

#include <cstddef>
#include <cstring>

namespace Esteria::LocalAccount {
constexpr std::size_t InputBytes = 57;
enum class InputKind { Diagnostic, Login };

// Volatile stores keep temporary credentials from surviving an optimized wipe.
inline void SecureClear(void* buffer, std::size_t size) {
    volatile unsigned char* bytes = static_cast<volatile unsigned char*>(buffer);
    while (size--) *bytes++ = 0;
}

struct Credentials {
    unsigned char nonce[16] = {};
    char username[18] = {}, password[17] = {};
    Credentials() = default;
    Credentials(const Credentials&) = delete;
    Credentials& operator=(const Credentials&) = delete;
    void Clear() {
        SecureClear(nonce, sizeof(nonce));
        SecureClear(username, sizeof(username));
        SecureClear(password, sizeof(password));
    }
    ~Credentials() { Clear(); }
};

// Both inputs carry the ordinary generated local account format, but a
// diagnostic fixture is never accepted as a normal game's login handoff.
inline bool Parse(const unsigned char* raw, std::size_t size, InputKind kind, Credentials& output) {
    output.Clear();
    if (!raw || size != InputBytes ||
        std::memcmp(raw, kind == InputKind::Login ? "ESTLOGN1" : "ESTAUTH1", 8) ||
        std::memcmp(raw + 24, "EST", 3)) return false;
    for (std::size_t i = 27; i < 41; ++i)
        if (!raw[i] || !std::strchr("0123456789ABCDEF", raw[i])) return false;
    for (std::size_t i = 41; i < InputBytes; ++i)
        if (!raw[i] || !std::strchr("ABCDEFGHJKLMNPQRSTUVWXYZ23456789", raw[i])) return false;
    std::memcpy(output.nonce, raw + 8, sizeof(output.nonce));
    std::memcpy(output.username, raw + 24, 17);
    std::memcpy(output.password, raw + 41, 16);
    return true;
}
}

namespace Esteria::LocalLogin {
inline bool requestedAtStartup = false;
inline bool startupValid = true;
inline void ConfigureAtStartup(bool requested, bool offline) {
    requestedAtStartup = requested;
    startupValid = !requested || offline;
}
inline bool Requested() { return requestedAtStartup; }
inline bool StartupValid() { return startupValid; }

struct Readiness {
    bool initialized = false, framesLoaded = false, suspended = false, reloading = false;
    bool loginScreen = false, idle = false, scanReady = false;
    bool tosAccepted = false, eulaAccepted = false, clientServicesReady = false;
    bool Ready() const {
        return initialized && framesLoaded && !suspended && !reloading && loginScreen && idle &&
            scanReady && tosAccepted && eulaAccepted && clientServicesReady;
    }
};

enum class Result { Dormant, Rejected, Waiting, AlreadyAttempted, ReadFailed, Submitted };
class Attempt {
    bool attempted = false;
public:
    // The state belongs to the process, not a Glue lifetime. Failure and Glue
    // reloads cannot cause another read or another automatic login attempt.
    template<class Read, class Submit>
    Result Run(bool requested, bool offline, const Readiness& readiness, Read read, Submit submit) {
        if (!requested) return Result::Dormant;
        if (!offline) return Result::Rejected;
        if (attempted) return Result::AlreadyAttempted;
        if (!readiness.Ready()) return Result::Waiting;
        attempted = true;
        LocalAccount::Credentials credentials;
        if (!read(credentials)) return Result::ReadFailed;
        submit(credentials.username, credentials.password);
        return Result::Submitted;
    }
};
}
#endif
