#ifndef ENGINE_SOURCE_OS_PROCESSOR_H
#define ENGINE_SOURCE_OS_PROCESSOR_H

#include <cstdint>

bool OsSupportsClientMath();
uint32_t OsGetProcessorFeatures();
uint32_t OsGetProcessorFeaturesEx(int32_t& vendor);
uint64_t OsGetProcessorTicksPerSecond();

#endif
