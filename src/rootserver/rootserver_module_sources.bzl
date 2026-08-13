"""Validation and grouping helpers for Rootserver's native source module."""

_ROOTSERVER_SOURCE_PREFIX = "src/rootserver/"

def rootserver_singleton_unity_groups(sources, name_prefix):
    groups = []
    for index, source in enumerate(sources):
        if source.kind != "source" or source.language != "c++":
            fail("unsupported Rootserver standalone source: %s" % source.path)
        if not source.path.startswith(_ROOTSERVER_SOURCE_PREFIX):
            fail("Rootserver source outside src/rootserver: %s" % source.path)
        groups.append(struct(
            name = "%s_%d" % (name_prefix, index),
            language = "c++",
            srcs = [source.path],
            generated_srcs = [],
            external_srcs = [],
        ))
    return groups

def rootserver_validate_source_inventory(
        unity_groups,
        standalone_sources,
        checked_in_sources,
        ignored_sources):
    group_names = {}
    owned = {}
    for group in unity_groups:
        if group.name in group_names:
            fail("duplicate Rootserver Unity group: %s" % group.name)
        group_names[group.name] = True
        if group.language != "c++" or group.generated_srcs or group.external_srcs:
            fail("invalid native Rootserver Unity group: %s" % group.name)
        for path in group.srcs:
            if not path.startswith(_ROOTSERVER_SOURCE_PREFIX):
                fail("Rootserver Unity source outside src/rootserver: %s" % path)
            package_path = path[len(_ROOTSERVER_SOURCE_PREFIX):]
            if package_path in owned:
                fail("duplicate Rootserver source: %s" % path)
            owned[package_path] = group.name

    for source in standalone_sources:
        if source.kind != "source" or source.language != "c++":
            fail("invalid Rootserver standalone source: %s" % source.path)
        if not source.path.startswith(_ROOTSERVER_SOURCE_PREFIX):
            fail("Rootserver standalone source outside src/rootserver: %s" % source.path)
        package_path = source.path[len(_ROOTSERVER_SOURCE_PREFIX):]
        if package_path in owned:
            fail("duplicate Rootserver standalone source: %s" % source.path)
        owned[package_path] = "standalone"

    ignored = {path: True for path in ignored_sources}
    for path in ignored:
        if path in owned:
            fail("ignored Rootserver source is also owned: %s" % path)

    checked_in = {path: True for path in checked_in_sources}
    missing = sorted([
        path
        for path in checked_in
        if path not in owned and path not in ignored
    ])
    stale = sorted([path for path in owned if path not in checked_in])
    stale_ignored = sorted([path for path in ignored if path not in checked_in])
    if missing or stale or stale_ignored:
        fail(
            "Rootserver source ownership differs from the tree: missing=%s stale=%s stale_ignored=%s" %
            (missing, stale, stale_ignored),
        )
