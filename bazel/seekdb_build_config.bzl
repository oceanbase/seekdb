"""Shared settings for the seekdb Bazel build."""

SEEKDB_X86_SIMD_COPTS = [
    "-mtune=core-avx2",
    "-mavx2",
    "-mfma",
    "-mbmi2",
    "-mavx512vl",
    "-mavx512bw",
]

# The legacy source tree still spells includes relative to src/ and a few
# historical include roots. These search paths do not grant ownership: Bazel
# places only declared action inputs in linux-sandbox, and generated owner
# visibility rejects private inputs before compilation.
SEEKDB_MIGRATION_COPTS = [
    "-std=gnu++20",
    "-I.",
    "-Isrc",
    "-Isrc/data_plane/api",
    "-Isrc/query/api",
    "-Isrc/objit/include",
    "-Isrc/oblib/easy",
    "-Isrc/oblib/easy/include",
    "-Isrc/oblib",
    "-Isrc/oblib/common",
    "-gdwarf-4",
    "-ffunction-sections",
    "-fdata-sections",
    "-fdebug-info-for-profiling",
    "-fmax-type-align=8",
    "-fno-strict-aliasing",
    "-fno-omit-frame-pointer",
    "-g",
    # Keep the initial full-build migration focused on missing inputs and
    # dependencies. Warning-policy tightening is independent follow-up work.
    "-Wno-everything",
    "-Wno-deprecated",
    "-Wno-reserved-user-defined-literal",
    "-Wno-unused-parameter",
]

# Semantic module targets enable Clang's layering check. Treat private-header
# violations as hard errors even while the broader migration suppresses legacy
# warnings with -Wno-everything.
SEEKDB_SEMANTIC_MODULE_COPTS = SEEKDB_MIGRATION_COPTS + [
    "-Werror=private-header",
]

SEEKDB_C_MIGRATION_COPTS = [
    "-std=gnu11",
    "-I.",
    "-Isrc",
    "-Isrc/data_plane/api",
    "-Isrc/query/api",
    "-Isrc/objit/include",
    "-Isrc/oblib/easy",
    "-Isrc/oblib/easy/include",
    "-Isrc/oblib",
    "-Isrc/oblib/common",
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

# Native C Unity actions select the C language contract from the rule's
# `language` attribute.  Keep this deliberately small: include ownership and
# warning policy come from declared Bazel dependencies, not migration search
# paths or blanket warning suppression.
SEEKDB_C_MODULE_COPTS = [
    "-std=gnu11",
]

SEEKDB_OBLIB_LOCAL_DEFINES = [
    'DEFAULT_LOG_FILE_SIZE_MB=256',
    'DEFAULT_LOG_LEVEL=OB_LOG_LEVEL_ERROR',
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

# Native OBLib modules obtain project include paths from their declared Bazel
# dependencies. These options deliberately contain no workspace-wide -I escape
# hatch.
SEEKDB_OBLIB_COPTS = [
    "-std=gnu++20",
    "-gdwarf-4",
    "-ffunction-sections",
    "-fdata-sections",
    "-fdebug-info-for-profiling",
    "-fmax-type-align=8",
    "-fno-strict-aliasing",
    "-fno-omit-frame-pointer",
    "-g",
    "-Wno-everything",
    "-Wno-deprecated",
    "-Wno-reserved-user-defined-literal",
    "-Wno-unused-parameter",
]

SEEKDB_OBLIB_C_COPTS = [
    "-std=gnu11",
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

SEEKDB_RELEASE_LOCAL_DEFINES = [
    'DEFAULT_LOG_FILE_SIZE_MB=256',
    'DEFAULT_LOG_LEVEL=OB_LOG_LEVEL_ERROR',
    'ENABLE_500_MEMORY_LIMIT',
    'ENABLE_INITIAL_EXEC_TLS_MODEL',
    'FATAL_ERROR_HANG',
    'NDEBUG',
    'OB_BUILD_LITE',
    'OB_BUILD_OBSERVER_LITE',
    'OB_BUILD_SYS_VEC_IDX',
    'OCI_LINK_RUNTIME',
    'PACKAGE_NAME=\\"OceanBase\\"',
    'PACKAGE_STRING=\\"OceanBase\\ 1.3.0.0\\"',
    'PACKAGE_VERSION=\\"1.3.0.0\\"',
    'RELEASEID=\\"1\\"',
    '_GLIBCXX_USE_CXX11_ABI=1',
    '_NO_EXCEPTION',
    '__STDC_CONSTANT_MACROS',
    '__STDC_LIMIT_MACROS',
]

_SEEKDB_SANITY_CONFIG = "//bazel:sanity_enabled"
_SEEKDB_SANITY_PASS = "@seekdb_3rd_headers//:sanity_pass"

def seekdb_arch_copts():
    return select({
        "@platforms//cpu:x86_64": ["-mtune=core2"],
        "//conditions:default": ["-mtune=generic"],
    })

def seekdb_platform_local_defines():
    return select({
        "@platforms//os:macos": ["_DARWIN_C_SOURCE"],
        "//conditions:default": [],
    })

def seekdb_openmp_copts():
    # Apple Clang does not ship an OpenMP runtime. The affected implementation
    # remains serial on macOS, matching the former CMake build.
    return select({
        "@platforms//os:macos": [],
        "//conditions:default": ["-fopenmp"],
    })

_SEEKDB_SANITY_NO_BUILTIN_COPTS = [
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

def seekdb_sanity_copts(instrument):
    return select({
        _SEEKDB_SANITY_CONFIG: _SEEKDB_SANITY_NO_BUILTIN_COPTS if instrument else [],
        "//conditions:default": [],
    })

def seekdb_sanity_cxxopts(instrument):
    return select({
        _SEEKDB_SANITY_CONFIG: [
            "-fpass-plugin=$(location %s)" % _SEEKDB_SANITY_PASS,
        ] if instrument else [],
        "//conditions:default": [],
    })

def seekdb_sanity_local_defines():
    return select({
        _SEEKDB_SANITY_CONFIG: ["ENABLE_SANITY"],
        "//conditions:default": [],
    })

def seekdb_sanity_compiler_inputs(instrument):
    return select({
        _SEEKDB_SANITY_CONFIG: [_SEEKDB_SANITY_PASS] if instrument else [],
        "//conditions:default": [],
    })

def seekdb_sanity_implementation_deps():
    return select({
        _SEEKDB_SANITY_CONFIG: ["@seekdb_3rd_headers//:sanity_headers"],
        "//conditions:default": [],
    })

def seekdb_sanity_test_deps():
    return select({
        _SEEKDB_SANITY_CONFIG: [
            "@seekdb_3rd_headers//:sanity_headers",
        ],
        "//conditions:default": [],
    })
