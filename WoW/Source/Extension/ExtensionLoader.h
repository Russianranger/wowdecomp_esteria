#ifndef WOW_SOURCE_EXTENSION_EXTENSIONLOADER_H
#define WOW_SOURCE_EXTENSION_EXTENSIONLOADER_H

#include <cstdint>

struct EXTENSIONENTRY
{
    const char* m_name;
    void (*m_add)();
};

extern const EXTENSIONENTRY g_extensionEntries[];
extern const uint32_t g_extensionCount;

#endif