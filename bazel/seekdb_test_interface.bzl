"""Compile-only interfaces for module-owned unit tests."""

load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

def _seekdb_test_interface_impl(ctx):
    return [
        CcInfo(
            compilation_context = cc_common.merge_compilation_contexts(
                compilation_contexts = [
                    dep[CcInfo].compilation_context
                    for dep in ctx.attr.deps
                ],
            ),
        ),
    ]

_seekdb_test_interface = rule(
    implementation = _seekdb_test_interface_impl,
    attrs = {
        "deps": attr.label_list(providers = [CcInfo]),
    },
)

def seekdb_test_interface(name, deps, **kwargs):
    """Forwards C++ headers while deliberately discarding all link inputs."""
    _seekdb_test_interface(
        name = name,
        deps = deps,
        testonly = True,
        **kwargs
    )
