#ifndef WOW_SOURCE_EXTENSION_EXTENSIONMGR_H
#define WOW_SOURCE_EXTENSION_EXTENSIONMGR_H

#include <cstdint>
#include <vector>
#include "WoW/Source/Extension/ExtensionScript.h"

template <class T>
class ExtensionRegistry {
    public:
    static std::vector<T*>& Scripts()
    {
        static std::vector<T*> scripts;
        return scripts;
    }

    static void Add(T* script)
    {
        Scripts().push_back(script);
    }

    static void Clear()
    {
        for (T* script : Scripts())
        {
            delete script;
        }

        Scripts().clear();
    }
};

class ExtensionMgr {
    public:
    static void Initialize();

    static void Shutdown();

    static void Add(ClientScript* script);

    static uint32_t GetScriptCount();

    static void Log(const char* format, ...);

    static void OnEnterWorld(uint32_t mapId);
    static void OnLeaveWorld();
};

#endif