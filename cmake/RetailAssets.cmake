get_filename_component(RETAIL_CLIENT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
# Keep upstream storage code out of default builds. A source checkout can be supplied for
# offline builds; otherwise download an immutable revision, never an upstream branch.
set(WOWDECOMP_CASCLIB_SOURCE_DIR "" CACHE PATH "Existing CascLib source checkout")
if (NOT WOWDECOMP_CASCLIB_SOURCE_DIR)
    if (CMAKE_VERSION VERSION_LESS 3.14)
        message(FATAL_ERROR "Retail assets require CMake 3.14+ or WOWDECOMP_CASCLIB_SOURCE_DIR")
    endif ()
    include(FetchContent)
    FetchContent_Declare(wow_casclib
        URL https://codeload.github.com/ladislav-zezula/CascLib/zip/38a34665624b8775bb875274b36191b21c38d97b
        URL_HASH SHA256=1b59deebc82a4eb75c6e9ef463a7644b22231a4b3b0657ad9afe0dd5205fee3f
    )
    FetchContent_GetProperties(wow_casclib)
    if (NOT wow_casclib_POPULATED)
        FetchContent_Populate(wow_casclib)
    endif ()
    set(WOWDECOMP_CASCLIB_SOURCE_DIR "${wow_casclib_SOURCE_DIR}")
endif ()

set(CASC_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(CASC_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(CASC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CASC_UNICODE OFF CACHE BOOL "" FORCE)
if (CMAKE_VERSION VERSION_GREATER_EQUAL 4.0)
    # Upstream still declares CMake 3.2; CMake 4 removed those compatibility policies.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif ()
add_subdirectory("${WOWDECOMP_CASCLIB_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/casclib" EXCLUDE_FROM_ALL)

# The pinned source uses the Windows-only LPDWORD spelling once in its otherwise
# portable parser. Compile a checked build-tree copy using the actual DWORD*
# signature; retain the original upstream source and all other target inputs.
if (NOT WIN32)
    set(casc_parser "${WOWDECOMP_CASCLIB_SOURCE_DIR}/src/CascFiles.cpp")
    file(READ "${casc_parser}" casc_parser_text)
    set(casc_parser_old "LPDWORD PtrBuildNumber = (LPDWORD)(pvParam);")
    string(REGEX MATCHALL "LPDWORD PtrBuildNumber = [(]LPDWORD[)][(]pvParam[)]" casc_parser_matches "${casc_parser_text}")
    list(LENGTH casc_parser_matches casc_parser_match_count)
    if (NOT casc_parser_match_count EQUAL 1)
        message(FATAL_ERROR "Pinned CascLib portable DWORD parser adaptation no longer matches exactly once")
    endif ()
    string(REPLACE "${casc_parser_old}" "DWORD* PtrBuildNumber = static_cast<DWORD*>(pvParam);" casc_parser_text "${casc_parser_text}")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/casclib-portable")
    set(casc_parser_copy "${CMAKE_BINARY_DIR}/casclib-portable/CascFiles.cpp")
    file(WRITE "${casc_parser_copy}" "${casc_parser_text}")
    get_target_property(casc_target_sources casc_static SOURCES)
    get_target_property(casc_target_source_dir casc_static SOURCE_DIR)
    set(casc_portable_sources)
    set(casc_parser_replaced 0)
    foreach (source IN LISTS casc_target_sources)
        if (NOT IS_ABSOLUTE "${source}")
            set(source "${casc_target_source_dir}/${source}")
        endif ()
        if (source STREQUAL casc_parser)
            list(APPEND casc_portable_sources "${casc_parser_copy}")
            math(EXPR casc_parser_replaced "${casc_parser_replaced} + 1")
        else ()
            list(APPEND casc_portable_sources "${source}")
        endif ()
    endforeach ()
    if (NOT casc_parser_replaced EQUAL 1)
        message(FATAL_ERROR "Pinned CascLib parser target input differs")
    endif ()
    set_property(TARGET casc_static PROPERTY SOURCES ${casc_portable_sources})
endif ()

add_library(RetailAssets STATIC
    "${RETAIL_CLIENT_ROOT}/Engine/Source/Retail/CascSource.cpp"
    "${RETAIL_CLIENT_ROOT}/Engine/Source/Retail/DecodedCache.cpp"
    "${RETAIL_CLIENT_ROOT}/Engine/Source/Retail/RetailModel.cpp"
    "${RETAIL_CLIENT_ROOT}/Engine/Source/Retail/RetailShader.cpp"
    "${RETAIL_CLIENT_ROOT}/Engine/Source/Retail/ClientTables.cpp"
    "${RETAIL_CLIENT_ROOT}/Engine/Source/Retail/Db2.cpp"
    "${RETAIL_CLIENT_ROOT}/Engine/Source/Retail/Appearance.cpp"
    "${RETAIL_CLIENT_ROOT}/Engine/Source/Retail/Compose.cpp"
    "${RETAIL_CLIENT_ROOT}/Engine/Source/Retail/BusyNotice.cpp"
)
target_include_directories(RetailAssets PUBLIC "${RETAIL_CLIENT_ROOT}")
target_compile_definitions(RetailAssets PUBLIC WOWDECOMP_RETAIL_ASSETS=1)
target_link_libraries(RetailAssets PRIVATE casc_static)

add_executable(RetailAssetProbe "${RETAIL_CLIENT_ROOT}/Tools/RetailAssets/probe.cpp")
target_link_libraries(RetailAssetProbe PRIVATE RetailAssets)
add_executable(RetailRaceProbe "${RETAIL_CLIENT_ROOT}/Tools/RetailAssets/races.cpp")
target_link_libraries(RetailRaceProbe PRIVATE RetailAssets)
add_executable(RetailComposeProbe "${RETAIL_CLIENT_ROOT}/Tools/RetailAssets/compose.cpp")
target_link_libraries(RetailComposeProbe PRIVATE RetailAssets)
add_executable(RetailAssetTests "${RETAIL_CLIENT_ROOT}/Tools/RetailAssets/tests.cpp")
target_link_libraries(RetailAssetTests PRIVATE RetailAssets)
add_executable(RetailAssetAcquire "${RETAIL_CLIENT_ROOT}/Tools/RetailAssets/acquire.cpp")
target_link_libraries(RetailAssetAcquire PRIVATE RetailAssets)
add_executable(CdnAssetDownload "${RETAIL_CLIENT_ROOT}/Tools/RetailAssets/download.cpp")
target_link_libraries(CdnAssetDownload PRIVATE RetailAssets)
add_executable(CdnSourceSmoke "${RETAIL_CLIENT_ROOT}/Tools/RetailAssets/source_smoke.cpp")
target_link_libraries(CdnSourceSmoke PRIVATE RetailAssets)
if (WIN32)
    add_executable(RetailShaderProbe "${RETAIL_CLIENT_ROOT}/Tools/RetailAssets/shader_probe.cpp"
        "${RETAIL_CLIENT_ROOT}/Engine/Source/Gx/CGxDeviceD3d11/ShaderTranslator.cpp")
    target_link_libraries(RetailShaderProbe PRIVATE RetailAssets d3dcompiler)
endif ()
enable_testing()
add_test(NAME retail-assets COMMAND RetailAssetTests)
