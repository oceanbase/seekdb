"""Authoritative coarse module dependency policy for the seekdb Bazel graph.

The policy is intentionally consumer -> producer: an entry under `sql` lists
the modules that SQL may depend on.  BUILD-file visibility does not grant a
cross-module edge that is absent here.
"""

# A package belongs to the module with the longest matching root.  Every
# package under //src must be registered here before it can participate in the
# checked graph.  Easy is owned by OBLib, not modeled as a peer module.
MODULE_ROOTS = {
    "data_plane": "src/data_plane",
    "logservice": "src/logservice",
    "oblib": "src/oblib",
    "observer": "src/observer",
    "pl": "src/pl",
    "query": "src/query",
    "rootserver": "src/rootserver",
    "share": "src/share",
    "standby": "src/standby",
    "sql": "src/sql",
    "storage": "src/storage",
    "wasm": "src/wasm",
}

# Authoritative production module graph. Every direct cross-module BUILD edge
# must be present here, and entries without a real BUILD edge are removed.
ALLOWED_MODULE_DEPS = {
    "wasm": ["observer", "oblib"],
    "data_plane": [
        "oblib",
        "share",
    ],
    "logservice": [
        "data_plane",
        "oblib",
        "query",
        "share",
    ],
    "oblib": [],
    "observer": [
        "data_plane",
        "logservice",
        "oblib",
        "pl",
        "query",
        "rootserver",
        "share",
        "sql",
        "storage",
        "standby",
    ],
    "pl": [
        "data_plane",
        "oblib",
        "query",
        "share",
        "sql",
    ],
    "query": [
        "oblib",
        "share",
    ],
    "rootserver": [
        "data_plane",
        "logservice",
        "oblib",
        "pl",
        "query",
        "share",
        "sql",
        "storage",
    ],
    "share": [
        "oblib",
    ],
    "sql": [
        "data_plane",
        "oblib",
        "query",
        "share",
    ],
    "storage": [
        "data_plane",
        "logservice",
        "oblib",
        "query",
        "share",
    ],
    "standby": [
        "logservice",
        "oblib",
        "share",
        "storage",
    ],
}

# C++ unit tests are owned by exactly one production module.  The longest
# matching root wins, just as it does for production packages.  A source under
# //unittest that is not covered here is unowned test debt and must be moved or
# deleted before it can enter the Bazel unit-test graph.
UNITTEST_MODULE_ROOTS = {
    "data_plane": "unittest/data_plane",
    "logservice": "unittest/logservice",
    "observer": "unittest/observer",
    "oblib": "unittest/oblib",
    "pl": "unittest/pl",
    "query": "unittest/query",
    "rootserver": "unittest/rootserver",
    "share": "unittest/share",
    "standby": "unittest/standby",
    "sql": "unittest/sql",
    "storage": "unittest/storage",
}

# Unit-test source code may name its module under test and the foundational
# runtime modules below.  Peer production modules are deliberately absent:
# those dependencies must be replaced with a module-local fixture/adapter, be
# moved to their real owner, or be removed from the C++ unit-test suite.
UNITTEST_ALLOWED_DIRECT_MODULE_DEPS = {
    "data_plane": [
        "oblib",
    ],
    "logservice": [
        "oblib",
    ],
    "observer": [
        "oblib",
    ],
    "oblib": [],
    "pl": [
        "oblib",
    ],
    "query": [
        "oblib",
    ],
    "rootserver": [
        "oblib",
    ],
    "share": [
        "oblib",
    ],
    "standby": [
        "oblib",
    ],
    "sql": [
        "oblib",
    ],
    "storage": [
        "oblib",
    ],
}

# This target supplies production implementation symbols only.  It owns no
# headers and therefore does not grant a unit test source-level access to the
# Observer module.
UNITTEST_RUNTIME_DEPS = [
    "//src/observer:liboceanbase",
]
