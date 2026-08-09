"""Helpers for declaring Share's native semantic source graph."""

_SHARE_SOURCE_PREFIX = "src/share/"

def _unity_group_key(group):
    return group.language + ":" + group.name

def share_select_unity_groups(groups, names, language = "c++"):
    """Selects an exact, ordered set of same-language Unity groups."""

    requested = {name: False for name in names}
    selected = []
    for group in groups:
        if group.language == language and group.name in requested:
            if requested[group.name]:
                fail("duplicate %s Unity group %s" % (language, group.name))
            requested[group.name] = True
            selected.append(group)
    missing = sorted([name for name, found in requested.items() if not found])
    if missing:
        fail("missing %s Share Unity groups: %s" % (language, missing))
    return selected

def share_package_sources(sources, language = "c++"):
    """Converts checked-in standalone source records to package paths."""

    result = []
    for source in sources:
        if source.kind != "source":
            fail("non-source Share input: %s (%s)" % (source.path, source.kind))
        if source.language == language:
            if not source.path.startswith(_SHARE_SOURCE_PREFIX):
                fail("source outside src/share: %s" % source.path)
            result.append(source.path[len(_SHARE_SOURCE_PREFIX):])
    if not result:
        fail("no %s standalone Share sources" % language)
    return result

def share_select_unity_sources(groups, name, source_paths, language = "c++"):
    """Builds one semantic Unity group from exact checked-in Unity members."""

    requested = {path: False for path in source_paths}
    selected = []
    for group in groups:
        if group.language != language:
            continue
        for path in group.srcs:
            if path in requested:
                if requested[path]:
                    fail("duplicate Share Unity source: %s" % path)
                requested[path] = True
                selected.append(path)
    missing = sorted([path for path, found in requested.items() if not found])
    if missing:
        fail("missing %s Share Unity sources: %s" % (language, missing))
    return struct(
        name = name,
        language = language,
        srcs = selected,
        generated_srcs = [],
        external_srcs = [],
    )

def share_unity_group_sources(groups):
    """Flattens semantic Unity groups into one duplicate-checked source set."""

    result = []
    seen = {}
    for group in groups:
        for source in group.srcs:
            if source in seen:
                fail(
                    "Share semantic source %s is owned by both %s and %s" %
                    (source, seen[source], group.name),
                )
            seen[source] = group.name
            result.append(source)
    return result

def share_singleton_unity_groups(sources, name_prefix, language = "c++"):
    """Preserves standalone action granularity inside one semantic library."""

    groups = []
    for index, source in enumerate(sources):
        if source.kind != "source":
            fail("non-source Share input: %s (%s)" % (source.path, source.kind))
        if source.language != language:
            fail("%s expects %s, got %s" % (source.path, language, source.language))
        if not source.path.startswith(_SHARE_SOURCE_PREFIX):
            fail("source outside src/share: %s" % source.path)
        groups.append(struct(
            name = "%s_%d" % (name_prefix, index),
            language = language,
            srcs = [source.path],
            generated_srcs = [],
            external_srcs = [],
        ))
    if not groups:
        fail("no %s standalone Share sources" % language)
    return groups

def share_remaining_unity_groups(groups, excluded_sources, language = "c++"):
    """Preserves release Unity action boundaries after native extractions."""

    excluded = {source: False for source in excluded_sources}
    result = []
    for group in groups:
        if group.language != language:
            continue
        members = []
        for source in group.srcs:
            if source in excluded:
                excluded[source] = True
            else:
                members.append(source)
        if members:
            result.append(struct(
                name = group.name,
                language = group.language,
                srcs = members,
                generated_srcs = group.generated_srcs,
                external_srcs = group.external_srcs,
            ))
    missing = sorted([source for source, found in excluded.items() if not found])
    if missing:
        fail("unknown excluded Share sources: %s" % missing)
    return result

def share_validate_source_inventory(unity_groups, standalone_sources):
    """Freezes the release source/action baseline before semantic regrouping."""

    group_keys = {}
    unity_paths = {}
    for group in unity_groups:
        key = _unity_group_key(group)
        if key in group_keys:
            fail("duplicate Share Unity group: %s" % key)
        group_keys[key] = True
        if group.generated_srcs:
            fail("Share Unity group %s still has generated_srcs" % key)
        if group.external_srcs:
            fail("Share Unity group %s still has external_srcs" % key)
        for path in group.srcs:
            if not path.startswith(_SHARE_SOURCE_PREFIX):
                fail("Share Unity source is outside src/share: %s" % path)
            if path in unity_paths:
                fail("duplicate Share Unity source: %s" % path)
            unity_paths[path] = True

    standalone_paths = {}
    for source in standalone_sources:
        if source.kind != "source":
            fail("non-source Share standalone input: %s" % source.path)
        if not source.path.startswith(_SHARE_SOURCE_PREFIX):
            fail("Share standalone source is outside src/share: %s" % source.path)
        if source.path in standalone_paths:
            fail("duplicate Share standalone source: %s" % source.path)
        if source.path in unity_paths:
            fail("Share source appears in Unity and standalone: %s" % source.path)
        standalone_paths[source.path] = True

    if len(group_keys) != 28:
        fail("Share inventory must contain 28 Unity groups, got %s" % len(group_keys))
    if len(unity_paths) != 312:
        fail("Share inventory must contain 312 Unity sources, got %s" % len(unity_paths))
    if len(standalone_paths) != 46:
        fail("Share inventory must contain 46 standalone sources, got %s" % len(standalone_paths))
