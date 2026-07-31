"""Helpers for declaring Storage's native semantic source graph."""

_STORAGE_SOURCE_PREFIX = "src/storage/"

def _unity_group_key(group):
    return group.language + ":" + group.name

def storage_singleton_unity_groups(sources, name_prefix, language = "c++"):
    """Preserves standalone action granularity inside one semantic library."""

    groups = []
    for index, source in enumerate(sources):
        if source.kind != "source":
            fail("non-source Storage input: %s (%s)" % (source.path, source.kind))
        if source.language != language:
            fail("%s expects %s, got %s" % (source.path, language, source.language))
        if not source.path.startswith(_STORAGE_SOURCE_PREFIX):
            fail("source outside src/storage: %s" % source.path)
        groups.append(struct(
            name = "%s_%d" % (name_prefix, index),
            language = language,
            srcs = [source.path],
            generated_srcs = [],
            external_srcs = [],
        ))
    return groups

def storage_validate_source_inventory(
        unity_groups,
        simd_unity_groups,
        standalone_sources,
        checked_in_sources,
        separately_owned_sources):
    """Freezes the release action baseline and proves complete source ownership."""

    group_keys = {}
    unity_paths = {}
    for group in unity_groups + simd_unity_groups:
        key = _unity_group_key(group)
        if key in group_keys:
            fail("duplicate Storage Unity group: %s" % key)
        group_keys[key] = True
        if group.generated_srcs:
            fail("Storage Unity group %s still has generated_srcs" % key)
        if group.external_srcs:
            fail("Storage Unity group %s still has external_srcs" % key)
        for path in group.srcs:
            if not path.startswith(_STORAGE_SOURCE_PREFIX):
                fail("Storage Unity source is outside src/storage: %s" % path)
            if path in unity_paths:
                fail("duplicate Storage Unity source: %s" % path)
            unity_paths[path] = True

    standalone_paths = {}
    for source in standalone_sources:
        if source.kind != "source":
            fail("non-source Storage standalone input: %s" % source.path)
        if not source.path.startswith(_STORAGE_SOURCE_PREFIX):
            fail("Storage standalone source is outside src/storage: %s" % source.path)
        if source.path in standalone_paths:
            fail("duplicate Storage standalone source: %s" % source.path)
        if source.path in unity_paths:
            fail("Storage source appears in Unity and standalone: %s" % source.path)
        standalone_paths[source.path] = True

    if len(unity_groups) != 44:
        fail("Storage inventory must contain 44 regular Unity groups, got %s" % len(unity_groups))
    if len(simd_unity_groups) != 1:
        fail("Storage inventory must contain 1 SIMD Unity group, got %s" % len(simd_unity_groups))
    if len(unity_paths) != 689:
        fail("Storage inventory must contain 689 Unity sources, got %s" % len(unity_paths))
    if len(standalone_paths) != 0:
        fail("Storage inventory must contain 0 standalone sources, got %s" % len(standalone_paths))

    owned_package_paths = {}
    for path in unity_paths.keys() + standalone_paths.keys():
        owned_package_paths[path[len(_STORAGE_SOURCE_PREFIX):]] = True
    for path in separately_owned_sources:
        if path in owned_package_paths:
            fail("separately owned Storage source is duplicated: %s" % path)
        owned_package_paths[path] = True

    checked_in = {path: True for path in checked_in_sources}
    missing = sorted([path for path in checked_in if path not in owned_package_paths])
    stale = sorted([path for path in owned_package_paths if path not in checked_in])
    if missing or stale:
        fail("Storage source ownership differs from the tree: missing=%s stale=%s" % (missing, stale))
