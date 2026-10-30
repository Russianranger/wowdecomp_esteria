#include "Engine/Source/Platform/LocalOffline.h"
#include <cstdio>
#include <initializer_list>

int main() {
    using namespace Esteria::LocalOffline;
    if (Enabled() || Reject("desktop-default-test")) return 1;
    for (const char* address : {"127.0.0.1", "127.0.0.1:3724", "127.0.0.1:8085", "127.0.0.1:65535"})
        if (!IsLiteralLoopbackEndpoint(address)) return 2;
    for (const char* address : {"localhost", "127.1", "127.0.0.2", "0.0.0.0", "2130706433", "::1",
         "127.0.0.1.example", "127.0.0.1 ", "127.0.0.1:", "127.0.0.1:0", "127.0.0.1:-1",
         "127.0.0.1:65536", "127.0.0.1:123456", "127.0.0.1:3724/", "127.0.0.1:80@remote"})
        if (IsLiteralLoopbackEndpoint(address)) return 3;
    if (IsLiteralLoopbackEndpoint(nullptr) || IsLiteralLoopbackHost(nullptr) ||
        IsLiteralLoopbackHost("127.0.0.1:3724") || !IsLiteralLoopbackHost("127.0.0.1")) return 4;
    const unsigned char local[4] = {127, 0, 0, 1}, other[4] = {127, 0, 0, 2};
    if (!IsLoopbackAddressBytes(local) || IsLoopbackAddressBytes(other) || IsLoopbackAddressBytes(nullptr)) return 5;
    ConfigureAtStartup(true);
    if (!Enabled() || !Reject("fixture remote request")) return 6;
    std::puts("PASS: explicit local-offline policy, strict literal IPv4 endpoints and desktop default");
}
