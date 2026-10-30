add_library(XvidDecoder SHARED
    XvidDecoder.cpp
    XvidDecoder.def
)

set_target_properties(XvidDecoder PROPERTIES
    OUTPUT_NAME XvidDecoder
    RUNTIME_OUTPUT_DIRECTORY ${WOWDECOMP_RUNTIME_OUTPUT_DIRECTORY}
)

foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config)
    set_target_properties(XvidDecoder PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${WOWDECOMP_RUNTIME_OUTPUT_DIRECTORY})
endforeach ()

target_include_directories(XvidDecoder
    PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/ThirdParty/xvidcore/include
)

if (WOWDECOMP_CPU_ARM64)
    # An x64 .lib cannot be linked into ARM64. Build the same 1.3.7 codec
    # from pinned upstream source; do not discard movie playback.
    include(${CMAKE_SOURCE_DIR}/cmake/XvidPortable.cmake)
    target_link_libraries(XvidDecoder PRIVATE XvidPortable)
else ()
    target_link_libraries(XvidDecoder PRIVATE ${CMAKE_SOURCE_DIR}/ThirdParty/xvidcore/lib/x64/libxvidcore.lib)
endif ()

# The bundled desktop codec uses the release CRT even in Debug. Source-built
# ARM64 uses CMake's selected CRT consistently across adapter and codec.
if (NOT WOWDECOMP_CPU_ARM64)
    target_compile_options(XvidDecoder PRIVATE $<$<CONFIG:Debug>:/MD>)
endif ()
