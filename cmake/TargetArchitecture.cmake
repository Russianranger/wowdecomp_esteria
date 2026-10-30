# Pointer width is not an instruction-set identifier: Windows ARM64 is LLP64
# just like Windows x64. Keep WOWDECOMP_ARCH_64 for data-layout guards only.
# Prefer the selected compiler target over the host/system processor: a build
# may cross-compile x64 on an ARM64 workstation (or ARM64 on x64).
if (CMAKE_GENERATOR_PLATFORM)
    set(_wow_target_arch "${CMAKE_GENERATOR_PLATFORM}")
elseif (MSVC_CXX_ARCHITECTURE_ID)
    set(_wow_target_arch "${MSVC_CXX_ARCHITECTURE_ID}")
else ()
    set(_wow_target_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif ()
string(TOLOWER "${_wow_target_arch}" _wow_target_arch)
if (_wow_target_arch MATCHES "arm64ec")
    message(FATAL_ERROR "ARM64EC is not yet qualified. Select native ARM64 or x64.")
elseif (_wow_target_arch MATCHES "^(arm64|aarch64)(,.*)?$")
    set(WOWDECOMP_CPU_ARM64 ON)
    add_compile_definitions(WOWDECOMP_CPU_ARM64=1)
elseif (_wow_target_arch MATCHES "^(x64|amd64|x86_64|x86|win32|i[3-6]86)(,.*)?$")
    set(WOWDECOMP_CPU_X86 ON)
    add_compile_definitions(WOWDECOMP_CPU_X86=1)
else ()
    message(FATAL_ERROR "Unqualified CPU architecture: ${_wow_target_arch}")
endif ()
unset(_wow_target_arch)
