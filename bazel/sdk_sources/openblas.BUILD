load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "library",
    srcs = [
        "lapack-netlib/CBLAS/src/cblas_globals.c",
        "lapack-netlib/CBLAS/src/cblas_saxpy.c",
        "lapack-netlib/CBLAS/src/cblas_sgemm.c",
        "lapack-netlib/CBLAS/src/cblas_sgemv.c",
        "lapack-netlib/CBLAS/src/cblas_sscal.c",
        "lapack-netlib/CBLAS/src/cblas_xerbla.c",
        "lapack-netlib/LAPACKE/src/lapacke_nancheck.c",
        "lapack-netlib/LAPACKE/src/lapacke_sgeqrf.c",
        "lapack-netlib/LAPACKE/src/lapacke_sgeqrf_work.c",
        "lapack-netlib/LAPACKE/src/lapacke_sgetrf.c",
        "lapack-netlib/LAPACKE/src/lapacke_sgetrf_work.c",
        "lapack-netlib/LAPACKE/src/lapacke_sorgqr.c",
        "lapack-netlib/LAPACKE/src/lapacke_sorgqr_work.c",
        "lapack-netlib/LAPACKE/src/lapacke_ssyev.c",
        "lapack-netlib/LAPACKE/src/lapacke_ssyev_work.c",
        "lapack-netlib/LAPACKE/utils/lapacke_lsame.c",
        "lapack-netlib/LAPACKE/utils/lapacke_s_nancheck.c",
        "lapack-netlib/LAPACKE/utils/lapacke_sge_nancheck.c",
        "lapack-netlib/LAPACKE/utils/lapacke_sge_trans.c",
        "lapack-netlib/LAPACKE/utils/lapacke_ssy_nancheck.c",
        "lapack-netlib/LAPACKE/utils/lapacke_ssy_trans.c",
        "lapack-netlib/LAPACKE/utils/lapacke_str_nancheck.c",
        "lapack-netlib/LAPACKE/utils/lapacke_str_trans.c",
        "lapack-netlib/LAPACKE/utils/lapacke_xerbla.c",
    ],
    hdrs = glob(
        [
            "lapack-netlib/CBLAS/include/*.h",
            "lapack-netlib/LAPACKE/include/*.h",
        ],
        allow_empty = True,
    ),
    additional_compiler_inputs = ["@seekdb//bazel/sdk_sources:openblas/blas_abi.h"],
    copts = [
        "-pthread",
        "-include",
        "$(location @seekdb//bazel/sdk_sources:openblas/blas_abi.h)",
    ],
    includes = [
        "lapack-netlib/CBLAS/include",
        "lapack-netlib/LAPACKE/include",
    ],
    linkstatic = True,
    local_defines = ["NO_BLAS_WRAP"],
    deps = ["@sdk_clapack//:library"],
)
