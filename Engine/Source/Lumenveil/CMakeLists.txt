# Lumenveil: the controller (Lumenveil.cpp) and the mod's ported parts (Core/, GPL-2.0, see Core/README.md).
file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(Lumenveil STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Lumenveil
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

# The vendored files are kept as upstream wrote them.
target_compile_definitions(Lumenveil PRIVATE WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
