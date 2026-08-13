"""Helpers for declaring Share's generated inner-table schema outputs."""

_SOURCE_PREFIX = "src/share/inner_table/"
_SCHEMA_CPP_PREFIX = _SOURCE_PREFIX + "ob_inner_table_schema."
_BAZEL_OUTPUT_PREFIX = "inner_table/bazel_generated/"

def inner_table_schema_cpp_outputs(unity_groups):
    """Returns Bazel output paths for every generated schema implementation."""
    outputs = []
    for group in unity_groups:
        for src in group.srcs + group.generated_srcs:
            if src.startswith(_SCHEMA_CPP_PREFIX) and src.endswith(".cpp"):
                output = _BAZEL_OUTPUT_PREFIX + src[len(_SOURCE_PREFIX):]
                if output not in outputs:
                    outputs.append(output)
    return sorted(outputs)

def inner_table_schema_source_replacements(unity_groups):
    """Maps source-tree paths to Bazel-generated file labels."""
    replacements = {}
    for output in inner_table_schema_cpp_outputs(unity_groups):
        basename = output[len(_BAZEL_OUTPUT_PREFIX):]
        replacements[_SOURCE_PREFIX + basename] = "//src/share:" + output
    return replacements
