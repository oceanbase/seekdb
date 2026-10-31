"""Target platform facts shared by native build actions."""

_OSES = ["linux", "macos", "ios", "android", "emscripten", "windows"]

# Preserve the SDK's LLVM 18 configuration with newer MSVC STL headers.
WINDOWS_STL_COMPAT_DEFINE = "_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH"

PLATFORM_ATTRS = {
    "_os_" + name: attr.label(default = Label("@platforms//os:" + name))
    for name in _OSES
} | {
    "_ios_simulator": attr.label(default = Label("@apple_support//constraints:simulator")),
}

def target_platform(ctx):
    """Read target constraints, never the machine running Bazel."""
    for name in _OSES:
        if ctx.target_platform_has_constraint(getattr(ctx.attr, "_os_" + name)[platform_common.ConstraintValueInfo]):
            simulator = name == "ios" and ctx.target_platform_has_constraint(
                ctx.attr._ios_simulator[platform_common.ConstraintValueInfo],
            )
            return struct(
                os = name,
                apple = name in ["macos", "ios"],
                ios_sdk = ("iphonesimulator" if simulator else "iphoneos") if name == "ios" else "",
                rust_target = {
                    "ios": "aarch64-apple-ios-sim" if simulator else "aarch64-apple-ios",
                    "android": "aarch64-linux-android",
                    "emscripten": "wasm32-unknown-emscripten",
                }.get(name, ""),
            )
    fail("Unsupported target platform for %s" % ctx.label)

def arch_copts():
    return select({
        "@platforms//cpu:wasm32": [],
        "@platforms//cpu:x86_64": ["-mtune=core2"],
        "@platforms//cpu:aarch64": ["-march=armv8-a+crc+lse", "-mtune=generic"],
        "//conditions:default": ["-mtune=generic"],
    })

def platform_local_defines():
    return select({
        "@platforms//os:windows": [
            WINDOWS_STL_COMPAT_DEFINE,
            "WIN32_LEAN_AND_MEAN", "NOMINMAX", "_ITERATOR_DEBUG_LEVEL=0",
            "_USE_MATH_DEFINES", "YY_NO_UNISTD_H",
            "strncasecmp=_strnicmp", "strcasecmp=_stricmp",
        ],
        "@platforms//os:emscripten": [
            "_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION",
            "_XOPEN_SOURCE=700",
            "VSAG_DISABLE_STATIC_HNSW",
            "HAVE_LIBAIO=0",
        ],
        "@platforms//os:android": ["_LIBCPP_ENABLE_CXX17_REMOVED_UNARY_BINARY_FUNCTION"],
        "@platforms//os:macos": ["_DARWIN_C_SOURCE"],
        "@platforms//os:ios": [
            "_DARWIN_C_SOURCE",
            "_XOPEN_SOURCE=700",
            "OB_HAVE_BUNDLED_JEMALLOC=1",
            "BOOST_NO_CXX98_FUNCTION_BASE",
        ],
        "//conditions:default": [],
    })

def openmp_copts():
    # Apple Clang does not ship an OpenMP runtime. The affected implementation
    # remains serial on macOS.
    return select({
        "@platforms//os:emscripten": [],
        "@platforms//os:macos": [],
        "@platforms//os:ios": ["-Xpreprocessor", "-fopenmp"],
        "//conditions:default": ["-fopenmp"],
    })

def platform_compiler_inputs():
    return select({
        "@platforms//os:android": ["//include:android_compat.h"],
        "//conditions:default": [],
    })

def platform_copts():
    return select({
        "@platforms//os:android": ["-include", "include/android_compat.h"],
        "//conditions:default": [],
    })
