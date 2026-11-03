"""Expose the native compiler environment to Cargo build scripts."""

load("@apple_support//lib:apple_support.bzl", "apple_support")
load("@rules_cc//cc:action_names.bzl", "CPP_LINK_EXECUTABLE_ACTION_NAME")
load("@rules_cc//cc:find_cc_toolchain.bzl", "CC_TOOLCHAIN_ATTRS", "find_cpp_toolchain", "use_cc_toolchain")
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")

_UNSUPPORTED_BUILD_SCRIPT_FEATURES = [
    "thin_lto",
    "module_maps",
    "use_header_modules",
    "fdo_instrument",
    "fdo_optimize",
    "rules_rust_unsupported_feature",
]

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
        unsupported_features = ctx.disabled_features + _UNSUPPORTED_BUILD_SCRIPT_FEATURES,
    )
    variables = cc_common.create_link_variables(
        cc_toolchain = cc_toolchain,
        feature_configuration = features,
        is_linking_dynamic_library = False,
        user_link_flags = ctx.fragments.cpp.linkopts,
    )
    environment = cc_common.get_environment_variables(
        feature_configuration = features,
        action_name = CPP_LINK_EXECUTABLE_ACTION_NAME,
        variables = variables,
    )
    link_args = cc_common.get_memory_inefficient_command_line(
        feature_configuration = features,
        action_name = CPP_LINK_EXECUTABLE_ACTION_NAME,
        variables = variables,
    )
    link_args = link_args + ["-LIBPATH:" + path for path in environment.get("LIB", "").split(";") if path]
    link_args = ['"{}"'.format(arg) if " " in arg else arg for arg in link_args]
    environment_file = ctx.actions.declare_file(ctx.label.name + ".env")
    ctx.actions.write(environment_file, "LDFLAGS=" + " ".join(link_args) + "\n")
    default_environment = ctx.configuration.default_shell_env
    paths = [
        environment.get("PATH", environment.get("Path", "")),
        default_environment.get("PATH", default_environment.get("Path", "")),
    ]
    # Keep the native compiler first, then allow configure/make to find tools
    # supplied by the invoking process. build.py fixes PATH in --action_env.
    return [
        DefaultInfo(files = depset([environment_file])),
        platform_common.TemplateVariableInfo({
            "CARGO_BUILD_SCRIPT_PATH": ";".join([path for path in paths if path]),
        }),
    ]

cargo_build_script_environment = rule(
    implementation = _cargo_build_script_environment_impl,
    attrs = CC_TOOLCHAIN_ATTRS | {
        "_windows": attr.label(default = Label("@platforms//os:windows")),
    },
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)

def _ios_bindgen_environment_impl(ctx):
    environment_file = ctx.actions.declare_file(ctx.label.name + ".env")
    apple_support.run_shell(
        actions = ctx.actions,
        xcode_config = ctx.attr._xcode_config[apple_common.XcodeVersionConfig],
        apple_platform_info = apple_support.platform_info_from_rule_ctx(ctx),
        outputs = [environment_file],
        arguments = [environment_file.path],
        command = """
printf 'LIBCLANG_PATH=%s/Toolchains/XcodeDefault.xctoolchain/usr/lib\\nBINDGEN_EXTRA_CLANG_ARGS=--sysroot "%s"\\n' "$DEVELOPER_DIR" "$SDKROOT" > "$1"
""",
    )
    return [DefaultInfo(files = depset([environment_file]))]

ios_bindgen_environment = rule(
    implementation = _ios_bindgen_environment_impl,
    attrs = apple_support.action_required_attrs() | apple_support.platform_constraint_attrs(),
    fragments = ["apple"],
)
