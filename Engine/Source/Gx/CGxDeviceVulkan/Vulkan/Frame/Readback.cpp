// Copies and read-backs off the scene image: D3D's StretchRect from the back buffer into a texture,
// and the back buffer's pixels for a screenshot.
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame/Internal.hpp"

#include <cstring>

using namespace gxvk::vk::frame::detail;

namespace
{
    /// A whole-image layout transition of one image on the main buffer.
    void Transition(VkCommandBuffer cmd, const texture::Image& img, VkImageLayout from, VkImageLayout to,
                    VkPipelineStageFlags srcStage, VkAccessFlags srcAccess, VkPipelineStageFlags dstStage, VkAccessFlags dstAccess)
    {
        VkImageMemoryBarrier b = {};
        b.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcAccessMask                   = srcAccess;
        b.dstAccessMask                   = dstAccess;
        b.oldLayout                       = from;
        b.newLayout                       = to;
        b.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        b.image                           = img.handle;
        b.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount     = img.levels;
        b.subresourceRange.layerCount     = img.layers;
        s.dapi->CmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    constexpr VkPipelineStageFlags kReaders = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
}

namespace gxvk::vk::frame
{
    bool CopyFromScene(texture::Image& dst, uint32_t layer, uint32_t level, const VkRect2D& src, const VkRect2D& dstRect, bool linear)
    {
        return CopyFromImage(s.scene, dst, layer, level, src, dstRect, linear);
    }

    bool CopyFromTarget(texture::Image& dst, const VkRect2D& src, const VkRect2D& dstRect, bool linear)
    {
        return CopyFromImage(s.target.image ? *s.target.image : s.scene, dst, 0, 0, src, dstRect, linear);
    }

    bool CopyFromImage(const texture::Image& source, texture::Image& dst, uint32_t layer, uint32_t level, const VkRect2D& src, const VkRect2D& dstRect, bool linear)
    {
        if (dst.handle == VK_NULL_HANDLE || layer >= dst.layers || level >= dst.levels || &source == &dst) return false;
        VkCommandBuffer cmd = Outside();
        if (!cmd || source.handle == VK_NULL_HANDLE || source.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return false;

        // The scene from its resting layout to a copy source; the destination from whatever it
        // holds (a sampled texture, or an unfilled one) to a copy target, its other texels kept.
        Transition(cmd, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | kReaders, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        const VkImageLayout from = dst.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ? dst.layout : VK_IMAGE_LAYOUT_UNDEFINED;
        Transition(cmd, dst, from, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   kReaders | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

        VkImageBlit blit = {};
        blit.srcSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel       = 0;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount     = 1;
        blit.srcOffsets[0] = VkOffset3D{ src.offset.x, src.offset.y, 0 };
        blit.srcOffsets[1] = VkOffset3D{ src.offset.x + static_cast<int32_t>(src.extent.width), src.offset.y + static_cast<int32_t>(src.extent.height), 1 };
        blit.dstSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel       = level;
        blit.dstSubresource.baseArrayLayer = layer;
        blit.dstSubresource.layerCount     = 1;
        blit.dstOffsets[0] = VkOffset3D{ dstRect.offset.x, dstRect.offset.y, 0 };
        blit.dstOffsets[1] = VkOffset3D{ dstRect.offset.x + static_cast<int32_t>(dstRect.extent.width), dstRect.offset.y + static_cast<int32_t>(dstRect.extent.height), 1 };
        s.dapi->CmdBlitImage(cmd, source.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                             linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);

        Transition(cmd, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                   kReaders | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        Transition(cmd, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                   kReaders | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                   VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT);
        dst.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ++s.stats.sceneCopies;
        return true;
    }

    bool CopyTargetDepth(VkImage dst)
    {
        if (dst == VK_NULL_HANDLE) return false;
        VkCommandBuffer cmd = Outside();
        if (!cmd) return false;
        const VkExtent2D extent = Extent();
        const DepthBuffer* depth = nullptr;
        for (uint32_t i = 0; i < s.depthCount; ++i)
            if (s.depths[i].extent.width == extent.width && s.depths[i].extent.height == extent.height) depth = &s.depths[i];
        if (!depth || !depth->initialised) return false;

        // Depth only: the stencil is the engine's, and a sampled view reads one aspect.
        VkImageMemoryBarrier b[2] = {};
        for (VkImageMemoryBarrier& one : b)
        {
            one.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            one.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            one.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
            one.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            one.subresourceRange.levelCount = 1;
            one.subresourceRange.layerCount = 1;
        }
        if (s.depthFormat == VK_FORMAT_D24_UNORM_S8_UINT) b[0].subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        b[0].image         = depth->image;
        b[0].oldLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        b[0].newLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b[1].image         = dst;
        b[1].oldLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b[1].newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        s.dapi->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | kReaders,
                                   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, b);

        VkImageCopy copy = {};
        copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        copy.srcSubresource.layerCount = 1;
        copy.dstSubresource            = copy.srcSubresource;
        copy.extent                    = VkExtent3D{ extent.width, extent.height, 1 };
        s.dapi->CmdCopyImage(cmd, depth->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        b[0].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b[0].newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        b[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        b[1].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b[1].newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        s.dapi->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | kReaders,
                                   0, 0, nullptr, 0, nullptr, 2, b);
        return true;
    }

    bool ReadScene(const VkRect2D& rect, uint8_t* out)
    {
        return ReadImage(s.scene, 0, rect, out);
    }

    bool ReadImage(const texture::Image& img, uint32_t layer, const VkRect2D& rect, uint8_t* out)
    {
        if (!out || rect.extent.width == 0 || rect.extent.height == 0) return false;
        // Outside() first: it ends a pass drawing into img, which leaves img in its resting layout.
        VkCommandBuffer cmd = Outside();
        if (!cmd || img.handle == VK_NULL_HANDLE || img.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || layer >= img.layers) return false;
        if (rect.offset.x < 0 || rect.offset.y < 0 || rect.offset.x + rect.extent.width > img.extent.width
            || rect.offset.y + rect.extent.height > img.extent.height)
            return false;

        const VkDeviceSize bytes = static_cast<VkDeviceSize>(rect.extent.width) * rect.extent.height * 4;
        memory::Buffer host;
        if (!memory::CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, memory::Where::HostVisible, host) || !host.block.mapped)
        {
            memory::DestroyBuffer(host);
            return false;
        }

        Transition(cmd, img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | kReaders, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copy = {};
        copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.baseArrayLayer = layer;
        copy.imageSubresource.layerCount     = 1;
        copy.imageOffset                     = VkOffset3D{ rect.offset.x, rect.offset.y, 0 };
        copy.imageExtent                     = VkExtent3D{ rect.extent.width, rect.extent.height, 1 };
        s.dapi->CmdCopyImageToBuffer(cmd, img.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host.handle, 1, &copy);
        Transition(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                   kReaders | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        // The copy's write made visible to the host before the fence is waited.
        VkMemoryBarrier toHost = {};
        toHost.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        s.dapi->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0, nullptr, 0, nullptr);

        // The frame's work so far, submitted and waited: the read is synchronous, as D3D's LockRect
        // of the back buffer is.
        const uint64_t serial = s.serial;
        FinishFrame(false);
        ++s.stats.flushes;
        const bool done = WaitSerial(serial);
        if (done) std::memcpy(out, host.block.mapped, static_cast<size_t>(bytes));
        memory::DestroyBuffer(host);
        ++s.stats.sceneReads;
        return done;
    }

    bool BeginImageRead(const texture::Image& img, const VkRect2D& rect, uint32_t texelBytes, AsyncRead& out)
    {
        if (rect.extent.width == 0 || rect.extent.height == 0 || texelBytes == 0) return false;
        VkCommandBuffer cmd = Outside();
        if (!cmd || img.handle == VK_NULL_HANDLE || img.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) return false;
        if (rect.offset.x < 0 || rect.offset.y < 0 || rect.offset.x + rect.extent.width > img.extent.width
            || rect.offset.y + rect.extent.height > img.extent.height)
            return false;

        const VkDeviceSize bytes = static_cast<VkDeviceSize>(rect.extent.width) * rect.extent.height * texelBytes;
        memory::Buffer host;
        if (!memory::CreateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, memory::Where::HostVisible, host) || !host.block.mapped)
        {
            memory::DestroyBuffer(host);
            return false;
        }
        Transition(cmd, img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | kReaders, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copy = {};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageOffset                 = VkOffset3D{ rect.offset.x, rect.offset.y, 0 };
        copy.imageExtent                 = VkExtent3D{ rect.extent.width, rect.extent.height, 1 };
        s.dapi->CmdCopyImageToBuffer(cmd, img.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, host.handle, 1, &copy);
        Transition(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                   kReaders | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        VkMemoryBarrier toHost = {};
        toHost.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        s.dapi->CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &toHost, 0, nullptr, 0, nullptr);

        out.host   = host;
        out.serial = s.serial;
        out.bytes  = bytes;
        return true;
    }

    bool PollImageRead(AsyncRead& read, void* out)
    {
        if (read.serial == 0 || read.serial > s.completed) return false;
        std::memcpy(out, read.host.block.mapped, static_cast<size_t>(read.bytes));
        memory::DestroyBuffer(read.host);
        read.serial = 0;
        return true;
    }

    void CancelImageRead(AsyncRead& read)
    {
        if (read.serial == 0) return;
        RetireBuffer(read.host, read.serial);
        read.serial = 0;
    }
}
