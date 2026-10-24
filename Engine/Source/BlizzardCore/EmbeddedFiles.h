#ifndef ENGINE_SOURCE_BLIZZARDCORE_EMBEDDEDFILES_H
#define ENGINE_SOURCE_BLIZZARDCORE_EMBEDDEDFILES_H

#include <cstdint>

// Files compiled into the executable and served by SFile as if they were in the archives.
// The data must outlive the process; names match case-insensitively with either slash.
void EmbeddedFileRegister(const char* name, const uint8_t* data, uint32_t size);
bool EmbeddedFileFind(const char* name, const uint8_t** data, uint32_t* size);

#endif
