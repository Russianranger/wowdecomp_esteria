// GxEffects over Vulkan (Effects.cpp): the device GxEffects::Get() hands out while the Vulkan
// device is up. Not in the original binary.
#pragma once

namespace gxvk::vk::effects
{
    /// Destroys what the passes made: pipelines, render passes, programs, descriptor pools, the
    /// constant rings, framebuffers. After the device is idle, before frame::Destroy(), the texture
    /// module and memory go. GxEffects::Get() returns null afterwards.
    void Shutdown();
}
