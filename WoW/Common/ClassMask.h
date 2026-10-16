#ifndef WOW_COMMON_CLASSMASK_H
#define WOW_COMMON_CLASSMASK_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

#ifndef WOWDECOMP_MAX_CLASSES
#define WOWDECOMP_MAX_CLASSES 32
#endif

// Class IDs remain bytes. Only membership masks grow in extended builds.
constexpr uint32_t CLASS_MASK_WORDS = WOWDECOMP_MAX_CLASSES > 32 ? 8 : 1;
constexpr uint32_t CLASS_MASK_EXTRA_COLUMNS = CLASS_MASK_WORDS - 1;

struct ClassMask {
    uint32_t words[CLASS_MASK_WORDS];

    // Legacy all-ones means unrestricted/all classes, including extended IDs.
    constexpr ClassMask(uint32_t legacy = 0) : words{} {
        words[0] = legacy;
        if (legacy == UINT32_MAX) {
            for (uint32_t i = 1; i < CLASS_MASK_WORDS; ++i) words[i] = UINT32_MAX;
        }
    }
    constexpr bool TestBit(uint32_t bit) const {
        return bit < 32 * CLASS_MASK_WORDS && (words[bit / 32] & (1u << (bit % 32)));
    }
    constexpr bool TestClass(uint32_t id) const {
        return id >= 1 && id <= WOWDECOMP_MAX_CLASSES && TestBit(id - 1);
    }
    constexpr void SetBit(uint32_t bit) {
        if (bit < 32 * CLASS_MASK_WORDS) words[bit / 32] |= 1u << (bit % 32);
    }
    constexpr void SetClass(uint32_t id) {
        if (id >= 1 && id <= WOWDECOMP_MAX_CLASSES) SetBit(id - 1);
    }
    constexpr bool Any() const {
        for (uint32_t word : words) if (word) return true;
        return false;
    }
    constexpr bool HasMultipleBits() const {
        bool found = false;
        for (uint32_t word : words) {
            if (word && (found || (word & (word - 1)))) return true;
            found = found || word != 0;
        }
        return false;
    }
    constexpr explicit operator bool() const { return Any(); }
    constexpr ClassMask operator~() const {
        ClassMask result;
        for (uint32_t i = 0; i < CLASS_MASK_WORDS; ++i) result.words[i] = ~words[i];
        return result;
    }
    constexpr ClassMask& operator|=(const ClassMask& other) {
        for (uint32_t i = 0; i < CLASS_MASK_WORDS; ++i) words[i] |= other.words[i];
        return *this;
    }
    constexpr ClassMask& operator&=(const ClassMask& other) {
        for (uint32_t i = 0; i < CLASS_MASK_WORDS; ++i) words[i] &= other.words[i];
        return *this;
    }
    constexpr bool operator==(const ClassMask& other) const {
        for (uint32_t i = 0; i < CLASS_MASK_WORDS; ++i) if (words[i] != other.words[i]) return false;
        return true;
    }
    constexpr bool operator!=(const ClassMask& other) const { return !(*this == other); }
};

constexpr ClassMask operator&(ClassMask a, const ClassMask& b) { return a &= b; }
constexpr ClassMask operator|(ClassMask a, const ClassMask& b) { return a |= b; }
static_assert(WOWDECOMP_MAX_CLASSES >= 32 && WOWDECOMP_MAX_CLASSES <= 254, "Supported class ceiling: 32..254");
static_assert(sizeof(ClassMask) == 4 * CLASS_MASK_WORDS, "Class mask wire size");
static_assert(std::is_trivially_copyable<ClassMask>::value, "Class masks support direct DBC reads");

#endif
