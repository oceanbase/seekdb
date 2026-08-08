"""Helpers for declaring OBLib's semantic native source groups."""

def oblib_select_unity_groups(groups, names, language = "c++"):
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
        fail("missing %s Unity groups: %s" % (language, missing))
    return selected

def oblib_exclude_unity_sources(groups, paths):
    excluded = {path: False for path in paths}
    result = []
    for group in groups:
        sources = []
        for source in group.srcs:
            if source in excluded:
                excluded[source] = True
            else:
                sources.append(source)
        if not sources:
            fail("excluding sources emptied Unity group %s" % group.name)
        result.append(struct(
            name = group.name,
            language = group.language,
            srcs = sources,
            generated_srcs = group.generated_srcs,
            external_srcs = group.external_srcs,
        ))
    missing = sorted([path for path, found in excluded.items() if not found])
    if missing:
        fail("excluded Unity sources were not found: %s" % missing)
    return result

def oblib_standalone_sources(sources, key, language = "c++"):
    prefix = "src/oblib/"
    result = []
    for source in sources[key]:
        if source.kind != "source":
            fail("non-source input in %s: %s" % (key, source.path))
        if source.language == language:
            if not source.path.startswith(prefix):
                fail("source outside src/oblib: %s" % source.path)
            result.append(source.path[len(prefix):])
    if not result:
        fail("no %s standalone sources in %s" % (language, key))
    return result
