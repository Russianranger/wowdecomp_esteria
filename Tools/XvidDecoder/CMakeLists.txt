add_library(XvidDecoder SHARED
    XvidDecoder.cpp
    XvidDecoder.def
)

set_target_properties(XvidDecoder PROPERTIES
    OUTPUT_NAME XvidDecoder
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin/Win
)

foreach (config ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
    string(TOUPPER ${config} config)
    set_target_properties(XvidDecoder PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} ${CMAKE_SOURCE_DIR}/bin/Win)
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

target_compile_options(XvidDecoder
    PRIVATE
        $<$<CONFIG:Debug>:/MD>
)
