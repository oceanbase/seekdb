"""Generated exact OBLib dependencies for semantic Unity groups."""

_PL_PARSER_C_GROUP_OBLIB_DEPS = {
    "pl_parser_c_0": [
    ],
    "pl_parser_c_1": [
        "//src/oblib:common_sql_mode",
        "//src/oblib:oblib_core_utilities",
        "//src/oblib:oblib_foundation",
    ],
    "pl_parser_c_2": [
        "//src/oblib:common_sql_mode",
        "//src/oblib:oblib_core_utilities",
        "//src/oblib:oblib_foundation",
    ],
    "pl_parser_c_3": [
        "//src/oblib:common_sql_mode",
        "//src/oblib:oblib_core_utilities",
        "//src/oblib:oblib_foundation",
    ],
}

def pl_parser_c_group_oblib_deps(groups, base = {}):
    actual = {group.name: True for group in groups}
    unknown = sorted([
        name
        for name in _PL_PARSER_C_GROUP_OBLIB_DEPS
        if name not in actual
    ])
    missing = sorted([
        name
        for name in actual
        if name not in _PL_PARSER_C_GROUP_OBLIB_DEPS
    ])
    if unknown or missing:
        fail("OBLib Unity dependency map differs from inventory: unknown=%s missing=%s" % (unknown, missing))
    return {
        name: base.get(name, []) + _PL_PARSER_C_GROUP_OBLIB_DEPS[name]
        for name in actual
    }
