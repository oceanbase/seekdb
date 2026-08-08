"""Validation and grouping helpers for PL's native source module."""

_PL_SOURCE_PREFIX = "src/pl/"

def pl_singleton_unity_groups(sources, name_prefix, language):
    groups = []
    for source in sources:
        if source.kind not in ("source", "generated"):
            fail("unsupported PL input: %s (%s)" % (source.path, source.kind))
        if source.language != language:
            continue
        if not source.path.startswith(_PL_SOURCE_PREFIX):
            fail("PL source outside src/pl: %s" % source.path)
        groups.append(struct(
            name = "%s_%d" % (name_prefix, len(groups)),
            language = language,
            srcs = [source.path],
            generated_srcs = [],
            external_srcs = [],
        ))
    if not groups:
        fail("no %s standalone PL sources" % language)
    return groups

def pl_validate_source_inventory(unity_groups, standalone_sources, checked_in_sources):
    group_names = {}
    unity_paths = {}
    for group in unity_groups:
        if group.name in group_names:
            fail("duplicate PL Unity group: %s" % group.name)
        group_names[group.name] = True
        if group.language != "c++":
            fail("PL release Unity group is not C++: %s" % group.name)
        if group.generated_srcs or group.external_srcs:
            fail("PL Unity group still has non-checked-in sources: %s" % group.name)
        for path in group.srcs:
            if not path.startswith(_PL_SOURCE_PREFIX):
                fail("PL Unity source outside src/pl: %s" % path)
            if path in unity_paths:
                fail("duplicate PL Unity source: %s" % path)
            unity_paths[path] = True

    standalone_paths = {}
    generated_paths = {}
    for source in standalone_sources:
        if source.kind not in ("source", "generated"):
            fail("unsupported PL standalone input: %s (%s)" % (source.path, source.kind))
        if not source.path.startswith(_PL_SOURCE_PREFIX):
            fail("PL standalone source outside src/pl: %s" % source.path)
        if source.path in unity_paths or source.path in standalone_paths:
            fail("duplicate PL standalone source: %s" % source.path)
        standalone_paths[source.path] = True
        if source.kind == "generated":
            generated_paths[source.path] = True

    if len(group_names) != 4:
        fail("PL inventory must contain 4 release Unity groups, got %s" % len(group_names))
    if len(unity_paths) != 31:
        fail("PL inventory must contain 31 Unity sources, got %s" % len(unity_paths))
    if len(standalone_paths) != 5:
        fail("PL inventory must contain 5 standalone sources, got %s" % len(standalone_paths))
    if len(generated_paths) != 2:
        fail("PL inventory must contain 2 generated parser sources, got %s" % len(generated_paths))

    owned = {}
    for path in unity_paths.keys():
        owned[path[len(_PL_SOURCE_PREFIX):]] = True
    for path in standalone_paths.keys():
        if path not in generated_paths:
            owned[path[len(_PL_SOURCE_PREFIX):]] = True
    checked_in = {path: True for path in checked_in_sources}
    missing = sorted([path for path in checked_in if path not in owned])
    stale = sorted([path for path in owned if path not in checked_in])
    if missing or stale:
        fail("PL source ownership differs from the tree: missing=%s stale=%s" % (missing, stale))
