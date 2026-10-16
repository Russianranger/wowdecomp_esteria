file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(Extension STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Extension
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)