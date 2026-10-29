"""Source grouping for seekdb libraries."""


def select_unity_groups(groups, names, language = "c++"):
    """Selects named groups in inventory order, checking the complete selection."""
    requested = {name: False for name in names}
    selected = []
    for group in groups:
        if (language == None or group.language == language) and group.name in requested:
            if requested[group.name]:
                fail("duplicate Unity group: %s" % group.name)
            requested[group.name] = True
            selected.append(group)
    missing = sorted([name for name, found in requested.items() if not found])
    if missing:
        fail("missing Unity groups: %s" % missing)
    return selected

def singleton_unity_groups(sources, name_prefix, language = "c++"):
    return [
        struct(name = "%s_%d" % (name_prefix, index), language = language, srcs = [source])
        for index, source in enumerate(sources)
    ]
