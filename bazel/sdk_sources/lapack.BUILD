load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

genrule(
    name = "mangling",
    srcs = ["LAPACKE/include/lapacke_mangling_with_flags.h.in"],
    outs = ["generated/lapacke_mangling.h"],
    cmd = "cp $< $@",
)

cc_library(
    name = "library",
    srcs = [
        "LAPACKE/src/lapacke_nancheck.c",
        "LAPACKE/src/lapacke_sgeqrf.c",
        "LAPACKE/src/lapacke_sgeqrf_work.c",
        "LAPACKE/src/lapacke_sgesdd.c",
        "LAPACKE/src/lapacke_sgesdd_work.c",
        "LAPACKE/src/lapacke_sgetrf.c",
        "LAPACKE/src/lapacke_sgetrf_work.c",
        "LAPACKE/src/lapacke_sorgqr.c",
        "LAPACKE/src/lapacke_sorgqr_work.c",
        "LAPACKE/src/lapacke_ssyev.c",
        "LAPACKE/src/lapacke_ssyev_work.c",
    ] + glob(["LAPACKE/utils/*.c"]),
    hdrs = [":mangling"] + glob(["LAPACKE/include/*.h"]),
    includes = ["LAPACKE/include", "generated"],
    local_defines = ["LAPACK_F2C"],
    linkopts = ["-framework", "Accelerate"],
    linkstatic = True,
    target_compatible_with = ["@platforms//os:ios", "@platforms//cpu:aarch64"],
)

filegroup(
    name = "licenses",
    srcs = ["LICENSE"],
)

filegroup(
    name = "source_metadata",
    srcs = ["source.json"],
)
