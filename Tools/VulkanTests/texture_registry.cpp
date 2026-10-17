#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture/Internal.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
int failAfter = -1;
void Check(bool pass, const char* message) {
    if (!pass) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
}
VkImage Handle(uintptr_t value) { return reinterpret_cast<VkImage>(value); }
}

// Exercise allocation failure in either half of a table growth without exhausting host memory.
void* operator new[](std::size_t bytes, const std::nothrow_t&) noexcept {
    if (failAfter == 0) { failAfter = -1; return nullptr; }
    if (failAfter > 0) --failAfter;
    try { return ::operator new[](bytes); } catch (...) { return nullptr; }
}

namespace gxvk::log { void Write(Level, const char*, ...) {} }

int main() {
    using namespace gxvk::vk::texture::detail;
    Check(FindOwned(VK_NULL_HANDLE) == kNotOwned, "empty registry lookup");
    Check(EnsureOwnedCapacity(), "initial allocation");
    auto insert = [](VkImage handle) {
        Check(EnsureOwnedCapacity(), "reserve image entry");
        s.owned[s.ownedCount].handle = handle;
        s.owned[s.ownedCount].block.size = reinterpret_cast<uintptr_t>(handle);
        HashInsert(s.ownedCount++);
    };
    for (uintptr_t i = 1; i <= kInitialImages; ++i) insert(Handle(i));
    Owned* original = s.owned;
    uint32_t* originalHash = s.hash;
    for (int allocation : {0, 1}) {
        failAfter = allocation;
        Check(!EnsureOwnedCapacity(), "allocation failure is reported");
        Check(s.owned == original && s.hash == originalHash, "failed growth preserves tables");
        Check(s.ownedCount == kInitialImages && s.ownedCapacity == kInitialImages,
              "failed growth preserves count and capacity");
        for (uintptr_t i = 1; i <= kInitialImages; ++i)
            Check(FindOwned(Handle(i)) == i - 1, "lookup survives failed growth");
    }
    for (uintptr_t i = kInitialImages + 1; i <= 40000; ++i) insert(Handle(i));
    Check(s.ownedCount == 40000 && s.ownedCapacity >= 40000, "cross the former 16384-image limit twice");
    Check(FindOwned(Handle(50000)) == kNotOwned, "missing lookup after growth");
    for (uintptr_t i = 1; i <= 40000; ++i) {
        const uint32_t index = FindOwned(Handle(i));
        Check(index != kNotOwned && s.owned[index].block.size == i, "growth preserves resource metadata");
    }
    // Remove from the middle repeatedly, exercising both probe repair and dense swap removal.
    while (s.ownedCount) {
        const VkImage removed = s.owned[s.ownedCount / 2].handle;
        RemoveOwned(s.ownedCount / 2);
        Check(FindOwned(removed) == kNotOwned, "removed handle disappears");
        if (s.ownedCount) {
            Check(FindOwned(s.owned[0].handle) == 0, "first remaining entry is indexed");
            Check(FindOwned(s.owned[s.ownedCount - 1].handle) == s.ownedCount - 1,
                  "last remaining entry is indexed");
        }
    }
    FreeTables();
    Check(!s.owned && !s.hash && !s.ownedCount && !s.ownedCapacity && !s.hashSlots, "release resets tables");
    Check(EnsureOwnedCapacity(), "reinitialize after release");
    // Force a probe chain to wrap around the hash table's end, then remove its head and middle.
    std::vector<VkImage> collisions;
    for (uintptr_t value = 100000; collisions.size() < 24; ++value) {
        if ((HashOf(Handle(value)) & (s.hashSlots - 1)) == s.hashSlots - 1) {
            collisions.push_back(Handle(value));
            insert(collisions.back());
        }
    }
    for (size_t i = 0; i < collisions.size(); ++i) {
        const uint32_t index = FindOwned(collisions[i]);
        Check(index != kNotOwned, "wrapped collision survives earlier removals");
        RemoveOwned(index);
        for (size_t j = i + 1; j < collisions.size(); ++j)
            Check(FindOwned(collisions[j]) != kNotOwned, "wrapped probe chain remains searchable");
    }
    FreeTables();
    std::puts("PASS: 40000 images, allocation failure rollback, growth, metadata, deletion, wrapped collisions, reinitialization");
}
