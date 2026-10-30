#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Context.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Memory.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Log.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace gxvk::log {
void Write(Level level, const char* format, ...) {
    if (level < Warn) return;
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::puts("");
}
}

namespace {
void Check(bool pass, const char* message) {
    if (!pass) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
}
}

int main() {
    using namespace gxvk::vk;
    // Hidden window, no game data or server connection required. No draws are submitted, so
    // DestroyIdle is valid immediately and resource accounting can be checked exactly.
    HWND window = CreateWindowExA(0, "STATIC", "Vulkan texture regression", WS_OVERLAPPEDWINDOW,
                                  0, 0, 128, 128, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    Check(window != nullptr, "create hidden window");
    Check(context::Bring(window) == context::Attempt::Done, "initialize Vulkan device");
    char adapter[256] = {}, driver[128] = {};
    context::AdapterText(adapter, sizeof(adapter), driver, sizeof(driver));
    std::printf("Vulkan adapter: %s; %s\n", adapter, driver);
    Check(memory::Init() && texture::Init(), "initialize memory and textures");
    std::vector<texture::Image> images(20000);
    for (auto& image : images)
        Check(texture::CreateMipped(1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, image), "create beyond 16384 live images");
    texture::Stats stats;
    texture::GetStats(stats);
    Check(stats.live == images.size() && stats.refused == 0, "all 20000 images tracked without refusals");
    for (size_t i = 0; i < images.size(); i += 2) texture::DestroyIdle(images[i]);
    texture::GetStats(stats);
    Check(stats.live == images.size() / 2, "release alternating images after growth");
    for (size_t i = 0; i < images.size(); i += 2)
        Check(texture::CreateMipped(1, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, images[i]), "reuse entries after release");
    for (auto& image : images) texture::DestroyIdle(image);
    texture::GetStats(stats);
    Check(stats.live == 0 && stats.created == 30000 && stats.destroyed == 30000 && stats.refused == 0,
          "every GPU image is released and accounted for");
    texture::Shutdown();
    memory::Shutdown();
    context::Teardown();
    DestroyWindow(window);
    std::puts("PASS: Vulkan driver created 20000 simultaneous images; all 30000 create/destroy operations accounted for");
}
