// Run the actual addon harness against the same patched Lua 5.1 library as the native client.
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lualib.h"
#include <cstdio>
#include <cassert>
#include <chrono>
#include <cstdint>

// Standalone hosts provide the engine's profiling clock and pointer validation hooks.
int64_t OsGetAsyncClocksPerSecond() { return 1000000000; }
uint64_t OsGetAsyncTimeClocks() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
void OsValidateFunctionPointer(void const* pointer) { assert(pointer); }

// WoW omits the global filesystem loader; only this standalone test host supplies one.
static int LoadFixture(lua_State* state) {
    char const* path = luaL_checkstring(state, 1);
    if (luaL_loadfile(state, path)) return lua_error(state);
    return 1;
}

static int PrintFixture(lua_State* state) {
    for (int i = 1; i <= lua_gettop(state); ++i) {
        char const* text = lua_tostring(state, i);
        std::printf("%s%s", i > 1 ? "\t" : "", text ? text : luaL_typename(state, i));
    }
    std::printf("\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    lua_State* state = luaL_newstate();
    if (!state) return 3;
    luaL_openlibs(state);
    lua_register(state, "loadfile", LoadFixture);
    lua_register(state, "print", PrintFixture);
    lua_createtable(state, argc - 1, 0);
    for (int i = 1; i < argc; ++i) {
        lua_pushstring(state, argv[i]);
        lua_rawseti(state, -2, i - 1);
    }
    lua_setglobal(state, "arg");
    int result = luaL_loadfile(state, argv[1]);
    if (!result) result = lua_pcall(state, 0, LUA_MULTRET, 0);
    if (result) std::fprintf(stderr, "%s\n", lua_tostring(state, -1));
    else std::printf("Native runtime: %s\n", LUA_RELEASE);
    lua_close(state);
    return result ? 1 : 0;
}
