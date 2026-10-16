option(WOWDECOMP_EXTENDED_LIMITS "Enable non-stock player/container/class formats (matching server required)" OFF)
set(WOWDECOMP_QUEST_LOG_SLOTS 25 CACHE STRING "Active quest capacity: 25..1000 (25 = stock)")
set(WOWDECOMP_CONTAINER_SLOTS 36 CACHE STRING "Container capacity: 36..254 (36 = stock; does not create larger bags)")
set(WOWDECOMP_MAX_CLASSES 32 CACHE STRING "Maximum class ID: 32..254 (above 32 requires extended DBC/server masks)")

foreach(limit WOWDECOMP_QUEST_LOG_SLOTS WOWDECOMP_CONTAINER_SLOTS WOWDECOMP_MAX_CLASSES)
    if (NOT "${${limit}}" MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "${limit} must be a positive integer")
    endif ()
endforeach ()
if (WOWDECOMP_QUEST_LOG_SLOTS LESS 25 OR WOWDECOMP_QUEST_LOG_SLOTS GREATER 1000)
    message(FATAL_ERROR "WOWDECOMP_QUEST_LOG_SLOTS must be between 25 and 1000")
endif ()
if (WOWDECOMP_MAX_CLASSES LESS 32 OR WOWDECOMP_MAX_CLASSES GREATER 254)
    message(FATAL_ERROR "WOWDECOMP_MAX_CLASSES must be between 32 and 254")
endif ()
if (NOT WOWDECOMP_EXTENDED_LIMITS AND NOT WOWDECOMP_MAX_CLASSES EQUAL 32)
    message(FATAL_ERROR "Non-stock class masks require WOWDECOMP_EXTENDED_LIMITS=ON")
endif ()
if (WOWDECOMP_CONTAINER_SLOTS LESS 36 OR WOWDECOMP_CONTAINER_SLOTS GREATER 254)
    message(FATAL_ERROR "WOWDECOMP_CONTAINER_SLOTS must be between 36 and 254")
endif ()
if (NOT WOWDECOMP_EXTENDED_LIMITS AND (NOT WOWDECOMP_QUEST_LOG_SLOTS EQUAL 25 OR NOT WOWDECOMP_CONTAINER_SLOTS EQUAL 36))
    message(FATAL_ERROR "Non-stock capacities require WOWDECOMP_EXTENDED_LIMITS=ON")
endif ()

math(EXPR player_dwords "1326 + 5 * (${WOWDECOMP_QUEST_LOG_SLOTS} - 25)")
math(EXPR required_masks "(${player_dwords} + 31) / 32")
set(WOWDECOMP_MAX_UPDATE_MASKS "${required_masks}" CACHE STRING "Accepted update-mask dwords: required capacity..255")
if (NOT "${WOWDECOMP_MAX_UPDATE_MASKS}" MATCHES "^[1-9][0-9]*$" OR WOWDECOMP_MAX_UPDATE_MASKS LESS required_masks OR WOWDECOMP_MAX_UPDATE_MASKS GREATER 255)
    message(FATAL_ERROR "WOWDECOMP_MAX_UPDATE_MASKS must be ${required_masks}..255; clear the cache entry to derive it from quest capacity")
endif ()
if (NOT WOWDECOMP_EXTENDED_LIMITS AND NOT WOWDECOMP_MAX_UPDATE_MASKS EQUAL 42)
    message(FATAL_ERROR "Non-stock masks require WOWDECOMP_EXTENDED_LIMITS=ON")
endif ()
add_compile_definitions(
    WOWDECOMP_QUEST_LOG_SLOTS=${WOWDECOMP_QUEST_LOG_SLOTS}
    WOWDECOMP_CONTAINER_SLOTS=${WOWDECOMP_CONTAINER_SLOTS}
    WOWDECOMP_MAX_UPDATE_MASKS=${WOWDECOMP_MAX_UPDATE_MASKS}
    WOWDECOMP_MAX_CLASSES=${WOWDECOMP_MAX_CLASSES}
)

if (NOT WOWDECOMP_QUEST_LOG_SLOTS EQUAL 25 OR NOT WOWDECOMP_CONTAINER_SLOTS EQUAL 36 OR NOT WOWDECOMP_MAX_CLASSES EQUAL 32)
    find_program(WOWDECOMP_PYTHON NAMES python3 python REQUIRED)
    set(limits_generated "${CMAKE_BINARY_DIR}/generated/limits")
    file(MAKE_DIRECTORY "${limits_generated}")
    execute_process(
        COMMAND "${WOWDECOMP_PYTHON}" "${CMAKE_SOURCE_DIR}/Tools/gen_descriptors.py"
            --quest-slots "${WOWDECOMP_QUEST_LOG_SLOTS}"
            --container-slots "${WOWDECOMP_CONTAINER_SLOTS}"
            --max-classes "${WOWDECOMP_MAX_CLASSES}"
            --output "${limits_generated}"
        RESULT_VARIABLE limits_result
    )
    if (NOT limits_result EQUAL 0)
        message(FATAL_ERROR "Extended-limit descriptor generation failed")
    endif ()
    include_directories("${limits_generated}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/Tools/gen_descriptors.py"
        "${CMAKE_SOURCE_DIR}/Tools/extend_class_masks.py"
        "${CMAKE_SOURCE_DIR}/WoW/Source/Object/Player.cpp"
        "${CMAKE_SOURCE_DIR}/WoW/Source/Object/Container.cpp"
        "${CMAKE_SOURCE_DIR}/WoW/Source/ObjectMgrClient/Mirror.cpp")
    message(STATUS "Extended limits: ${WOWDECOMP_QUEST_LOG_SLOTS} quests, ${WOWDECOMP_CONTAINER_SLOTS} container slots, class IDs through ${WOWDECOMP_MAX_CLASSES}, ${WOWDECOMP_MAX_UPDATE_MASKS} update masks; matching server required")
endif ()
