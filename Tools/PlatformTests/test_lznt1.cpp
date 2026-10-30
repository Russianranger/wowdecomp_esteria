#include "WoW/Common/ComponentCore/Lznt1.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void Check(bool condition, const char* label) {
    if (!condition) {
        std::cerr << "LZNT1 test failed: " << label << '\n';
        std::exit(1);
    }
}

void Expect(const std::vector<uint8_t>& packed, const std::vector<uint8_t>& expected) {
    std::vector<uint8_t> output(expected.size() + 2, 0xcd);
    size_t written = 99;
    Check(Esteria::DecodeLznt1(packed.data(), packed.size(), output.data() + 1,
                              expected.size(), written), "valid stream accepted");
    Check(written == expected.size(), "decoded size");
    Check(std::equal(expected.begin(), expected.end(), output.begin() + 1), "decoded bytes");
    Check(output.front() == 0xcd && output.back() == 0xcd, "output guards");
    if (!expected.empty()) {
        Check(!Esteria::DecodeLznt1(packed.data(), packed.size(), output.data() + 1,
                                   expected.size() - 1, written), "short destination rejected");
        Check(written == 0, "failed output not published");
    }
}

void Reject(const std::vector<uint8_t>& packed, size_t capacity = 4096) {
    std::vector<uint8_t> output(capacity + 2, 0xcd);
    size_t written = 99;
    Check(!Esteria::DecodeLznt1(packed.data(), packed.size(), output.data() + 1,
                               capacity, written), "invalid stream rejected");
    Check(written == 0, "invalid stream has no published size");
    Check(output.front() == 0xcd && output.back() == 0xcd, "invalid stream output guards");
}

// A corpus independent of our decoder, from Microsoft [MS-XCA] section 3.3.
// https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-xca/94164d22-2928-4417-876e-d193766c4db6
const std::vector<uint8_t> example = {
    0x38, 0xb0, 0x88, 0x46, 0x23, 0x20, 0x00, 0x20,
    0x47, 0x20, 0x41, 0x00, 0x10, 0xa2, 0x47, 0x01,
    0xa0, 0x45, 0x20, 0x44, 0x00, 0x08, 0x45, 0x01,
    0x50, 0x79, 0x00, 0xc0, 0x45, 0x20, 0x05, 0x24,
    0x13, 0x88, 0x05, 0xb4, 0x02, 0x4a, 0x44, 0xef,
    0x03, 0x58, 0x02, 0x8c, 0x09, 0x16, 0x01, 0x48,
    0x45, 0x00, 0xbe, 0x00, 0x9e, 0x00, 0x04, 0x01,
    0x18, 0x90, 0x00
};

void TestValidAndMalformed() {
    const char text[] = "F# F# G A A G F# E D D E F# F# E E F# F# G A A G F# E D D E F# E D D E E F# D E F# G F# D E F# G F# E D E A F# F# G A A G F# E D D E F# E D D";
    const std::vector<uint8_t> expected(text, text + sizeof(text));
    Check(expected.size() == 142, "reference corpus size");
    Expect(example, expected);
    auto terminated = example;
    terminated.insert(terminated.end(), {0, 0});
    Expect(terminated, expected);
    Expect({}, {});
    Expect({0, 0}, {});
    Expect({0x02, 0x30, 'a', 'b', 'c'}, {'a', 'b', 'c'});
    Expect({0x03, 0xb0, 0x02, 'A', 0xfc, 0x0f}, std::vector<uint8_t>(4096, 'A'));

    // Full raw chunk followed by the independent compressed fixture; history
    // and displacement bit allocation must reset at each chunk boundary.
    std::vector<uint8_t> multiple = {0xff, 0x3f};
    multiple.insert(multiple.end(), 4096, 0x5a);
    multiple.insert(multiple.end(), example.begin(), example.end());
    std::vector<uint8_t> combined(4096, 0x5a);
    combined.insert(combined.end(), expected.begin(), expected.end());
    Expect(multiple, combined);

    Reject({0x00}); // Truncated header.
    Reject({0x00, 0x20, 'a'}); // Invalid signature.
    Reject({0x03, 0x30, 'a'}); // Truncated raw chunk.
    Reject({0x00, 0xb0, 0}); // Flag byte without data.
    Reject({0x02, 0xb0, 0x01, 0x00, 0x00}); // History before chunk start.
    Reject({0x02, 0xb0, 0x02, 'A', 0x00}); // Half a match token.
    Reject({0x03, 0xb0, 0x02, 'A', 0x00, 0x10}); // Displacement beyond history.
    Reject({0x03, 0xb0, 0x02, 'A', 0xfd, 0x0f}, 8192); // Decoded chunk > 4096.
    Reject({0x00, 0x30, 'A', 0x00, 0x30, 'B'}); // Short intermediate chunk.
    Reject({0, 0, 1}); // Data after terminal marker.
    for (size_t size = 1; size < example.size(); ++size) {
        Reject(std::vector<uint8_t>(example.begin(), example.begin() + size));
    }
    size_t written = 99;
    Check(!Esteria::DecodeLznt1(nullptr, 1, nullptr, 0, written), "null source");
    Check(!Esteria::DecodeLznt1(nullptr, 0, nullptr, 1, written), "null destination");
}

void TestBoundedRandomInputs() {
    std::mt19937 random(12340);
    for (unsigned i = 0; i < 20000; ++i) {
        std::vector<uint8_t> packed(random() % 160);
        for (auto& byte : packed) {
            byte = static_cast<uint8_t>(random());
        }
        const size_t capacity = random() % 512;
        std::vector<uint8_t> output(capacity + 2, 0xcd);
        size_t written = 99;
        const bool result = Esteria::DecodeLznt1(packed.data(), packed.size(),
                                                output.data() + 1, capacity, written);
        Check(output.front() == 0xcd && output.back() == 0xcd, "random output guards");
        Check(result ? written <= capacity : written == 0, "random size invariant");
    }
}

#ifdef _WIN32
void TestWindowsCompression() {
    // Run on the desktop reference CI to compare the portable decoder against
    // the same system encoder used by Esteria's original texture-bank tooling.
    using WorkspaceFn = LONG(WINAPI*)(USHORT, PULONG, PULONG);
    using CompressFn = LONG(WINAPI*)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, ULONG, PULONG, PVOID);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto workspaceSize = reinterpret_cast<WorkspaceFn>(GetProcAddress(ntdll, "RtlGetCompressionWorkSpaceSize"));
    auto compress = reinterpret_cast<CompressFn>(GetProcAddress(ntdll, "RtlCompressBuffer"));
    Check(workspaceSize && compress, "Windows reference encoder available");
    ULONG work = 0;
    ULONG fragment = 0;
    Check(workspaceSize(2, &work, &fragment) >= 0, "Windows workspace size");
    std::vector<uint8_t> workspace(work);
    std::mt19937 random(12340);
    for (size_t size : {1u, 16u, 17u, 32u, 33u, 255u, 256u, 4095u, 4096u, 4097u, 16384u, 1048576u}) {
        for (unsigned pattern = 0; pattern < 4; ++pattern) {
            std::vector<uint8_t> input(size);
            for (size_t i = 0; i < size; ++i) {
                input[i] = static_cast<uint8_t>(pattern == 0 ? 'A'
                    : pattern == 1 ? i % 251 : pattern == 2 ? (i / 131) % 5 : random());
            }
            std::vector<uint8_t> packed(size * 2 + 8192);
            ULONG packedSize = 0;
            Check(compress(2, input.data(), static_cast<ULONG>(size), packed.data(),
                           static_cast<ULONG>(packed.size()), 4096, &packedSize,
                           workspace.data()) >= 0, "Windows reference compression");
            packed.resize(packedSize);
            Expect(packed, input);
        }
    }
}
#endif

}

int main() {
    TestValidAndMalformed();
    TestBoundedRandomInputs();
#ifdef _WIN32
    TestWindowsCompression();
#endif
    std::cout << "LZNT1 fixtures, malformed streams and bounded random inputs passed\n";
}
