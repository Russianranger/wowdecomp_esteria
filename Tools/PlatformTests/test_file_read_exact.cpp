#include "Engine/Source/BlizzardCore/Source/Packages/iounit/ReadExactAt.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

static void require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

static void checkTableRead(int64_t start, uint32_t transferLimit, unsigned expectedCalls) {
    constexpr uint32_t size = 1048576;
    std::vector<uint8_t> table(size);
    uint32_t copied = 0;
    unsigned calls = 0;
    auto read = [&](void* dest, int64_t offset, int32_t* amount) {
        ++calls;
        require(offset == start + copied && dest == table.data() + copied);
        require(*amount == static_cast<int32_t>(size - copied));
        *amount = std::min(*amount, static_cast<int32_t>(transferLimit));
        for (int32_t i = 0; i < *amount; ++i) {
            table[copied + i] = static_cast<uint8_t>((offset + i) % 251);
        }
        copied += *amount;
        return true;
    };
    require(System_File::ReadExactAt(table.data(), start, size, read));
    require(copied == size && calls == expectedCalls);
    for (uint32_t i = 0; i < size; ++i) {
        require(table[i] == static_cast<uint8_t>((start + i) % 251));
    }
}

int main() {
    checkTableRead(4003005889, 507904, 3); // Actual Wine read from stock patch.MPQ.
    checkTableRead(4003005889, 1048576, 1);
    checkTableRead(INT64_C(0xFFFFF000), 65536, 16); // Continuation crosses 4 GiB.

    uint8_t out[8] = {};
    for (int32_t last : {0, -1, 7, 1}) {
        unsigned calls = 0;
        auto incomplete = [&](void* dest, int64_t offset, int32_t* amount) {
            ++calls;
            if (calls == 1) {
                require(dest == out && offset == 0 && *amount == 8);
                *amount = 2;
                return true;
            }
            require(calls == 2 && dest == out + 2 && offset == 2 && *amount == 6);
            *amount = last;
            return last != 1; // Failed backend call, even with a positive output count.
        };
        require(!System_File::ReadExactAt(out, 0, 8, incomplete) && calls == 2);
    }

    auto unexpected = [](void*, int64_t, int32_t*) { std::abort(); return false; };
    require(!System_File::ReadExactAt(out, -1, 1, unexpected));
    require(!System_File::ReadExactAt(out, INT64_MAX, 2, unexpected));
    for (bool ok : {false, true}) {
        unsigned calls = 0;
        auto empty = [&](void*, int64_t pos, int32_t* amount) {
            ++calls;
            require(pos == INT64_MAX && *amount == 0);
            return ok;
        };
        require(System_File::ReadExactAt(nullptr, INT64_MAX, 0, empty) == ok);
        require(calls == 1);
    }
    auto huge = [](void*, int64_t, int32_t* amount) {
        require(*amount == INT32_MAX);
        return false;
    };
    require(!System_File::ReadExactAt(out, 0, UINT32_MAX, huge));
}
