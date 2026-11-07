// Images: the module's life, the recorded fills (region batches, clears, generated mip chains) and
// the destroy.
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture/Internal.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"

#include <algorithm>

using namespace gxvk::vk::texture::detail;

namespace
{
    /// The stages that read a sampled image: a draw's vertex and fragment shaders.
    constexpr VkPipelineStageFlags kReaders = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    /// The layout every fill leaves an image in: what a combined-image-sampler descriptor names,
    /// so the draw path binds it with no barrier per frame.
    constexpr VkImageLayout kSampled = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

namespace gxvk::vk::texture
{
    bool Init()
    {
        const context::Handles* h = context::Get();
        if (!h || !h->device || !h->dapi || !h->iapi || !h->physical)
        {
            Write(Warn, "vulkan: texture: Init() without a live context");
            return false;
        }

        if (s.ready)
        {
            if (s.device == h->device) return true;
            // A different VkDevice means the old one is gone: nothing on it can be destroyed.
            Write(Warn, "vulkan: texture: the context reports a different VkDevice; forgetting %u image(s) rather than destroying them", s.ownedCount);
            FreeTables();
            s = State();
        }

        if (!HaveEntryPoints(*h->dapi, *h->iapi)) return false;

        if (!EnsureOwnedCapacity())
        {
            Write(Error, "vulkan: texture: cannot allocate the image table");
            FreeTables();
            return false;
        }

        s.device   = h->device;
        s.physical = h->physical;
        s.dapi     = h->dapi;
        s.iapi     = h->iapi;

        VkPhysicalDeviceProperties props = {};
        s.iapi->GetPhysicalDeviceProperties(s.physical, &props);
        s.maxDimension     = props.limits.maxImageDimension2D;
        s.maxDimensionCube = props.limits.maxImageDimensionCube;
        s.transferBits     = props.apiVersion >= VK_MAKE_API_VERSION(0, 1, 1, 0);

        s.ready = true;
        Write(Debug, "vulkan: texture: ready (images up to %ux%u, cubes up to %u, image table starts at %u and grows on demand)",
              s.maxDimension, s.maxDimension, s.maxDimensionCube, s.ownedCapacity);
        return true;
    }

    void Shutdown()
    {
        if (!s.ready) return;
        if (s.ownedCount)
            Write(Warn, "vulkan: texture: Shutdown() with %u image(s) still alive; they are forgotten", s.ownedCount);
        FreeTables();
        s = State();
    }

    bool CreateMipped(uint32_t width, uint32_t height, uint32_t levels, VkFormat format, Image& out)
    {
        return CreateImage(width, height, levels, false, format, out);
    }

    bool CreateCube(uint32_t size, uint32_t levels, VkFormat format, Image& out)
    {
        // Square is a requirement of CUBE_COMPATIBLE, and the cube limit is its own, often smaller.
        if (s.ready && size > s.maxDimensionCube)
        {
            Write(Warn, "vulkan: texture: refusing a %u-texel cube map; this device allows 1..%u on a side", size, s.maxDimensionCube);
            ++s.stats.refused;
            return false;
        }
        return CreateImage(size, size, levels, true, format, out);
    }

    bool CreateTarget(uint32_t width, uint32_t height, uint32_t levels, bool cube, VkFormat format, Image& out)
    {
        if (cube && (width != height || (s.ready && width > s.maxDimensionCube))) return false;
        return CreateImage(width, height, levels, cube, format, out, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    }

    bool RegionBytes(const Image& img, uint32_t width, uint32_t height, uint64_t& rowBytes, uint32_t& rowCount)
    {
        const FormatInfo fmt = Describe(img.format);
        if (fmt.unitBytes == 0 || width == 0 || height == 0) return false;
        rowBytes = RowBytes(fmt, width);
        rowCount = RowCount(fmt, height);
        return true;
    }

    bool CanGenerateMips(VkFormat format)
    {
        if (!s.ready) return false;
        const FormatInfo fmt = Describe(format);
        // A blit cannot write a block-compressed level (BLIT_DST is never offered for BC).
        if (fmt.unitBytes == 0 || fmt.compressed) return false;
        VkFormatProperties props = {};
        s.iapi->GetPhysicalDeviceFormatProperties(s.physical, format, &props);
        const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        return (props.optimalTilingFeatures & need) == need;
    }

    void DestroyIdle(Image& img)
    {
        if (img.handle == VK_NULL_HANDLE)
        {
            img = Image();
            return;
        }
        const uint32_t i = FindOwned(img.handle);
        if (i == kNotOwned || !s.dapi || !s.device)
        {
            img = Image();   // not one of ours: forgetting is the only safe answer
            return;
        }
        if (s.owned[i].view != VK_NULL_HANDLE) s.dapi->DestroyImageView(s.device, s.owned[i].view, nullptr);
        s.dapi->DestroyImage(s.device, s.owned[i].handle, nullptr);
        memory::Free(s.owned[i].block);
        RemoveOwned(i);
        img = Image();
        ++s.stats.destroyed;
        s.stats.live = s.ownedCount;
    }

    uint32_t RecordRegions(VkCommandBuffer cmd, Image& img, VkBuffer src, const RegionCopy* regions, uint32_t count)
    {
        if (!s.ready || !cmd || src == VK_NULL_HANDLE || img.handle == VK_NULL_HANDLE || !regions || count == 0) return 0;
        const FormatInfo fmt = Describe(img.format);
        if (fmt.unitBytes == 0) return 0;

        constexpr uint32_t kBatch = 64;
        VkBufferImageCopy  copies[kBatch];
        uint32_t           n        = 0;
        uint32_t           recorded = 0;
        bool               began    = false;
        const bool         sampled  = img.layout == kSampled;
        const auto flush = [&]() {
            if (n == 0) return;
            s.dapi->CmdCopyBufferToImage(cmd, src, img.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, n, copies);
            recorded += n;
            n = 0;
        };
        for (uint32_t i = 0; i < count; ++i)
        {
            const RegionCopy& g = regions[i];
            if (g.level >= img.levels || g.layer >= img.layers) continue;
            const uint32_t levelW = LevelSide(img.extent.width, g.level);
            const uint32_t levelH = LevelSide(img.extent.height, g.level);
            if (g.width == 0 || g.height == 0 || uint64_t(g.x) + g.width > levelW || uint64_t(g.y) + g.height > levelH) continue;
            if (fmt.compressed)
            {
                // A copy may not start or stop inside a 4x4 block, except that an extent may run to
                // the level's edge (the last levels of a chain are narrower than a block). The
                // driver does not report a violation; it copies garbage.
                const bool originOk = g.x % kBlock == 0 && g.y % kBlock == 0;
                const bool widthOk  = g.width % kBlock == 0 || g.x + g.width == levelW;
                const bool heightOk = g.height % kBlock == 0 || g.y + g.height == levelH;
                if (!originOk || !widthOk || !heightOk) continue;
            }
            // bufferOffset must be a multiple of 4 and of the texel block size.
            if (g.srcOffset % 4 != 0 || g.srcOffset % fmt.unitBytes != 0) continue;
            if (!began)
            {
                // One barrier for the batch: every earlier read of the image (this buffer's draws,
                // frames in flight: write-after-read needs only the execution dependency) and an
                // earlier copy or clear (TRANSFER, with its writes) before the copies. Contents
                // outside the regions are kept, so the old layout is the tracked one; UNDEFINED
                // only for a never-filled image.
                VkPipelineStageFlags srcStage = sampled ? (kReaders | VK_PIPELINE_STAGE_TRANSFER_BIT) : VK_PIPELINE_STAGE_TRANSFER_BIT;
                if (img.layout != VK_IMAGE_LAYOUT_UNDEFINED && !sampled && img.layout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                    && img.layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
                    srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
                Transition(cmd, img.handle, img.levels, img.layers, img.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           srcStage, img.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
                began = true;
            }
            VkBufferImageCopy& c = copies[n++];
            c = VkBufferImageCopy{};
            c.bufferOffset                    = g.srcOffset;
            c.bufferRowLength                 = 0;   // tightly packed
            c.bufferImageHeight               = 0;
            c.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            c.imageSubresource.mipLevel       = g.level;
            c.imageSubresource.baseArrayLayer = g.layer;
            c.imageSubresource.layerCount     = 1;
            c.imageOffset = VkOffset3D{ static_cast<int32_t>(g.x), static_cast<int32_t>(g.y), 0 };
            c.imageExtent = VkExtent3D{ g.width, g.height, 1 };
            if (n == kBatch) flush();
        }
        if (!began) return 0;
        flush();
        Transition(cmd, img.handle, img.levels, img.layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kSampled,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   kReaders | VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
        img.layout = kSampled;
        s.stats.regions += recorded;
        ++s.stats.batches;
        return recorded;
    }

    static unsigned Quantize(float value, unsigned maximum)
    {
        if (!(value > 0.0f)) return 0;
        if (value >= 1.0f) return maximum;
        return static_cast<unsigned>(value * maximum + 0.5f);
    }

    // A single-colour BC block. All selectors use endpoint zero, except BC1's
    // transparent colour (selector three, equal endpoints). BC2 stores explicit
    // 4-bit alphas and BC3 has an independent 8-bit alpha endpoint.
    static void SolidBlock(VkFormat format, const float* rgba, uint8_t (&block)[16])
    {
        const bool bc1 = format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        uint8_t* rgb = block + (bc1 ? 0 : 8);
        unsigned color = (Quantize(rgba[0], 31) << 11) | (Quantize(rgba[1], 63) << 5) | Quantize(rgba[2], 31);
        rgb[0] = rgb[2] = static_cast<uint8_t>(color);
        rgb[1] = rgb[3] = static_cast<uint8_t>(color >> 8);
        if (bc1 && !Quantize(rgba[3], 1)) std::memset(rgb + 4, 0xff, 4);
        if (format == VK_FORMAT_BC2_UNORM_BLOCK) std::memset(block, Quantize(rgba[3], 15) * 0x11, 8);
        if (format == VK_FORMAT_BC3_UNORM_BLOCK) block[0] = block[1] = static_cast<uint8_t>(Quantize(rgba[3], 255));
    }

    static bool RecordCompressedClear(VkCommandBuffer cmd, Image& img, const FormatInfo& fmt, const float* rgba)
    {
        // Reuse the same staged bytes across all faces, mips and strips. A frame
        // owns and flushes this slice until the submission fence completes.
        const uint64_t baseRow = RowBytes(fmt, img.extent.width);
        const uint64_t bytes = std::min(baseRow * RowCount(fmt, img.extent.height),
                                        std::max<uint64_t>(baseRow, 64 * 1024));
        VkBuffer buffer;
        VkDeviceSize offset;
        void* mapped;
        if (!frame::Stage(bytes, buffer, offset, mapped)) return false;

        uint8_t block[16] = {};
        SolidBlock(img.format, rgba, block);
        auto* dst = static_cast<uint8_t*>(mapped);
        std::memcpy(dst, block, fmt.unitBytes);
        for (uint64_t filled = fmt.unitBytes; filled < bytes; filled *= 2)
            std::memcpy(dst + filled, dst, static_cast<size_t>(std::min(filled, bytes - filled)));

        RegionCopy regions[64];
        unsigned count = 0;
        for (uint32_t level = 0; level < img.levels; ++level)
        {
            const uint32_t width = LevelSide(img.extent.width, level);
            const uint32_t height = LevelSide(img.extent.height, level);
            const uint32_t strip = static_cast<uint32_t>(std::min<uint64_t>(height, bytes / RowBytes(fmt, width) * kBlock));
            for (uint32_t layer = 0; layer < img.layers; ++layer)
            {
                for (uint32_t y = 0; y < height; y += strip)
                {
                    regions[count++] = {level, 0, y, width, std::min(height - y, strip), layer, offset};
                    if (count == 64)
                    {
                        if (RecordRegions(cmd, img, buffer, regions, count) != count) return false;
                        count = 0;
                    }
                }
            }
        }
        return !count || RecordRegions(cmd, img, buffer, regions, count) == count;
    }

    bool RecordClear(VkCommandBuffer cmd, Image& img, const float* rgba)
    {
        if (!s.ready || !cmd || img.handle == VK_NULL_HANDLE) return false;
        const FormatInfo fmt = Describe(img.format);
        if (fmt.compressed)
        {
            static const float grey[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
            if (!RecordCompressedClear(cmd, img, fmt, rgba ? rgba : grey)) return false;
            ++s.stats.clears;
            return true;
        }
        // From UNDEFINED (the contents are discarded), after every earlier use of the image at any
        // stage: a recorded clear may follow anything in the buffer.
        Transition(cmd, img.handle, img.levels, img.layers, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        // Opaque mid grey unless a colour is named: a level that stays this way is recognisable
        // and neutral at a distance.
        VkClearColorValue colour = {};
        colour.float32[0] = 0.5f;
        colour.float32[1] = 0.5f;
        colour.float32[2] = 0.5f;
        colour.float32[3] = 1.0f;
        if (rgba)
            for (unsigned i = 0; i < 4; ++i) colour.float32[i] = rgba[i];
        const VkImageSubresourceRange range = WholeColour(img.levels, img.layers);
        s.dapi->CmdClearColorImage(cmd, img.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &range);
        Transition(cmd, img.handle, img.levels, img.layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kSampled,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   kReaders | VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);
        img.layout = kSampled;
        ++s.stats.clears;
        return true;
    }

    bool RecordGenerateMips(VkCommandBuffer cmd, Image& img)
    {
        if (!s.ready || !cmd || img.handle == VK_NULL_HANDLE || img.levels < 2) return false;

        // One subresource-range barrier for the chain's per-level steps; Transition() covers the
        // whole image, the right shape only for the first and the last step.
        const auto levels = [&](uint32_t base, uint32_t count, VkImageLayout from, VkImageLayout to,
                                VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) {
            VkImageMemoryBarrier b = {};
            b.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.srcAccessMask       = srcAccess;
            b.dstAccessMask       = dstAccess;
            b.oldLayout           = from;
            b.newLayout           = to;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image               = img.handle;
            b.subresourceRange    = WholeColour(img.levels, img.layers);
            b.subresourceRange.baseMipLevel = base;
            b.subresourceRange.levelCount   = count;
            s.dapi->CmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
        };

        // The whole image to TRANSFER_DST, contents kept, after every earlier sampler read and with
        // the level-0 upload's transfer write made available.
        Transition(cmd, img.handle, img.levels, img.layers, img.layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   kReaders | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT);

        // Level by level, every layer at once (a blit's layer range maps layer i to layer i, so a
        // cube's six faces filter independently). Level l - 1 becomes the source once written.
        for (uint32_t l = 1; l < img.levels; ++l)
        {
            levels(l - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
            VkImageBlit blit = {};
            blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel       = l - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount     = img.layers;
            blit.srcOffsets[1] = VkOffset3D{ static_cast<int32_t>(LevelSide(img.extent.width, l - 1)), static_cast<int32_t>(LevelSide(img.extent.height, l - 1)), 1 };
            blit.dstSubresource          = blit.srcSubresource;
            blit.dstSubresource.mipLevel = l;
            blit.dstOffsets[1] = VkOffset3D{ static_cast<int32_t>(LevelSide(img.extent.width, l)), static_cast<int32_t>(LevelSide(img.extent.height, l)), 1 };
            s.dapi->CmdBlitImage(cmd, img.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        }

        // Every level to the sampling layout: the sources from TRANSFER_SRC, the last from TRANSFER_DST.
        levels(0, img.levels - 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, kSampled, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, kReaders, VK_ACCESS_SHADER_READ_BIT);
        levels(img.levels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, kSampled, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, kReaders, VK_ACCESS_SHADER_READ_BIT);
        img.layout = kSampled;
        ++s.stats.chains;
        return true;
    }

    void GetStats(Stats& out) { out = s.stats; }
}
