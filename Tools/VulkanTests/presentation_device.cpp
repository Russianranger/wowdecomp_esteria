#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Context.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Log.hpp"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
std::atomic<unsigned> errorMessages{0};
}

namespace gxvk::log {
void Write(Level level, const char* format, ...) {
    if (level == Error) errorMessages.fetch_add(1, std::memory_order_relaxed);
    if (level < Warn) return;
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::puts("");
}
}

namespace {
using namespace gxvk::vk;
constexpr uint64_t kTimeout = 5000000000ull;

void Check(bool pass, const char* operation) {
    if (!pass) { std::fprintf(stderr, "FAILED: %s\n", operation); std::exit(1); }
}

void Success(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "FAILED: %s returned %d\n", operation, static_cast<int>(result));
        std::exit(1);
    }
}

void Presented(VkResult result, const char* operation) {
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) Success(result, operation);
}

void PumpMessages() {
    MSG message;
    while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

// Exercise the engine's actual dynamically loaded dispatch, selected device,
// Win32 surface and swapchain. A render-pass clear needs no shaders or game data.
// This establishes successful clear/submit/present operations, not screenshot
// pixel fidelity, the engine's full draw pipeline, or hardware performance.
void ClearAndPresent(unsigned frames) {
    const auto* h = context::Get();
    Check(h && h->swapchain && h->extent.width && h->extent.height, "live nonempty swapchain");
    const auto& d = *h->dapi;
    uint32_t count = 0;
    Success(d.GetSwapchainImagesKHR(h->device, h->swapchain, &count, nullptr), "count swapchain images");
    Check(count > 0 && count <= 64, "bounded swapchain image count");
    std::vector<VkImage> images(count);
    Success(d.GetSwapchainImagesKHR(h->device, h->swapchain, &count, images.data()), "get swapchain images");
    Check(count == images.size(), "stable swapchain image count");

    VkAttachmentDescription attachment = {};
    attachment.format = h->format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo passInfo = {};
    passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    passInfo.attachmentCount = 1;
    passInfo.pAttachments = &attachment;
    passInfo.subpassCount = 1;
    passInfo.pSubpasses = &subpass;
    passInfo.dependencyCount = 1;
    passInfo.pDependencies = &dependency;
    VkRenderPass pass = VK_NULL_HANDLE;
    Success(d.CreateRenderPass(h->device, &passInfo, nullptr, &pass), "create clear render pass");

    std::vector<VkImageView> views(count);
    std::vector<VkFramebuffer> buffers(count);
    // A present-wait semaphore belongs to each swapchain image. Reacquiring that
    // image proves its previous presentation has released it before reuse.
    std::vector<VkSemaphore> rendered(count);
    VkSemaphoreCreateInfo semaphoreInfo = {};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo view = {};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = h->format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        Success(d.CreateImageView(h->device, &view, nullptr, &views[i]), "create swapchain image view");
        VkFramebufferCreateInfo buffer = {};
        buffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        buffer.renderPass = pass;
        buffer.attachmentCount = 1;
        buffer.pAttachments = &views[i];
        buffer.width = h->extent.width;
        buffer.height = h->extent.height;
        buffer.layers = 1;
        Success(d.CreateFramebuffer(h->device, &buffer, nullptr, &buffers[i]), "create clear framebuffer");
        Success(d.CreateSemaphore(h->device, &semaphoreInfo, nullptr, &rendered[i]), "create present semaphore");
    }

    VkSemaphore acquired = VK_NULL_HANDLE;
    Success(d.CreateSemaphore(h->device, &semaphoreInfo, nullptr, &acquired), "create acquire semaphore");
    VkFenceCreateInfo fenceInfo = {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence fence = VK_NULL_HANDLE;
    Success(d.CreateFence(h->device, &fenceInfo, nullptr, &fence), "create completion fence");
    VkCommandPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = h->graphicsFamily;
    VkCommandPool pool = VK_NULL_HANDLE;
    Success(d.CreateCommandPool(h->device, &poolInfo, nullptr, &pool), "create command pool");
    VkCommandBufferAllocateInfo allocation = {};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    Success(d.AllocateCommandBuffers(h->device, &allocation, &command), "allocate command buffer");

    for (unsigned frame = 0; frame < frames; ++frame) {
        PumpMessages();
        Success(d.WaitForFences(h->device, 1, &fence, VK_TRUE, kTimeout), "wait for preceding clear");
        uint32_t image = 0;
        Presented(d.AcquireNextImageKHR(h->device, h->swapchain, kTimeout, acquired,
                                       VK_NULL_HANDLE, &image), "acquire present image");
        Check(image < count, "acquired image is in swapchain");
        VkCommandBufferBeginInfo begin = {};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        Success(d.BeginCommandBuffer(command, &begin), "begin clear commands");
        VkClearValue clear = {};
        clear.color.float32[frame % 2 ? 2 : 0] = 1.0f;
        clear.color.float32[3] = 1.0f;
        VkRenderPassBeginInfo render = {};
        render.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render.renderPass = pass;
        render.framebuffer = buffers[image];
        render.renderArea.extent = h->extent;
        render.clearValueCount = 1;
        render.pClearValues = &clear;
        d.CmdBeginRenderPass(command, &render, VK_SUBPASS_CONTENTS_INLINE);
        d.CmdEndRenderPass(command);
        Success(d.EndCommandBuffer(command), "finish clear commands");
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit = {};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1;
        submit.pWaitSemaphores = &acquired;
        submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &command;
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores = &rendered[image];
        Success(d.ResetFences(h->device, 1, &fence), "reset completion fence");
        Success(d.QueueSubmit(h->graphicsQueue, 1, &submit, fence), "submit clear commands");
        VkPresentInfoKHR present = {};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &rendered[image];
        present.swapchainCount = 1;
        present.pSwapchains = &h->swapchain;
        present.pImageIndices = &image;
        VkResult imageResult = VK_ERROR_UNKNOWN;
        present.pResults = &imageResult;
        Presented(d.QueuePresentKHR(h->presentQueue, &present), "present cleared image");
        Presented(imageResult, "present swapchain result");
    }
    Success(d.WaitForFences(h->device, 1, &fence, VK_TRUE, kTimeout), "wait for final clear");
    Success(d.DeviceWaitIdle(h->device), "wait before swapchain fixture cleanup");
    std::printf("Presented %u alternating clear frames at %ux%u, format %d\n",
                frames, h->extent.width, h->extent.height, static_cast<int>(h->format));
    d.DestroyCommandPool(h->device, pool, nullptr);
    d.DestroyFence(h->device, fence, nullptr);
    d.DestroySemaphore(h->device, acquired, nullptr);
    for (uint32_t i = 0; i < count; ++i) {
        d.DestroySemaphore(h->device, rendered[i], nullptr);
        d.DestroyFramebuffer(h->device, buffers[i], nullptr);
        d.DestroyImageView(h->device, views[i], nullptr);
    }
    d.DestroyRenderPass(h->device, pass, nullptr);
}
}

int main() {
    using namespace gxvk::vk;
    HWND window = CreateWindowExA(0, "STATIC", "Esteria Vulkan presentation fixture",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 256, 192, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    Check(window != nullptr, "create visible Win32 window");
    PumpMessages();
    context::SetVsync(true);
    context::SetValidation(true);
    Check(context::Bring(window) == context::Attempt::Done, "initialize real Vulkan surface/device/swapchain");
    const bool validationActive = context::ValidationActive();
    std::printf("Vulkan validation callback: %s\n", validationActive ? "active" : "unavailable (not a validation-layer pass)");
    char adapter[256] = {}, driver[128] = {};
    context::AdapterText(adapter, sizeof(adapter), driver, sizeof(driver));
    std::printf("Vulkan adapter: %s; %s\n", adapter, driver);
    ClearAndPresent(8);
    const uint32_t generation = context::SwapchainGeneration();
    const VkExtent2D before = context::Get()->extent;
    Check(SetWindowPos(window, nullptr, 0, 0, 384, 256, SWP_NOZORDER | SWP_NOMOVE) != 0, "resize fixture window");
    PumpMessages();
    context::Rebind(window);
    Check(context::SwapchainGeneration() != generation && context::Get() && context::Get()->swapchain,
          "rebuild swapchain after resize");
    Check(context::Get()->extent.width != before.width || context::Get()->extent.height != before.height,
          "resized swapchain extent changed");
    ClearAndPresent(8);
    context::LogValidationSummary();
    context::Teardown();
    Check(errorMessages.load(std::memory_order_relaxed) == 0, "no renderer or validation callback errors");
    Check(DestroyWindow(window) != 0, "destroy fixture window");
    std::puts("PASS: Vulkan Win32 clear/submit/present completed 16 frames across swapchain resize");
}
