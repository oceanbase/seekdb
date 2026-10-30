"""Shared settings for the seekdb Bazel build."""

VERSION = "1.4.0.0"

X86_SIMD_COPTS = [
    "-mtune=core-avx2",
    "-mavx2",
    "-mfma",
    "-mbmi2",
    "-mavx512vl",
    "-mavx512bw",
]

# Shared compiler flags; only legacy modules need workspace-relative includes.
_COMMON_COPTS = [
    "-gdwarf-4",
    "-ffunction-sections",
    "-fdata-sections",
    "-fdebug-info-for-profiling",
    "-fmax-type-align=8",
    "-fno-strict-aliasing",
    "-fno-omit-frame-pointer",
    "-g",
    "-Wno-everything",
]
_CXX_WARNINGS = [
    "-Wno-deprecated",
    "-Wno-reserved-user-defined-literal",
    "-Wno-unused-parameter",
]
_MODULE_INCLUDES = [
    "-I.",
    "-Isrc",
    "-Isrc/data_plane/api",
    "-Isrc/query/api",
    "-Isrc/oblib/easy",
    "-Isrc/oblib/easy/include",
    "-Isrc/oblib",
    "-Isrc/oblib/common",
]
CXX_COPTS = ["-std=gnu++20"] + _MODULE_INCLUDES + _COMMON_COPTS + _CXX_WARNINGS
C_COPTS = ["-std=gnu11"] + _MODULE_INCLUDES + _COMMON_COPTS
MODULE_CXX_COPTS = CXX_COPTS + ["-Werror=private-header"]
MODULE_C_COPTS = ["-std=gnu11"]
OBLIB_CXX_COPTS = ["-std=gnu++20"] + _COMMON_COPTS + _CXX_WARNINGS
OBLIB_C_COPTS = ["-std=gnu11"] + _COMMON_COPTS

OBLIB_LOCAL_DEFINES = [
    'ENABLE_500_MEMORY_LIMIT',
    'ENABLE_INITIAL_EXEC_TLS_MODEL',
    'FATAL_ERROR_HANG',
    'NDEBUG',
    'OB_BUILD_LITE',
    'OB_BUILD_OBSERVER_LITE',
    'OB_BUILD_SYS_VEC_IDX',
    'OCI_LINK_RUNTIME',
    '_GLIBCXX_USE_CXX11_ABI=1',
    '_NO_EXCEPTION',
    '__STDC_CONSTANT_MACROS',
    '__STDC_LIMIT_MACROS',
]

RELEASE_LOCAL_DEFINES = OBLIB_LOCAL_DEFINES + [
    'PACKAGE_NAME=\\"OceanBase\\"',
    'PACKAGE_VERSION=\\"%s\\"' % VERSION,
    'RELEASEID=\\"1\\"',
]

_SANITY_CONFIG = "//bazel:sanity_enabled"
_SANITY_PASS = "//bazel/third_party:sanity_pass"

_SANITY_NO_BUILTIN_COPTS = [
    "-fno-builtin-memset",
    "-fno-builtin-bzero",
    "-fno-builtin-memcpy",
    "-fno-builtin-memmove",
    "-fno-builtin-memcmp",
    "-fno-builtin-strlen",
    "-fno-builtin-strnlen",
    "-fno-builtin-strcpy",
    "-fno-builtin-strncpy",
    "-fno-builtin-strcmp",
    "-fno-builtin-strncmp",
    "-fno-builtin-strcasecmp",
    "-fno-builtin-strncasecmp",
    "-fno-builtin-vsprintf",
    "-fno-builtin-vsnprintf",
    "-fno-builtin-sprintf",
    "-fno-builtin-snprintf",
]

def sanity_settings(instrument):
    """Keeps Sanity's instrumentation, inputs, and platform constraint together."""
    settings = {
        "copts": _SANITY_NO_BUILTIN_COPTS if instrument else [],
        "cxxopts": ["-fpass-plugin=$(location %s)" % _SANITY_PASS] if instrument else [],
        "additional_compiler_inputs": [_SANITY_PASS] if instrument else [],
        "local_defines": ["ENABLE_SANITY", "OB_HAVE_BUNDLED_JEMALLOC=1"],
        "implementation_deps": ["//bazel/third_party:sanity_headers"],
        # Even targets without instrumentation must reject unsupported target platforms
        # when the build requests the Sanity runtime contract.
        "target_compatible_with": ["@platforms//os:linux"],
    }
    return struct(**{
        name: select({_SANITY_CONFIG: values, "//conditions:default": []})
        for name, values in settings.items()
    })


def sanity_linkopts():
    return select({
        "//bazel:sanity_enabled": [
            "-Wl,--wrap=memcpy",
            "-Wl,--wrap=memmove",
            "-Wl,--wrap=memset",
            "-Wl,--wrap=bzero",
            "-Wl,--wrap=memcmp",
            "-Wl,--wrap=strlen",
            "-Wl,--wrap=strnlen",
            "-Wl,--wrap=strcpy",
            "-Wl,--wrap=strncpy",
            "-Wl,--wrap=strcmp",
            "-Wl,--wrap=strncmp",
            "-Wl,--wrap=strcasecmp",
            "-Wl,--wrap=strncasecmp",
            "-Wl,--wrap=vsprintf",
            "-Wl,--wrap=vsnprintf",
            "-Wl,--wrap=sprintf",
            "-Wl,--wrap=snprintf",
        ],
        "//conditions:default": [],
    })

def logging_defines():
    return select({
        "//bazel:rpm_logging_enabled": ["DEFAULT_LOG_FILE_SIZE_MB=16", "DEFAULT_LOG_LEVEL=OB_LOG_LEVEL_DBA_WARN"],
        "//conditions:default": ["DEFAULT_LOG_FILE_SIZE_MB=256", "DEFAULT_LOG_LEVEL=OB_LOG_LEVEL_ERROR"],
    })
