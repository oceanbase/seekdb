# Copyright (c) 2026 OceanBase.
# SPDX-License-Identifier: Apache-2.0
# Single-precision reference closure for VSAG's eight BLAS/LAPACK operations.
# Keep f2c's integer ABI explicit: this target currently supports wasm32 only.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "The CLAPACK backend currently requires wasm32")
endif()
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/clapack-sources.txt" clapack_sources)
list(TRANSFORM clapack_sources PREPEND "${CLAPACK_SOURCE}/")
set(lapacke "${OPENBLAS_SOURCE}/lapack-netlib/LAPACKE")
set(cblas "${OPENBLAS_SOURCE}/lapack-netlib/CBLAS")
configure_file("${cblas}/include/cblas_mangling_with_flags.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cblas_mangling.h" @ONLY)
add_library(seekdb_vsag_blas STATIC ${clapack_sources}
  "${VSAG_SOURCE}/src/impl/blas/blas_function.cpp"
  "${cblas}/src/cblas_saxpy.c" "${cblas}/src/cblas_sscal.c"
  "${cblas}/src/cblas_sgemv.c" "${cblas}/src/cblas_sgemm.c"
  "${cblas}/src/cblas_globals.c" "${cblas}/src/cblas_xerbla.c"
  "${lapacke}/src/lapacke_nancheck.c")
foreach(routine sgeqrf sorgqr sgetrf ssyev)
  target_sources(seekdb_vsag_blas PRIVATE
    "${lapacke}/src/lapacke_${routine}.c"
    "${lapacke}/src/lapacke_${routine}_work.c")
endforeach()
foreach(routine sge_trans ssy_trans str_trans sge_nancheck ssy_nancheck str_nancheck s_nancheck
    lsame xerbla)
  target_sources(seekdb_vsag_blas PRIVATE "${lapacke}/utils/lapacke_${routine}.c")
endforeach()
foreach(routine pow_ri pow_ii r_sign s_cmp s_copy i_nint)
  target_sources(seekdb_vsag_blas PRIVATE "${CLAPACK_SOURCE}/F2CLIBS/libf2c/${routine}.c")
endforeach()
target_include_directories(seekdb_vsag_blas PRIVATE
  "${CLAPACK_SOURCE}/INCLUDE" "${cblas}/include" "${lapacke}/include"
  "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(seekdb_vsag_blas PRIVATE NO_BLAS_WRAP)
target_compile_options(seekdb_vsag_blas PRIVATE
  -include "${CMAKE_CURRENT_LIST_DIR}/blas_abi.h")
set_target_properties(seekdb_vsag_blas PROPERTIES C_STANDARD 11 CXX_STANDARD 17)
install(TARGETS seekdb_vsag_blas ARCHIVE DESTINATION lib)
