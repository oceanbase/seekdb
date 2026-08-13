"""C++ test helper carrying seekdb's standard compile and link contract."""

load("@rules_cc//cc:cc_test.bzl", _rules_cc_test = "cc_test")
load(
    "//bazel:seekdb_build_config.bzl",
    "seekdb_sanity_test_deps",
)

def seekdb_cc_test(name, **kwargs):
    """Links one test whose C++ sources are owned by cc_library targets."""

    data = kwargs.pop("data", [])
    deps = kwargs.pop("deps", [])
    features = kwargs.pop("features", [])
    linkopts = kwargs.pop("linkopts", [])
    package = native.package_name()
    package_parent = "../" * (len(package.split("/")) if package else 0)

    # rules_cc uses the non-standard $EXEC_ORIGIN token for cc_test targets
    # that statically link the C++ runtime.  Keep that runtime contract, but
    # also make the shared implementation an explicit test input and provide
    # a glibc-compatible path to its stable Bazel output location.
    _rules_cc_test(
        name = name,
        data = data + [
            "//src/observer:liboceanbase",
        ],
        deps = deps + [
            "//src/observer:liboceanbase",
        ] + seekdb_sanity_test_deps(),
        features = features + ["static_link_cpp_runtimes"],
        linkstatic = True,
        linkopts = linkopts + [
            "-Wl,-rpath,$ORIGIN/%ssrc/observer" % package_parent,
        ],
        **kwargs
    )
