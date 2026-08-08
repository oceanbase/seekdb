"""Validation and grouping helpers for Observer's native composition root."""

_OBSERVER_SOURCE_PREFIX = "src/observer/"

def observer_singleton_unity_groups(sources, name_prefix):
    groups = []
    for index, source in enumerate(sources):
        if source.kind != "source" or source.language != "c++":
            fail("unsupported Observer standalone source: %s" % source.path)
        if not source.path.startswith(_OBSERVER_SOURCE_PREFIX):
            fail("Observer source outside src/observer: %s" % source.path)
        groups.append(struct(
            name = "%s_%d" % (name_prefix, index),
            language = "c++",
            srcs = [source.path],
            generated_srcs = [],
            external_srcs = [],
        ))
    return groups

def observer_validate_source_inventory(
        unity_groups,
        standalone_source_sets,
        checked_in_sources,
        ignored_sources,
        generated_sources):
    group_names = {}
    owned = {}
    unity_count = 0
    standalone_count = 0

    for group in unity_groups:
        if group.name in group_names:
            fail("duplicate Observer Unity group: %s" % group.name)
        group_names[group.name] = True
        if group.language != "c++" or group.generated_srcs or group.external_srcs:
            fail("invalid native Observer Unity group: %s" % group.name)
        for path in group.srcs:
            if not path.startswith(_OBSERVER_SOURCE_PREFIX):
                fail("Observer Unity source outside src/observer: %s" % path)
            package_path = path[len(_OBSERVER_SOURCE_PREFIX):]
            if package_path in owned:
                fail("duplicate Observer source: %s" % path)
            owned[package_path] = group.name
            unity_count += 1

    for source_set in standalone_source_sets:
        for source in source_set:
            if source.kind != "source" or source.language != "c++":
                fail("invalid Observer standalone source: %s" % source.path)
            if not source.path.startswith(_OBSERVER_SOURCE_PREFIX):
                fail("Observer standalone source outside src/observer: %s" % source.path)
            package_path = source.path[len(_OBSERVER_SOURCE_PREFIX):]
            if package_path in owned:
                fail("duplicate Observer standalone source: %s" % source.path)
            owned[package_path] = "standalone"
            standalone_count += 1

    if len(unity_groups) != 18 or unity_count != 246:
        fail(
            "Observer Unity baseline changed: groups=%s sources=%s" %
            (len(unity_groups), unity_count),
        )
    if standalone_count != 5:
        fail("Observer standalone baseline changed: %s" % standalone_count)

    ignored = {path: True for path in ignored_sources}
    if len(ignored) != 6:
        fail("Observer ignored-source baseline changed: %s" % len(ignored))
    for path in ignored:
        if path in owned:
            fail("ignored Observer source is also owned: %s" % path)

    generated = {}
    for path in generated_sources:
        if not path.startswith(_OBSERVER_SOURCE_PREFIX):
            fail("generated Observer source outside src/observer: %s" % path)
        generated[path[len(_OBSERVER_SOURCE_PREFIX):]] = True
    if len(generated) != 1:
        fail("Observer generated-source baseline changed: %s" % len(generated))
    missing_generated = sorted([path for path in generated if path not in owned])
    if missing_generated:
        fail("generated Observer sources are not owned: %s" % missing_generated)

    checked_in = {path: True for path in checked_in_sources}
    missing = sorted([
        path
        for path in checked_in
        if path not in owned and path not in ignored
    ])
    stale = sorted([
        path
        for path in owned
        if path not in checked_in and path not in generated
    ])
    stale_ignored = sorted([path for path in ignored if path not in checked_in])
    if missing or stale or stale_ignored:
        fail(
            "Observer source ownership differs from the tree: missing=%s stale=%s stale_ignored=%s" %
            (missing, stale, stale_ignored),
        )

def observer_validate_header_inventory(private_headers, checked_in_headers):
    owned = {path: True for path in private_headers}
    if len(owned) != len(private_headers):
        fail("duplicate Observer private header")

    checked_in = {path: True for path in checked_in_headers}
    missing = sorted([path for path in checked_in if path not in owned])
    stale = sorted([path for path in owned if path not in checked_in])
    if missing or stale:
        fail(
            "Observer header ownership differs from the tree: missing=%s stale=%s" %
            (missing, stale),
        )
