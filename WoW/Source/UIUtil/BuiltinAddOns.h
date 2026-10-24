#ifndef WOW_SOURCE_UIUTIL_BUILTINADDONS_H
#define WOW_SOURCE_UIUTIL_BUILTINADDONS_H

class CStatus;

// One file compiled into the executable. `data` has a NUL after the last counted byte.
struct EmbeddedBlob {
    const char* name;
    const unsigned char* data;
    unsigned size;
};

extern const EmbeddedBlob g_builtinBlobs[];
extern const unsigned g_builtinBlobsCount;

const char* GetAddOnAccountName();

// Registers the built-in art with SFile, loads each built-in add-on's saved variables and runs its Lua.
void LoadBuiltinAddOns(CStatus* status);
// Writes the built-in add-ons' saved variables next to the loaded add-ons' own.
void SaveBuiltinAddOns();

#endif
