// The Vulkan tier's log: one function over the engine's gx.log (CGxDevice::Log).
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Log.hpp"

#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {
    struct DiagnosticLog {
        std::mutex mutex;
        FILE* file = nullptr;
        bool attempted = false;
        ~DiagnosticLog() { if (file) std::fclose(file); }
    };
}

namespace gxvk::log
{
    void Write(Level level, const char* format, ...)
    {
        static const char* const kTags[] = { "debug", "info", "WARN", "ERROR" };

        char line[2048];
        va_list args;
        va_start(args, format);
        std::vsnprintf(line, sizeof line, format, args);
        va_end(args);

        // CGxDevice::Log is a no-op until GxLogOpen() has run, which ConsoleDeviceInitialize does
        // before the device is created; nothing here needs to know.
        CGxDevice::Log("[%s] %s", kTags[level], line);

        // Keep a renderer-specific log: other clients/tools in the same development folder
        // open gx.log too. Flush each infrequent diagnostic so a crash retains its cause.
        static DiagnosticLog diagnostic;
        std::lock_guard<std::mutex> lock(diagnostic.mutex);
        if (!diagnostic.attempted) {
            diagnostic.attempted = true;
            char path[MAX_PATH + 32];
            const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
            if (length && length < MAX_PATH) {
                if (char* slash = std::strrchr(path, '\\')) {
                    std::strcpy(slash + 1, "Logs");
                    CreateDirectoryA(path, nullptr);
                    std::strcat(path, "\\vulkan.log");
                    diagnostic.file = std::fopen(path, "wt");
                }
            }
        }
        if (diagnostic.file) {
            std::fprintf(diagnostic.file, "[%s] %s\n", kTags[level], line);
            std::fflush(diagnostic.file);
        }
    }
}
