"""Helpers for declaring OBLib's semantic native source groups."""

def standalone_sources(sources, key):
    prefix = "src/oblib/"
    result = []
    for source in sources[key]:
        if not source.startswith(prefix):
            fail("source outside src/oblib: %s" % source)
        result.append(source[len(prefix):])
    if not result:
        fail("no standalone sources in %s" % key)
    return result
