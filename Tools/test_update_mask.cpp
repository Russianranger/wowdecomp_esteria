// Standalone executable: no client objects or packet-stream implementation needed.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include "WoW/Source/ObjectMgrClient/QuestUpdateBatch.h"
#include "WoW/Source/ObjectMgrClient/UpdateMask.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::fprintf(stderr, "Check failed at line %d: %s\n", __LINE__, #condition); \
        std::exit(1); \
    } \
} while (false)

using Masks = std::array<uint32_t, 255>;

static void SetField(Masks& masks, uint32_t field) {
    masks[field / 32] |= uint32_t(1) << (field % 32);
}

// Deliberately independent implementation: inspect every bit in order.
static bool ReferenceValidation(const Masks& masks, uint32_t blockCount,
                                uint32_t fieldCount, uint32_t availableDwords) {
    uint32_t values = 0;
    for (uint32_t field = 0; field < 32 * blockCount; ++field) {
        if ((masks[field / 32] >> (field % 32)) & 1) {
            if (field >= fieldCount) {
                return false;
            }
            ++values;
        }
    }
    return values <= availableDwords;
}

static void CheckCapacityBoundary(uint32_t fieldCount) {
    Masks masks{};
    const uint32_t blocks = (fieldCount + 31) / 32;
    SetField(masks, fieldCount - 1);
    CHECK(ValidateUpdateMask(masks.data(), blocks, fieldCount, 1));
    CHECK(!ValidateUpdateMask(masks.data(), blocks, fieldCount, 0));
    masks.fill(0);
    SetField(masks, fieldCount);
    CHECK(!ValidateUpdateMask(masks.data(), blocks, fieldCount, 1));
}

static void CheckExplicitCases() {
    CheckCapacityBoundary(1326); // Stock player: fields 0..1325, 42 mask blocks.
    CheckCapacityBoundary(6201); // 1,000 quests: fields 0..6200, 194 mask blocks.
    CheckCapacityBoundary(574);  // 254 bag slots: fields 0..573, 18 mask blocks.

    Masks masks{};
    CHECK(ValidateUpdateMask(nullptr, 0, 0, 0));
    CHECK(ValidateUpdateMask(nullptr, 0, 6201, 0));
    CHECK(ValidateUpdateMask(masks.data(), 255, 0, 0));
    CHECK(ValidateUpdateMask(masks.data(), 255, 6201, 0));

    for (uint32_t field : {31u, 32u, 63u, 64u}) {
        SetField(masks, field);
    }
    CHECK(ValidateUpdateMask(masks.data(), 3, 65, 4));
    CHECK(!ValidateUpdateMask(masks.data(), 3, 65, 3));
    CHECK(!ValidateUpdateMask(masks.data(), 3, 64, 4));
    CHECK(ValidateUpdateMask(masks.data(), 255, 574, 4)); // Zero trailing blocks.

    masks.fill(0);
    SetField(masks, 573);
    CHECK(ValidateUpdateMask(masks.data(), 255, 574, 1));
    SetField(masks, 18 * 32); // Entire block beyond the container's final field.
    CHECK(!ValidateUpdateMask(masks.data(), 19, 574, 2));

    masks.fill(0);
    SetField(masks, 6200);
    CHECK(ValidateUpdateMask(masks.data(), 255, 6201, 1));
    SetField(masks, 254 * 32);
    CHECK(!ValidateUpdateMask(masks.data(), 255, 6201, 2));
    CHECK(!ValidateUpdateMask(masks.data(), 255, 0, 2));

    masks.fill(0xFFFFFFFFu);
    CHECK(ValidateUpdateMask(masks.data(), 255, 8160, 8160));
    CHECK(!ValidateUpdateMask(masks.data(), 255, 8160, 8159));
}

static void CheckRandomizedCases() {
    std::mt19937 random(0x12340u);
    constexpr uint32_t capacities[] = {0, 1, 31, 32, 33, 64, 65, 574, 1326, 6201, 8160};
    for (uint32_t trial = 0; trial < 20000; ++trial) {
        Masks masks{};
        const uint32_t blockCount = random() % 256;
        const uint32_t fieldCount = trial % 2
            ? capacities[random() % (sizeof(capacities) / sizeof(capacities[0]))]
            : random() % 8161;
        uint32_t values = 0;
        for (uint32_t block = 0; block < blockCount; ++block) {
            masks[block] = random();
            if (trial % 3 == 0) {
                masks[block] &= random() & random(); // Sparse updates.
            }
            if (trial % 4 < 2) {
                // Half the inputs contain only valid fields, including tail padding.
                for (uint32_t bit = 0; bit < 32; ++bit) {
                    if (32 * block + bit >= fieldCount) {
                        masks[block] &= ~(uint32_t(1) << bit);
                    }
                }
            }
            for (uint32_t bit = 0; bit < 32; ++bit) {
                values += (masks[block] >> bit) & 1;
            }
        }
        const uint32_t available = trial % 3 == 0 ? (values ? values - 1 : 0)
            : trial % 3 == 1 ? values : random() % 8161;
        CHECK(ValidateUpdateMask(masks.data(), blockCount, fieldCount, available)
            == ReferenceValidation(masks, blockCount, fieldCount, available));
    }
}

static void CheckQuestRefreshBatching() {
    CHECK(QuestUpdateBatch::NeedsRefresh());
    CHECK(QuestUpdateBatch::NeedsRefresh());
    {
        QuestUpdateBatch batch;
        CHECK(QuestUpdateBatch::NeedsRefresh());
        CHECK(!QuestUpdateBatch::NeedsRefresh());
        {
            QuestUpdateBatch nested;
            CHECK(QuestUpdateBatch::NeedsRefresh());
            CHECK(!QuestUpdateBatch::NeedsRefresh());
        }
        CHECK(!QuestUpdateBatch::NeedsRefresh()); // Restore the refreshed parent.
    }
    CHECK(QuestUpdateBatch::NeedsRefresh());
    CHECK(QuestUpdateBatch::NeedsRefresh());
    {
        QuestUpdateBatch next;
        CHECK(QuestUpdateBatch::NeedsRefresh()); // New object update, fresh refresh.
        CHECK(!QuestUpdateBatch::NeedsRefresh());
    }
    {
        QuestUpdateBatch parent;
        {
            QuestUpdateBatch nested;
            CHECK(QuestUpdateBatch::NeedsRefresh());
            CHECK(!QuestUpdateBatch::NeedsRefresh());
        }
        CHECK(QuestUpdateBatch::NeedsRefresh()); // Also restore an unrefreshed parent.
        CHECK(!QuestUpdateBatch::NeedsRefresh());
    }
    CHECK(QuestUpdateBatch::NeedsRefresh());
}

int main() {
    CheckExplicitCases();
    CheckRandomizedCases();
    CheckQuestRefreshBatching();
    std::puts("Update-mask checks passed (20,000 seeded random cases plus boundary cases); quest batching passed.");
    return 0;
}
