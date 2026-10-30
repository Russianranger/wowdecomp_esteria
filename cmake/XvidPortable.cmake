# Xvid 1.3.7, official release archive. All decoder functionality uses the
# upstream generic C kernels; no x86 assembly or binary import library.
include(FetchContent)
FetchContent_Declare(xvid137
    URL https://downloads.xvid.com/downloads/xvidcore-1.3.7.tar.gz
    URL_HASH SHA256=abbdcbd39555691dd1c9b4d08f0a031376a3b211652c0d8b3b8aa9be1303ce2d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(xvid137)
# Patch a build-tree copy, never an override/source checkout supplied by users.
set(_xvid_source "${CMAKE_CURRENT_BINARY_DIR}/xvid-portable")
file(COPY "${xvid137_SOURCE_DIR}/src/" DESTINATION "${_xvid_source}")
file(READ "${_xvid_source}/portab.h" _portab)
# VS 2010 and later supply stdint.h. Upstream's legacy type-name macros
# corrupt its typedef declarations when modern Windows headers include it.
# Keep the old compiler/Watcom fallback, but use the standard types on MSVC.
set(_portab_types_old "#if defined(_MSC_VER) || defined (__WATCOMC__)")
string(FIND "${_portab}" "${_portab_types_old}" _portab_types_guard)
if (_portab_types_guard EQUAL -1)
    message(FATAL_ERROR "Xvid source does not match the pinned 1.3.7 fixed-width type patch")
endif ()
string(REPLACE "${_portab_types_old}"
    "#if defined(_MSC_VER) && _MSC_VER >= 1600\n#    include <stdint.h>\n#elif defined(_MSC_VER) || defined (__WATCOMC__)"
    _portab "${_portab}")
# stdint.h also owns intptr_t; do not redefine it as a macro on modern MSVC.
foreach (_portab_intptr_type int32_t int64_t)
    set(_portab_intptr_old "#    define intptr_t ${_portab_intptr_type}\n#    define _INTPTR_T_DEFINED")
    string(FIND "${_portab}" "${_portab_intptr_old}" _portab_intptr_guard)
    if (_portab_intptr_guard EQUAL -1)
        message(FATAL_ERROR "Xvid source does not match the pinned 1.3.7 pointer type patch")
    endif ()
    string(REPLACE "${_portab_intptr_old}"
        "#    if !defined(_MSC_VER) || _MSC_VER < 1600\n${_portab_intptr_old}\n#    endif"
        _portab "${_portab}")
endforeach ()
# Upstream writes two pointer-sized affinity masks into DWORDs on Win64.
string(REPLACE "DWORD p_aff, s_aff, r = 0;" "DWORD_PTR p_aff = 0, s_aff = 0; DWORD r = 0;" _portab "${_portab}")
file(WRITE "${_xvid_source}/portab.h" "${_portab}")
# Upstream's GMC log2 helper assumes every MSVC target except x64 accepts
# x86 inline assembly. The generic build must use the existing C loop on
# MSVC ARM64 too (and on x64 when testing these same generic kernels).
file(READ "${_xvid_source}/motion/gmc.c" _gmc)
set(_gmc_old "#if !defined(_MSC_VER) || defined(ARCH_IS_X86_64)")
string(FIND "${_gmc}" "${_gmc_old}" _gmc_guard)
if (_gmc_guard EQUAL -1)
    message(FATAL_ERROR "Xvid source does not match the pinned 1.3.7 GMC portability patch")
endif ()
string(REPLACE "${_gmc_old}" "${_gmc_old} || defined(ARCH_IS_GENERIC)" _gmc "${_gmc}")
file(WRITE "${_xvid_source}/motion/gmc.c" "${_gmc}")
set(_xvid_sources
    decoder.c
    encoder.c
    xvid.c
    bitstream/bitstream.c
    bitstream/cbp.c
    bitstream/mbcoding.c
    dct/fdct.c
    dct/idct.c
    dct/simple_idct.c
    image/colorspace.c
    image/image.c
    image/interpolate8x8.c
    image/font.c
    image/postprocessing.c
    image/qpel.c
    image/reduced.c
    motion/estimation_bvop.c
    motion/estimation_common.c
    motion/estimation_gmc.c
    motion/estimation_pvop.c
    motion/estimation_rd_based.c
    motion/estimation_rd_based_bvop.c
    motion/gmc.c
    motion/motion_comp.c
    motion/vop_type_decision.c
    motion/sad.c
    prediction/mbprediction.c
    plugins/plugin_single.c
    plugins/plugin_2pass1.c
    plugins/plugin_2pass2.c
    plugins/plugin_lumimasking.c
    plugins/plugin_dump.c
    plugins/plugin_psnr.c
    plugins/plugin_ssim.c
    plugins/plugin_psnrhvsm.c
    quant/quant_h263.c
    quant/quant_matrix.c
    quant/quant_mpeg.c
    utils/emms.c
    utils/mbtransquant.c
    utils/mem_align.c
    utils/mem_transfer.c
    utils/timer.c
)
list(TRANSFORM _xvid_sources PREPEND "${_xvid_source}/")
add_library(XvidPortable STATIC ${_xvid_sources})
target_compile_definitions(XvidPortable PRIVATE ARCH_IS_GENERIC ARCH_IS_64BIT)
target_include_directories(XvidPortable PUBLIC "${_xvid_source}")
set_target_properties(XvidPortable PROPERTIES POSITION_INDEPENDENT_CODE ON)
if (WIN32)
    target_compile_definitions(XvidPortable PRIVATE WIN32)
else ()
    find_package(Threads REQUIRED)
    target_link_libraries(XvidPortable PUBLIC Threads::Threads m)
endif ()
# Package this alongside source/binaries; original bundled Xvid notices remain.
configure_file("${xvid137_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_BINARY_DIR}/Xvid-1.3.7-LICENSE.txt" COPYONLY)
unset(_xvid_sources)
unset(_portab)
unset(_portab_types_old)
unset(_portab_types_guard)
unset(_portab_intptr_type)
unset(_portab_intptr_old)
unset(_portab_intptr_guard)
unset(_gmc)
unset(_gmc_old)
unset(_gmc_guard)
unset(_xvid_source)
