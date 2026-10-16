// Standalone class-mask tests; build with default settings and MAX_CLASSES=254.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <type_traits>
#include "WoW/Common/ClassMask.h"
#include "WoW/Source/DB/Rec/TalentTabRec.h"
#include "WoW/Source/DB/Rec/SkillLineAbilityRec.h"
#include "WoW/Source/DB/Rec/SkillRaceClassInfoRec.h"
#include "WoW/Source/DB/Rec/FactionRec.h"
#include "WoW/Source/Object/ItemStats.h"
#include "WoW/Source/Object/CGPetition.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
        std::exit(1); \
    } \
} while (false)

constexpr bool Extended = WOWDECOMP_MAX_CLASSES > 32;
static_assert(sizeof(ClassMask) == (Extended ? 32 : 4), "Class-mask wire size");
static_assert(TalentTabRec::s_numColumns == (Extended ? 31 : 24), "TalentTab columns");
static_assert(TalentTabRec::s_rowSize == (Extended ? 124 : 96), "TalentTab bytes");
static_assert(SkillLineAbilityRec::s_numColumns == (Extended ? 28 : 14), "SkillLineAbility columns");
static_assert(SkillLineAbilityRec::s_rowSize == (Extended ? 112 : 56), "SkillLineAbility bytes");
static_assert(SkillRaceClassInfoRec::s_numColumns == (Extended ? 15 : 8), "SkillRaceClassInfo columns");
static_assert(SkillRaceClassInfoRec::s_rowSize == (Extended ? 60 : 32), "SkillRaceClassInfo bytes");
static_assert(FactionRec::s_numColumns == (Extended ? 85 : 57), "Faction columns");
static_assert(FactionRec::s_rowSize == (Extended ? 340 : 228), "Faction bytes");
static_assert(std::is_same<decltype(TalentTabRec::m_classMask), ClassMask>::value, "Talent class mask");
static_assert(std::is_same<decltype(SkillLineAbilityRec::m_classMask), ClassMask>::value, "Ability inclusion mask");
static_assert(std::is_same<decltype(SkillLineAbilityRec::m_excludeClass), ClassMask>::value, "Ability exclusion mask");
static_assert(std::is_same<decltype(SkillRaceClassInfoRec::m_classMask), ClassMask>::value, "Skill class mask");
static_assert(std::is_same<decltype(FactionRec::m_reputationClassMask), ClassMask[4]>::value, "Faction class masks");
static_assert(std::is_same<decltype(ItemStats_C::m_allowableClass), ClassMask>::value, "Item class mask");
static_assert(ItemStats_C::s_cacheVersion == (Extended ? 6 : 5), "Item-cache format version");
static_assert(CGPetition::s_cacheVersion == (Extended ? 2 : 1), "Petition-cache format version");
static_assert(std::is_same<decltype(CGPetition::m_allowedClasses), ClassMask>::value, "Petition class mask");
static_assert(sizeof(CGPetition::m_allowedClasses) == (Extended ? 32 : 4), "Petition class-mask wire size");

static void CheckBoundaryClasses() {
    constexpr uint32_t ids[] = {1, 31, 32, 33, 64, 65, 127, 128, 129, 224, 225, 253, 254};
    for (uint32_t id : ids) {
        ClassMask one;
        one.SetClass(id);
        CHECK(one.TestClass(id) == (id <= WOWDECOMP_MAX_CLASSES));
        CHECK(one.Any() == (id <= WOWDECOMP_MAX_CLASSES));
        CHECK(!one.HasMultipleBits());
        for (uint32_t other = 1; other <= 254; ++other) {
            CHECK(one.TestClass(other) == (other == id && id <= WOWDECOMP_MAX_CLASSES));
        }
    }
    ClassMask invalid;
    for (uint32_t id : {0u, 255u, 256u, UINT32_MAX}) {
        invalid.SetClass(id);
        CHECK(!invalid.TestClass(id));
    }
    CHECK(!invalid);
    invalid.SetBit(256);
    CHECK(!invalid);
    CHECK(!invalid.TestBit(256));
    CHECK(!invalid.TestBit(UINT32_MAX));

    ClassMask who;
    who.SetBit(254); // WHO uses the class ID directly, rather than ID - 1.
    CHECK(who.TestBit(254) == Extended);
    CHECK(!who.TestClass(254));
    CHECK(!who.TestClass(255));
    CHECK(!who.TestClass(256));
}

static void CheckLegacyAndBitOperations() {
    ClassMask all(UINT32_MAX);
    ClassMask lowOnly;
    lowOnly.words[0] = UINT32_MAX; // A serialized restricted mask, not the sentinel constructor.
    for (uint32_t id = 1; id <= 254; ++id) {
        CHECK(all.TestClass(id) == (id <= WOWDECOMP_MAX_CLASSES));
        CHECK(lowOnly.TestClass(id) == (id <= 32));
    }
    CHECK(all.HasMultipleBits());
    CHECK(all == ~~all);
    CHECK(!~all);
    CHECK((all == lowOnly) == !Extended);
    CHECK(all != ClassMask());

    ClassMask first;
    first.SetClass(32);
    ClassMask second;
    second.SetClass(64);
    ClassMask last;
    last.SetClass(254);
    CHECK(!(first & second));
    CHECK(!(first & last));
    CHECK(!(second & last));
    CHECK((first | second).HasMultipleBits() == Extended);
    CHECK((first | last).HasMultipleBits() == Extended);
    CHECK(!first.HasMultipleBits());
    CHECK((first & all) == first);
    CHECK((first | all) == all);
}

static void CheckRandomizedOperations() {
    std::mt19937 random(0xC1A554u);
    for (uint32_t trial = 0; trial < 10000; ++trial) {
        ClassMask left, right;
        for (uint32_t word = 0; word < CLASS_MASK_WORDS; ++word) {
            left.words[word] = random();
            right.words[word] = random();
        }
        const ClassMask intersection = left & right;
        const ClassMask combined = left | right;
        const ClassMask inverse = ~left;
        uint32_t setCount = 0;
        bool same = true;
        for (uint32_t bit = 0; bit < 256; ++bit) {
            const bool a = bit < 32 * CLASS_MASK_WORDS
                && ((left.words[bit / 32] >> (bit % 32)) & 1);
            const bool b = bit < 32 * CLASS_MASK_WORDS
                && ((right.words[bit / 32] >> (bit % 32)) & 1);
            CHECK(intersection.TestBit(bit) == (a && b));
            CHECK(combined.TestBit(bit) == (a || b));
            CHECK(inverse.TestBit(bit) == (bit < 32 * CLASS_MASK_WORDS && !a));
            if (bit < WOWDECOMP_MAX_CLASSES) {
                CHECK(left.TestClass(bit + 1) == a);
            }
            setCount += a;
            same = same && a == b;
        }
        CHECK(left.Any() == (setCount != 0));
        CHECK(static_cast<bool>(left) == (setCount != 0));
        CHECK(left.HasMultipleBits() == (setCount > 1));
        CHECK((left == right) == same);
        CHECK((left != right) == !same);
        CHECK(~~left == left);
        ClassMask assigned = left;
        assigned &= right;
        CHECK(assigned == intersection);
        assigned = left;
        assigned |= right;
        CHECK(assigned == combined);
    }
}

int main() {
    CheckBoundaryClasses();
    CheckLegacyAndBitOperations();
    CheckRandomizedOperations();
    std::printf("Class-mask checks passed (%u words; 10,000 seeded random cases).\n", CLASS_MASK_WORDS);
    return 0;
}
