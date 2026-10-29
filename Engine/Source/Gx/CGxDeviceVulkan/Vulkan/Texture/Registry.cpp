// The textures' owned-image table, format descriptions, layouts and barriers.
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture/Internal.hpp"

namespace gxvk::vk::texture::detail
{
    State s;

    void Fail(const char* what, VkResult r)
    {
        Write(Warn, "vulkan: texture: %s failed: %s", what, ResultName(r));
    }

    /**
     * The format table: every VkFormat this module accepts. A short allow-list, because the unit
     * size is what sizes the copies and refuses an upload whose byte count is wrong. Three-byte
     * formats are left out: rarely supported with optimal tiling.
     *
     * @return FormatInfo info : unitBytes is 0 for a format this module does not handle
     */
    FormatInfo Describe(VkFormat f)
    {
        switch (f)
        {
            case VK_FORMAT_R8_UNORM:                 return { 1,  false, "R8_UNORM" };
            case VK_FORMAT_R8G8_UNORM:               return { 2,  false, "R8G8_UNORM" };
            case VK_FORMAT_R8G8_SNORM:               return { 2,  false, "R8G8_SNORM" };
            case VK_FORMAT_R5G6B5_UNORM_PACK16:      return { 2,  false, "R5G6B5_UNORM_PACK16" };
            case VK_FORMAT_B5G6R5_UNORM_PACK16:      return { 2,  false, "B5G6R5_UNORM_PACK16" };
            case VK_FORMAT_A1R5G5B5_UNORM_PACK16:    return { 2,  false, "A1R5G5B5_UNORM_PACK16" };
            case VK_FORMAT_R8G8B8A8_UNORM:           return { 4,  false, "R8G8B8A8_UNORM" };
            case VK_FORMAT_R8G8B8A8_SRGB:            return { 4,  false, "R8G8B8A8_SRGB" };
            case VK_FORMAT_B8G8R8A8_UNORM:           return { 4,  false, "B8G8R8A8_UNORM" };
            case VK_FORMAT_B8G8R8A8_SRGB:            return { 4,  false, "B8G8R8A8_SRGB" };
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return { 4,  false, "A2B10G10R10_UNORM_PACK32" };
            case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return { 4,  false, "A2R10G10B10_UNORM_PACK32" };
            case VK_FORMAT_R16G16_SFLOAT:            return { 4,  false, "R16G16_SFLOAT" };
            case VK_FORMAT_R32_SFLOAT:               return { 4,  false, "R32_SFLOAT" };
            case VK_FORMAT_R32G32_SFLOAT:            return { 8,  false, "R32G32_SFLOAT" };
            case VK_FORMAT_R16G16B16A16_SFLOAT:      return { 8,  false, "R16G16B16A16_SFLOAT" };
            case VK_FORMAT_R32G32B32A32_SFLOAT:      return { 16, false, "R32G32B32A32_SFLOAT" };
            case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:     return { 8,  true,  "BC1_RGBA_UNORM_BLOCK" };
            case VK_FORMAT_BC2_UNORM_BLOCK:          return { 16, true,  "BC2_UNORM_BLOCK" };
            case VK_FORMAT_BC3_UNORM_BLOCK:          return { 16, true,  "BC3_UNORM_BLOCK" };
            default:                                 return {};
        }
    }

    const char* Name(VkFormat f)
    {
        const FormatInfo info = Describe(f);
        return info.name ? info.name : FormatName(f);
    }

    /// max(1, side >> level): how Vulkan defines a mip level's side.
    uint32_t LevelSide(uint32_t side, uint32_t level)
    {
        const uint32_t v = level >= 32 ? 0 : side >> level;
        return v ? v : 1;
    }

    /// floor(log2(max(w, h))) + 1: the level at which both sides have reached 1.
    uint32_t MaxLevels(uint32_t width, uint32_t height)
    {
        uint32_t side   = width > height ? width : height;
        uint32_t levels = 1;
        while (side > 1)
        {
            side >>= 1;
            ++levels;
        }
        return levels;
    }

    uint32_t RowCount(const FormatInfo& info, uint32_t height)
    {
        return info.compressed ? (height + kBlock - 1) / kBlock : height;
    }

    uint64_t RowBytes(const FormatInfo& info, uint32_t width)
    {
        const uint64_t units = info.compressed ? (width + kBlock - 1) / kBlock : width;
        return units * info.unitBytes;
    }

    /// Confirms every entry point this module calls is non-null, naming the ones that are not.
    bool HaveEntryPoints(const loader::Device& d, const loader::Instance& i)
    {
        struct Entry { const void* fn; const char* name; };
        const Entry entries[] = {
            { reinterpret_cast<const void*>(d.CreateImage),                      "vkCreateImage" },
            { reinterpret_cast<const void*>(d.DestroyImage),                     "vkDestroyImage" },
            { reinterpret_cast<const void*>(d.GetImageMemoryRequirements),       "vkGetImageMemoryRequirements" },
            { reinterpret_cast<const void*>(d.BindImageMemory),                  "vkBindImageMemory" },
            { reinterpret_cast<const void*>(d.CreateImageView),                  "vkCreateImageView" },
            { reinterpret_cast<const void*>(d.DestroyImageView),                 "vkDestroyImageView" },
            { reinterpret_cast<const void*>(d.CmdPipelineBarrier),               "vkCmdPipelineBarrier" },
            { reinterpret_cast<const void*>(d.CmdCopyBufferToImage),             "vkCmdCopyBufferToImage" },
            { reinterpret_cast<const void*>(d.CmdClearColorImage),               "vkCmdClearColorImage" },
            { reinterpret_cast<const void*>(d.CmdBlitImage),                     "vkCmdBlitImage" },
            { reinterpret_cast<const void*>(i.GetPhysicalDeviceProperties),       "vkGetPhysicalDeviceProperties" },
            { reinterpret_cast<const void*>(i.GetPhysicalDeviceFormatProperties), "vkGetPhysicalDeviceFormatProperties" },
        };

        unsigned missing = 0;
        for (const Entry& e : entries)
        {
            if (!e.fn)
            {
                Write(Warn, "vulkan: texture: entry point missing: %s", e.name);
                ++missing;
            }
        }
        if (missing) Write(Warn, "vulkan: texture: %u entry point(s) missing; declining", missing);
        return missing == 0;
    }

    /// Reserve one more entry before creating GPU resources. Commit both tables together so a
    /// failed allocation leaves every existing image and lookup intact.
    bool EnsureOwnedCapacity()
    {
        if (s.ownedCount < s.ownedCapacity) return true;
        // Leave room for doubling the hash slot count in uint32_t as well as both host arrays.
        if (s.ownedCapacity >= (1u << 30)) return false;
        const uint32_t capacity = s.ownedCapacity ? s.ownedCapacity * 2 : kInitialImages;
        const uint32_t slots = capacity * 2;
        if (uint64_t(capacity) * sizeof(Owned) > SIZE_MAX
            || uint64_t(slots) * sizeof(uint32_t) > SIZE_MAX) return false;

        Owned* owned = new (std::nothrow) Owned[capacity];
        uint32_t* hash = new (std::nothrow) uint32_t[slots]();
        if (!owned || !hash)
        {
            delete[] owned;
            delete[] hash;
            Write(Warn, "vulkan: texture: cannot grow the image table from %u to %u entries", s.ownedCapacity, capacity);
            return false;
        }
        for (uint32_t i = 0; i < s.ownedCount; ++i)
        {
            owned[i] = s.owned[i];
            uint32_t p = HashOf(owned[i].handle) & (slots - 1);
            while (hash[p] != 0) p = (p + 1) & (slots - 1);
            hash[p] = i + 1;
        }
        const uint32_t previous = s.ownedCapacity;
        delete[] s.owned;
        delete[] s.hash;
        s.owned = owned;
        s.hash = hash;
        s.ownedCapacity = capacity;
        s.hashSlots = slots;
        if (previous)
            Write(Debug, "vulkan: texture: image table grew from %u to %u entries (%u live)", previous, capacity, s.ownedCount);
        return true;
    }

    /// The dense index of an image, or kNotOwned when it is not in the table.
    uint32_t FindOwned(VkImage handle)
    {
        if (!s.hash || handle == VK_NULL_HANDLE)
        {
            for (uint32_t i = 0; i < s.ownedCount; ++i)
                if (s.owned[i].handle == handle) return i;
            return kNotOwned;
        }
        for (uint32_t p = HashOf(handle) & (s.hashSlots - 1);; p = (p + 1) & (s.hashSlots - 1))
        {
            const uint32_t v = s.hash[p];
            if (v == 0) return kNotOwned;
            if (s.owned[v - 1].handle == handle) return v - 1;
        }
    }

    /// The hash slot holding dense index `index`.
    static uint32_t HashSlotOf(uint32_t index)
    {
        for (uint32_t p = HashOf(s.owned[index].handle) & (s.hashSlots - 1);; p = (p + 1) & (s.hashSlots - 1))
            if (s.hash[p] == index + 1 || s.hash[p] == 0) return p;
    }

    /// Files a new dense entry (already written at s.owned[index]) in the index.
    void HashInsert(uint32_t index)
    {
        if (!s.hash) return;
        uint32_t p = HashOf(s.owned[index].handle) & (s.hashSlots - 1);
        while (s.hash[p] != 0) p = (p + 1) & (s.hashSlots - 1);
        s.hash[p] = index + 1;
    }

    /**
     * Takes dense entry `i` out of the table, keeping it dense (swap-remove) and the index exact:
     * linear probing with backward-shift deletion, so no tombstones accumulate. The entry's
     * objects are the caller's to have destroyed first.
     */
    void RemoveOwned(uint32_t i)
    {
        const uint32_t last = s.ownedCount - 1;
        if (s.hash)
        {
            uint32_t hole = HashSlotOf(i);
            s.hash[hole] = 0;
            for (uint32_t j = (hole + 1) & (s.hashSlots - 1); s.hash[j] != 0; j = (j + 1) & (s.hashSlots - 1))
            {
                const uint32_t home = HashOf(s.owned[s.hash[j] - 1].handle) & (s.hashSlots - 1);
                // The entry at j may move into the hole unless its home lies cyclically in (hole, j].
                const bool stays = hole <= j ? (home > hole && home <= j) : (home > hole || home <= j);
                if (stays) continue;
                s.hash[hole] = s.hash[j];
                s.hash[j]    = 0;
                hole         = j;
            }
            if (i != last) s.hash[HashSlotOf(last)] = i + 1;
        }
        s.owned[i]    = s.owned[last];
        s.owned[last] = Owned();
        --s.ownedCount;
    }

    void FreeTables()
    {
        delete[] s.owned;
        delete[] s.hash;
        s.owned = nullptr;
        s.hash  = nullptr;
        s.ownedCount = 0;
        s.ownedCapacity = 0;
        s.hashSlots = 0;
    }

    /**
     * The whole-image colour subresource range. Every barrier here names every level and layer,
     * because Image tracks ONE layout for the whole image: a barrier on one level would leave the
     * others in a layout the tracked field no longer describes, and every later barrier reads that
     * field as its oldLayout.
     */
    VkImageSubresourceRange WholeColour(uint32_t levels, uint32_t layers)
    {
        VkImageSubresourceRange range = {};
        range.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        range.baseMipLevel   = 0;
        range.levelCount     = levels;
        range.baseArrayLayer = 0;
        range.layerCount     = layers;
        return range;
    }

    /// One whole-image layout transition as its own vkCmdPipelineBarrier. Queue families IGNORED:
    /// every image is EXCLUSIVE and only ever used on the graphics queue.
    void Transition(VkCommandBuffer cmd, VkImage image, uint32_t levels, uint32_t layers, VkImageLayout from, VkImageLayout to,
                    VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess)
    {
        VkImageMemoryBarrier b = {};
        b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask       = srcAccess;
        b.dstAccessMask       = dstAccess;
        b.oldLayout           = from;
        b.newLayout           = to;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image               = image;
        b.subresourceRange    = WholeColour(levels, layers);
        s.dapi->CmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
}
