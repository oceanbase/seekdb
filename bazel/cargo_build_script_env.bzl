"""Expose the native compiler and shell-tool PATH to Cargo build scripts."""

load("@rules_cc//cc:action_names.bzl", "CPP_LINK_EXECUTABLE_ACTION_NAME")
load("@rules_cc//cc:find_cc_toolchain.bzl", "CC_TOOLCHAIN_ATTRS", "find_cpp_toolchain", "use_cc_toolchain")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")

def _cargo_build_script_environment_impl(ctx):
    # crate_universe attaches toolchains to every target platform. Only the
    # Windows build-script environment references this PATH variable.
    if not ctx.target_platform_has_constraint(ctx.attr._windows[platform_common.ConstraintValueInfo]):
        return [platform_common.TemplateVariableInfo({})]

    cc_toolchain = find_cpp_toolchain(ctx)
    features = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = ctx.features,
        unsupported_features = ctx.disabled_features,
    )
    environment = cc_common.get_environment_variables(
        feature_configuration = features,
        action_name = CPP_LINK_EXECUTABLE_ACTION_NAME,
        variables = cc_common.create_link_variables(
            cc_toolchain = cc_toolchain,
            feature_configuration = features,
            is_using_linker = True,
        ),
    )
    default_environment = ctx.configuration.default_shell_env
    paths = [
        environment.get("PATH", environment.get("Path", "")),
        default_environment.get("PATH", default_environment.get("Path", "")),
    ]
    # Keep the native compiler first, then allow configure/make to find tools
    # supplied by the invoking process. build.py fixes PATH in --action_env.
    return [platform_common.TemplateVariableInfo({
        "CARGO_BUILD_SCRIPT_PATH": ";".join([path for path in paths if path]),
    })]

cargo_build_script_environment = rule(
    implementation = _cargo_build_script_environment_impl,
    attrs = CC_TOOLCHAIN_ATTRS | {
        "_windows": attr.label(default = Label("@platforms//os:windows")),
    },
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)
