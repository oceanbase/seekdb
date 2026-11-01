load("@rules_cc//cc:defs.bzl", "cc_library")
load("@seekdb//bazel/sdk_sources:openmp/configure.bzl", "openmp_headers")

package(default_visibility = ["//visibility:public"])

openmp_headers(
    name = "headers",
    omp_template = "runtime/src/include/omp.h.var",
    ompx_template = "runtime/src/include/ompx.h.var",
)

genrule(
    name = "messages",
    srcs = [
        "runtime/src/i18n/en_US.txt",
        "runtime/tools/libomputils.py",
    ],
    outs = [
        "generated/kmp_i18n_id.inc",
        "generated/kmp_i18n_default.inc",
    ],
    cmd = "python3 $(location runtime/tools/message-converter.py) --target-system-override=Darwin " +
          "--enum=$(location generated/kmp_i18n_id.inc) $(location runtime/src/i18n/en_US.txt) && " +
          "python3 $(location runtime/tools/message-converter.py) --target-system-override=Darwin " +
          "--default=$(location generated/kmp_i18n_default.inc) $(location runtime/src/i18n/en_US.txt)",
    tools = ["runtime/tools/message-converter.py"],
)

cc_library(
    name = "library",
    srcs = [
        "runtime/src/kmp_affinity.cpp",
        "runtime/src/kmp_alloc.cpp",
        "runtime/src/kmp_atomic.cpp",
        "runtime/src/kmp_barrier.cpp",
        "runtime/src/kmp_cancel.cpp",
        "runtime/src/kmp_collapse.cpp",
        "runtime/src/kmp_csupport.cpp",
        "runtime/src/kmp_debug.cpp",
        "runtime/src/kmp_dispatch.cpp",
        "runtime/src/kmp_environment.cpp",
        "runtime/src/kmp_error.cpp",
        "runtime/src/kmp_ftn_cdecl.cpp",
        "runtime/src/kmp_ftn_extra.cpp",
        "runtime/src/kmp_global.cpp",
        "runtime/src/kmp_gsupport.cpp",
        "runtime/src/kmp_i18n.cpp",
        "runtime/src/kmp_io.cpp",
        "runtime/src/kmp_itt.cpp",
        "runtime/src/kmp_lock.cpp",
        "runtime/src/kmp_runtime.cpp",
        "runtime/src/kmp_sched.cpp",
        "runtime/src/kmp_settings.cpp",
        "runtime/src/kmp_str.cpp",
        "runtime/src/kmp_taskdeps.cpp",
        "runtime/src/kmp_tasking.cpp",
        "runtime/src/kmp_threadprivate.cpp",
        "runtime/src/kmp_utility.cpp",
        "runtime/src/kmp_version.cpp",
        "runtime/src/kmp_wait_release.cpp",
        "runtime/src/z_Linux_asm.S",
        "runtime/src/z_Linux_util.cpp",
    ],
    hdrs = [
        ":headers",
        ":messages",
    ] + glob([
        "runtime/src/*.h",
        "runtime/src/*.inl",
        "runtime/src/thirdparty/ittnotify/*.h",
    ]),
    copts = ["-std=c++17", "-fno-exceptions", "-fno-rtti"],
    local_defines = [
        "_GNU_SOURCE",
        "_REENTRANT",
        "LIBOMP_HAVE_PTHREAD_SETNAME_NP",
    ],
    includes = [
        "generated",
        "runtime/src",
        "runtime/src/i18n",
        "runtime/src/thirdparty/ittnotify",
    ],
    linkstatic = True,
    target_compatible_with = ["@platforms//os:ios", "@platforms//cpu:aarch64"],
)

filegroup(
    name = "licenses",
    srcs = ["LICENSE.TXT"],
)

filegroup(
    name = "source_metadata",
    srcs = ["source.json"],
)
