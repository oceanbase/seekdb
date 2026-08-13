"""C++ toolchain semantics for seekdb's repository-owned Clang."""

load(
    "@rules_cc//cc/private/toolchain:unix_cc_toolchain_config.bzl",
    "cc_toolchain_config",
)

_CXX_BUILTIN_INCLUDE_DIRECTORIES = @CXX_BUILTIN_INCLUDE_DIRECTORIES@

def seekdb_cc_toolchain_config(name, compiler_path, toolchain_identifier):
    cc_toolchain_config(
        name = name,
        abi_libc_version = "glibc",
        abi_version = "x86_64",
        all_compile_flags = [],
        compile_flags = [
            "-Wall",
            "-Wthread-safety",
            "-Wself-assign",
            "-Wunused-but-set-parameter",
            "-Wno-free-nonheap-object",
            "-fcolor-diagnostics",
            "-fno-omit-frame-pointer",
        ],
        compiler = "clang",
        conly_flags = [],
        coverage_compile_flags = ["--coverage"],
        coverage_link_flags = ["--coverage"],
        cpu = "k8",
        cxx_builtin_include_directories = _CXX_BUILTIN_INCLUDE_DIRECTORIES,
        cxx_flags = ["-std=c++17"],
        dbg_compile_flags = [
            "-O0",
            "-g",
        ],
        extra_flags_per_feature = {
            "use_module_maps": [
                "-Xclang",
                "-fno-cxx-modules",
            ],
        },
        fastbuild_compile_flags = [],
        host_system_name = "local",
        link_flags = [
            "-fuse-ld=lld",
            "-Wl,-no-as-needed",
            "-Wl,-z,relro,-z,now",
        ],
        link_libs = [
            "-Wl,--push-state,-as-needed",
            "-lm",
            "-Wl,--pop-state",
        ],
        opt_compile_flags = ["-O2"],
        opt_link_flags = [],
        supports_start_end_lib = True,
        target_libc = "glibc",
        target_system_name = "x86_64-redhat-linux",
        tool_paths = {
            "ar": "devtools/bin/llvm-ar",
            "c++filt": "devtools/bin/llvm-cxxfilt",
            "cpp": compiler_path,
            "dwp": "devtools/bin/llvm-dwp",
            "gcc": compiler_path,
            "gcov": "devtools/bin/llvm-cov",
            "ld": "devtools/bin/ld.lld",
            "nm": "devtools/bin/llvm-nm",
            "objcopy": "devtools/bin/llvm-objcopy",
            "objdump": "devtools/bin/llvm-objdump",
            "strip": "devtools/bin/llvm-strip",
            "validate_static_library": "validate_static_library.sh",
        },
        toolchain_identifier = toolchain_identifier,
        unfiltered_compile_flags = [
            "-no-canonical-prefixes",
            "-Wno-builtin-macro-redefined",
            "-D__DATE__=\"redacted\"",
            "-D__TIMESTAMP__=\"redacted\"",
            "-D__TIME__=\"redacted\"",
        ],
    )
