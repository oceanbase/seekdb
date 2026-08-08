"""Toolchain-backed final executable/shared link for seekdb.

The rule deliberately exposes only the link-order concepts required by
seekdb's release executable. CcInfo traversal, archive selection, toolchain flags,
action inputs, and the compiler-driver invocation stay inside this module.

Object-only cc_import inputs are kept when a dependency has no archive.  The
emitted user-input order is:

  direct objects
  --start-group <group static inputs> --end-group
  --whole-archive <whole-group transitive static inputs> --no-whole-archive
  --whole-archive <whole static inputs> --no-whole-archive
  <whole-target transitive inputs, then tail dependency static inputs>
  <explicit ordered static/shared link files>
  <toolchain static C++ runtime>
  <system libraries>
  <linkopts>

`extra_inputs` declares runtime symlink chains and similar files without
placing them on the link command line.

Transitive CcInfo closures are de-duplicated by artifact path within each
static-link section. This prevents a common dependency reached through
multiple top-level modules from being linked more than once. To repeat one
archive positionally, callers use distinct alias/filegroup labels in
`ordered_link_files`; that explicitly ordered section preserves repetition.

`shared = True` selects the toolchain's dynamic-library link action. The C++
toolchain may add its own driver flags and runtime libraries around the
ordered sequence.
"""

load(
    "@rules_cc//cc:action_names.bzl",
    "CPP_LINK_DYNAMIC_LIBRARY_ACTION_NAME",
    "CPP_LINK_EXECUTABLE_ACTION_NAME",
)
load(
    "@rules_cc//cc:find_cc_toolchain.bzl",
    "CC_TOOLCHAIN_ATTRS",
    "find_cpp_toolchain",
    "use_cc_toolchain",
)
load("@rules_cc//cc/common:cc_common.bzl", "cc_common")
load("@rules_cc//cc/common:cc_info.bzl", "CcInfo")

_STATIC_ARCHIVE_SUFFIXES = (".a", ".lo", ".lib")

def _target_is_macos(ctx):
    return ctx.target_platform_has_constraint(
        ctx.attr._macos_constraint[platform_common.ConstraintValueInfo],
    )

def _append_unique(result, seen, files):
    for file in files:
        if file.path not in seen:
            seen[file.path] = True
            result.append(file)

def _library_objects(library):
    if library.pic_objects:
        return library.pic_objects
    return library.objects

def _direct_objects(target):
    objects = []
    seen = {}
    for linker_input in target[CcInfo].linking_context.linker_inputs.to_list():
        if linker_input.owner != target.label:
            continue
        for library in linker_input.libraries:
            _append_unique(objects, seen, _library_objects(library))
    if not objects:
        fail(
            (
                "%s contributes no direct C++ objects; pass a source-owning " +
                "target instead of a source-less aggregate"
            ) % target.label,
        )
    return objects

def _selected_static_archive(library):
    if library.pic_static_library != None:
        return library.pic_static_library
    return library.static_library

def _static_link_inputs(target):
    inputs = []
    for linker_input in target[CcInfo].linking_context.linker_inputs.to_list():
        for library in linker_input.libraries:
            archive = _selected_static_archive(library)
            if archive != None:
                inputs.append(archive)
            else:
                # cc_import(objects = ...) intentionally has no archive.  It
                # still belongs in the same ordered static-link position.
                inputs.extend(_library_objects(library))
    if not inputs:
        fail("%s contributes no static archives or object files" % target.label)
    return inputs

def _direct_static_link_inputs(target):
    inputs = []
    for linker_input in target[CcInfo].linking_context.linker_inputs.to_list():
        if linker_input.owner != target.label:
            continue
        for library in linker_input.libraries:
            archive = _selected_static_archive(library)
            if archive != None:
                inputs.append(archive)
            else:
                inputs.extend(_library_objects(library))
    if not inputs:
        fail(
            (
                "%s contributes no direct static archive or object files; " +
                "whole_archive_deps requires a source-owning target"
            ) % target.label,
        )
    return inputs

def _is_supported_ordered_link_file(file):
    if file.path.endswith(_STATIC_ARCHIVE_SUFFIXES):
        return True
    return (
        file.basename.endswith(".so") or
        ".so." in file.basename or
        file.basename.endswith(".dylib")
    )

def _ordered_link_files_from_targets(targets):
    link_files = []
    for target in targets:
        files = target[DefaultInfo].files.to_list()
        if not files:
            fail("%s contributes no ordered link files" % target.label)
        for file in files:
            if not _is_supported_ordered_link_file(file):
                fail(
                    "%s is not a supported static/shared link file from %s" %
                    (file.path, target.label),
                )
        # Repetition is significant here: the release link deliberately names
        # some archives more than once.
        link_files.extend(files)
    return link_files

def _files_from_targets(targets):
    files = []
    for target in targets:
        target_files = target[DefaultInfo].files.to_list()
        if not target_files:
            fail("%s contributes no extra input files" % target.label)
        files.extend(target_files)
    return files

def _static_inputs_from_deps(targets):
    inputs = []
    seen = {}
    for target in targets:
        # Different top-level module targets commonly carry overlapping
        # transitive CcInfo closures. Keep the first position of each artifact
        # in this section; explicit positional repetition belongs in
        # ordered_link_files instead.
        _append_unique(inputs, seen, _static_link_inputs(target))
    return inputs

def _whole_archive_inputs(targets):
    direct_inputs = []
    direct_seen = {}
    for target in targets:
        _append_unique(
            direct_inputs,
            direct_seen,
            _direct_static_link_inputs(target),
        )

    transitive_inputs = []
    transitive_seen = {}
    for target in targets:
        for file in _static_link_inputs(target):
            if file.path not in direct_seen and file.path not in transitive_seen:
                transitive_seen[file.path] = True
                transitive_inputs.append(file)
    return direct_inputs, transitive_inputs

def _system_library_flags(system_libs):
    flags = []
    for library in system_libs:
        if not library or library.startswith("-") or "/" in library:
            fail(
                "system_libs entries are bare linker names such as 'pthread'; " +
                "got %r" % library,
            )
        flags.append("-l" + library)
    return flags

def _file_paths(files):
    return [file.path for file in files]

def _seekdb_final_link_impl(ctx):
    cc_toolchain = find_cpp_toolchain(ctx)
    is_macos = _target_is_macos(ctx)
    requested_features = ctx.features
    if not is_macos and "static_link_cpp_runtimes" not in requested_features:
        requested_features = requested_features + ["static_link_cpp_runtimes"]
    feature_configuration = cc_common.configure_features(
        ctx = ctx,
        cc_toolchain = cc_toolchain,
        requested_features = requested_features,
        unsupported_features = ctx.disabled_features,
    )
    static_cpp_runtime_inputs = []
    if not is_macos:
        static_cpp_runtime_inputs = cc_toolchain.static_runtime_lib(
            feature_configuration = feature_configuration,
        ).to_list()
    if not is_macos and not static_cpp_runtime_inputs:
        fail(
            "%s requires a toolchain-provided static C++ runtime" % ctx.label,
        )

    direct_objects = []
    direct_object_seen = {}
    for dep in ctx.attr.direct_object_deps:
        _append_unique(
            direct_objects,
            direct_object_seen,
            _direct_objects(dep),
        )

    group_inputs = _static_inputs_from_deps(ctx.attr.group_deps)
    whole_group_inputs = _static_inputs_from_deps(ctx.attr.whole_group_deps)
    whole_inputs, whole_transitive_inputs = _whole_archive_inputs(
        ctx.attr.whole_archive_deps,
    )
    tail_inputs = []
    tail_seen = {}
    _append_unique(tail_inputs, tail_seen, whole_transitive_inputs)
    _append_unique(
        tail_inputs,
        tail_seen,
        _static_inputs_from_deps(ctx.attr.tail_deps),
    )
    ordered_link_files = _ordered_link_files_from_targets(
        ctx.attr.ordered_link_files,
    )
    extra_inputs = _files_from_targets(ctx.attr.extra_inputs)

    link_inputs = (
        direct_objects +
        group_inputs +
        whole_group_inputs +
        whole_inputs +
        tail_inputs +
        ordered_link_files +
        static_cpp_runtime_inputs
    )
    if not link_inputs:
        fail("%s has no object or archive inputs" % ctx.label)

    ordered_link_args = _file_paths(direct_objects)
    if group_inputs and is_macos:
        # ld64 repeatedly searches archives, so it does not need GNU ld's
        # --start-group/--end-group cycle breaker.
        ordered_link_args.extend(_file_paths(group_inputs))
    elif group_inputs:
        ordered_link_args.append("-Wl,--start-group")
        ordered_link_args.extend(_file_paths(group_inputs))
        ordered_link_args.append("-Wl,--end-group")
    if whole_group_inputs and is_macos:
        ordered_link_args.extend([
            "-Wl,-force_load,%s" % path
            for path in _file_paths(whole_group_inputs)
        ])
    elif whole_group_inputs:
        ordered_link_args.append("-Wl,--whole-archive")
        ordered_link_args.extend(_file_paths(whole_group_inputs))
        ordered_link_args.append("-Wl,--no-whole-archive")
    if whole_inputs and is_macos:
        ordered_link_args.extend([
            "-Wl,-force_load,%s" % path
            for path in _file_paths(whole_inputs)
        ])
    elif whole_inputs:
        ordered_link_args.append("-Wl,--whole-archive")
        ordered_link_args.extend(_file_paths(whole_inputs))
        ordered_link_args.append("-Wl,--no-whole-archive")
    ordered_link_args.extend(_file_paths(tail_inputs))
    ordered_link_args.extend(_file_paths(ordered_link_files))
    ordered_link_args.extend(_file_paths(static_cpp_runtime_inputs))
    ordered_link_args.extend(_system_library_flags(ctx.attr.system_libs))
    ordered_link_args.extend(ctx.attr.linkopts)

    output = ctx.outputs.out
    action_name = (
        CPP_LINK_DYNAMIC_LIBRARY_ACTION_NAME
        if ctx.attr.shared
        else CPP_LINK_EXECUTABLE_ACTION_NAME
    )
    link_variables = cc_common.create_link_variables(
        cc_toolchain = cc_toolchain,
        feature_configuration = feature_configuration,
        output_file = output.path,
        user_link_flags = ordered_link_args,
        is_using_linker = True,
        is_linking_dynamic_library = ctx.attr.shared,
    )
    tool = cc_common.get_tool_for_action(
        feature_configuration = feature_configuration,
        action_name = action_name,
    )
    command_line = cc_common.get_memory_inefficient_command_line(
        feature_configuration = feature_configuration,
        action_name = action_name,
        variables = link_variables,
    )
    environment = cc_common.get_environment_variables(
        feature_configuration = feature_configuration,
        action_name = action_name,
        variables = link_variables,
    )

    args = ctx.actions.args()
    args.add_all(command_line)
    ctx.actions.run(
        executable = tool,
        arguments = [args],
        env = environment,
        inputs = depset(
            direct = link_inputs + extra_inputs,
            transitive = [cc_toolchain.all_files],
        ),
        outputs = [output],
        mnemonic = "SeekdbFinalLink",
        progress_message = (
            "Linking seekdb shared library %{output}"
            if ctx.attr.shared
            else "Linking seekdb executable %{output}"
        ),
        use_default_shell_env = True,
    )

    return [
        DefaultInfo(
            executable = output,
            files = depset([output]),
        ),
    ]

seekdb_final_link = rule(
    implementation = _seekdb_final_link_impl,
    attrs = {
        "direct_object_deps": attr.label_list(providers = [CcInfo]),
        "group_deps": attr.label_list(providers = [CcInfo]),
        "whole_group_deps": attr.label_list(providers = [CcInfo]),
        "whole_archive_deps": attr.label_list(providers = [CcInfo]),
        "tail_deps": attr.label_list(providers = [CcInfo]),
        "ordered_link_files": attr.label_list(allow_files = True),
        "extra_inputs": attr.label_list(allow_files = True),
        "system_libs": attr.string_list(),
        "linkopts": attr.string_list(),
        "out": attr.output(mandatory = True),
        "shared": attr.bool(default = False),
        "_macos_constraint": attr.label(
            default = Label("@platforms//os:macos"),
        ),
    } | CC_TOOLCHAIN_ATTRS,
    executable = True,
    fragments = ["cpp"],
    toolchains = use_cc_toolchain(),
)
