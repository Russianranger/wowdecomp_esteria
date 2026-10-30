file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

if (NOT WIN32)
    list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/CGxDevice(D3d|D3d9Ex|D3d11|OpenGL)/")
endif ()

# The Vulkan backend is its own target (CGxDeviceVulkan/CMakeLists.txt): it is C++20 where the rest of Gx is
# the C++17 the decompiled code is built as, and a C++ standard is a target property, not a source one.
list(FILTER PRIVATE_SOURCES EXCLUDE REGEX "/CGxDeviceVulkan/")

add_library(Gx STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Gx
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Gx
    PRIVATE
        d3d9.lib
        d3d11.lib
        dxgi.lib
        d3dcompiler.lib
)

if (WIN32)
    add_subdirectory(CGxDeviceVulkan)

    target_link_libraries(Gx
        PRIVATE
            opengl32.lib
        PUBLIC
            GxVulkan
    )
endif ()

if (TARGET nvapi)
    target_link_libraries(Gx PRIVATE nvapi)
endif ()

if (WOWDECOMP_RETAIL_ASSETS)
    target_compile_definitions(Gx PRIVATE WOWDECOMP_RETAIL_ASSETS=1)
endif ()
