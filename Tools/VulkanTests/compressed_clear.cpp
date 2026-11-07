#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture/Internal.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace gxvk::vk;
namespace {
    void Check(bool ok, const char* why) {
        if (!ok) { std::fprintf(stderr, "FAILED: %s\n", why); std::exit(1); }
    }

    texture::Image img;
    std::vector<uint8_t> stage;
    std::vector<VkBufferImageCopy> copies;
    VkImageLayout layout;
    unsigned clears;
    float clearValue[4];
    bool failStage;
    const auto cmd = reinterpret_cast<VkCommandBuffer>(uintptr_t(1));
    const auto buffer = reinterpret_cast<VkBuffer>(uintptr_t(2));
    constexpr VkDeviceSize offset = 32;

    VKAPI_ATTR void VKAPI_CALL Copy(VkCommandBuffer c, VkBuffer b, VkImage image, VkImageLayout target,
                                    uint32_t count, const VkBufferImageCopy* regions) {
        Check(c == cmd && b == buffer && image == img.handle && layout == target &&
              target == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, "copy layout and owned staging");
        copies.insert(copies.end(), regions, regions + count);
    }
    VKAPI_ATTR void VKAPI_CALL Barrier(VkCommandBuffer, VkPipelineStageFlags, VkPipelineStageFlags,
        VkDependencyFlags, uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*,
        uint32_t count, const VkImageMemoryBarrier* b) {
        Check(count == 1 && b->image == img.handle && b->oldLayout == layout &&
              b->subresourceRange.layerCount == img.layers && b->subresourceRange.levelCount == img.levels,
              "tracked whole image layout");
        layout = b->newLayout;
    }
    VKAPI_ATTR void VKAPI_CALL Clear(VkCommandBuffer, VkImage image, VkImageLayout dst, const VkClearColorValue* v,
                                     uint32_t count, const VkImageSubresourceRange* r) {
        Check(img.format == VK_FORMAT_R8G8B8A8_UNORM && image == img.handle && count == 1 &&
              layout == dst && r->layerCount == img.layers && r->levelCount == img.levels, "legal colour clear");
        std::copy(v->float32, v->float32 + 4, clearValue);
        ++clears;
    }

    void Start(VkFormat format) {
        img = {};
        img.handle = reinterpret_cast<VkImage>(uintptr_t(3));
        img.format = format;
        img.extent = {1027, 513};
        img.levels = 11;
        img.layers = 6;
        copies.clear();
        stage.clear();
        layout = img.layout;
        clears = 0;
    }

    void TestSolid(VkFormat format, const float* rgba, std::array<uint8_t,16> block, unsigned bytes) {
        Start(format);
        Check(texture::RecordClear(cmd, img, rgba), "record compressed clear");
        Check(layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && layout == img.layout && !clears,
              "all compressed fills are ready for sampling");
        Check(!stage.empty() && stage.size() <= 65536 && stage.size() % bytes == 0, "bounded aligned staging");
        for (size_t i = 0; i < stage.size(); i += bytes)
            Check(std::equal(block.begin(), block.begin() + bytes, stage.begin() + i), "encoded colour/alpha");
        // Every strip is contiguous, covers the whole level width and fits the single staged slice.
        std::vector<unsigned> nextY(img.levels * img.layers);
        for (const auto& r : copies) {
            const auto& sub = r.imageSubresource;
            Check(sub.mipLevel < img.levels && sub.baseArrayLayer < img.layers && sub.layerCount == 1 &&
                  sub.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT, "copy subresource");
            const unsigned w = std::max(1u, img.extent.width >> sub.mipLevel);
            const unsigned h = std::max(1u, img.extent.height >> sub.mipLevel);
            auto& next = nextY[sub.baseArrayLayer * img.levels + sub.mipLevel];
            Check(r.imageOffset.x == 0 && r.imageOffset.z == 0 && unsigned(r.imageOffset.y) == next &&
                  next % 4 == 0 && r.imageExtent.width == w && r.imageExtent.depth == 1 &&
                  r.imageExtent.height && next + r.imageExtent.height <= h, "contiguous aligned strip");
            Check(r.bufferOffset == offset && !r.bufferRowLength && !r.bufferImageHeight &&
                  (w + 3) / 4 * ((r.imageExtent.height + 3) / 4) * bytes <= stage.size(), "bounded copy");
            next += r.imageExtent.height;
        }
        for (size_t i = 0; i < nextY.size(); ++i)
            Check(nextY[i] == std::max(1u, img.extent.height >> (i % img.levels)), "all mips/layers filled");
    }
}

namespace gxvk::log { void Write(Level, const char*, ...) {} }
namespace gxvk::vk::context { const Handles* Get() { return nullptr; } }
namespace gxvk::vk::memory { void Free(const Block&) {} }
namespace gxvk::vk::texture::detail {
    bool CreateImage(uint32_t, uint32_t, uint32_t, bool, VkFormat, Image&, VkImageUsageFlags) { return false; }
}
namespace gxvk::vk::frame {
    bool Stage(VkDeviceSize n, VkBuffer& b, VkDeviceSize& at, void*& dst) {
        if (failStage) return false;
        Check(stage.empty(), "single staging reservation");
        stage.resize(static_cast<size_t>(n), 0xcd);
        b = buffer; at = offset; dst = stage.data(); return true;
    }
}

int main() {
    loader::Device api{};
    api.CmdClearColorImage = Clear;
    api.CmdPipelineBarrier = Barrier;
    api.CmdCopyBufferToImage = Copy;
    texture::detail::s.ready = true;
    texture::detail::s.dapi = &api;
    const float rgba[4] = { 1, 0, 1, 0.25f };
    TestSolid(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, nullptr, {0x10,0x84,0x10,0x84}, 8);
    TestSolid(VK_FORMAT_BC1_RGBA_UNORM_BLOCK, rgba, {0x1f,0xf8,0x1f,0xf8,255,255,255,255}, 8);
    TestSolid(VK_FORMAT_BC2_UNORM_BLOCK, rgba, {0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x1f,0xf8,0x1f,0xf8}, 16);
    TestSolid(VK_FORMAT_BC3_UNORM_BLOCK, rgba, {64,64,0,0,0,0,0,0,0x1f,0xf8,0x1f,0xf8}, 16);
    Start(VK_FORMAT_BC3_UNORM_BLOCK);
    failStage = true;
    Check(!texture::RecordClear(cmd, img, nullptr) && layout == VK_IMAGE_LAYOUT_UNDEFINED && copies.empty() &&
          !clears, "failed staging records no Vulkan work");
    Start(VK_FORMAT_R8G8B8A8_UNORM);
    Check(texture::RecordClear(cmd, img, rgba) && clears == 1 && copies.empty() && stage.empty() &&
          std::equal(rgba, rgba + 4, clearValue) && layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
          "uncompressed clear retains requested colour without staging");
}
