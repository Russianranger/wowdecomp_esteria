#include "BuiltinAddOns.h"
#include <cstring>
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/BlizzardCore/EmbeddedFiles.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Os/OsFile.h"
#include "Engine/Source/Storm/H/storm.h"

// The saved-variable files keep the names the stand-alone add-ons used, so existing settings carry over.
static const struct {
    const char* name;
    const char* savedVariable;
} s_builtinAddOns[] = {
    { "WowDecompBodyShape", "WowDecompBodyShapeDB" },
    { "EsteriaInventory", "EsteriaInventoryDB" },
};

static const EmbeddedBlob* FindLua(const char* name) {
    char key[128];
    SStrPrintf(key, sizeof(key), "lua:%s", name);

    for (unsigned i = 0; i < g_builtinBlobsCount; i++) {
        if (!strcmp(g_builtinBlobs[i].name, key)) {
            return &g_builtinBlobs[i];
        }
    }

    return nullptr;
}

void LoadBuiltinAddOns(CStatus* status) {
    for (unsigned i = 0; i < g_builtinBlobsCount; i++) {
        if (!strncmp(g_builtinBlobs[i].name, "file:", 5)) {
            EmbeddedFileRegister(g_builtinBlobs[i].name + 5, g_builtinBlobs[i].data, g_builtinBlobs[i].size);
        }
    }

    for (const auto& addon : s_builtinAddOns) {
        const EmbeddedBlob* lua = FindLua(addon.name);

        if (!lua) {
            continue;
        }

        char path[260];
        SStrPrintf(path, sizeof(path), "WTF\\Account\\%s\\SavedVariables\\%s.lua", GetAddOnAccountName(), addon.name);

        if (SFile::FileExistsEx(path, 1)) {
            FrameScript_LoadVariables(path, status);
        }

        FrameScript_Execute(reinterpret_cast<const char*>(lua->data), addon.name, nullptr);
    }
}

void SaveBuiltinAddOns() {
    for (const auto& addon : s_builtinAddOns) {
        char path[260];
        SStrPrintf(path, sizeof(path), "WTF\\Account\\%s\\SavedVariables\\%s.lua", GetAddOnAccountName(), addon.name);

        if (OsFileExists(path)) {
            char backup[260];
            SStrPrintf(backup, sizeof(backup), "%s.bak", path);
            OsMoveFile(path, backup);
        }

        HOSFILE file = OsCreateFile(path, OS_GENERIC_WRITE, 0, OS_CREATE_ALWAYS, OS_FILE_ATTRIBUTE_NORMAL, OS_FILE_TYPE_ANY);

        if (file == OS_INVALID_FILE) {
            continue;
        }

        FrameScript_PrintVariable(file, addon.savedVariable);

        uint32_t written;
        OsWriteFile(file, "\r\n", 2, &written);
        OsCloseFile(file);
    }
}
