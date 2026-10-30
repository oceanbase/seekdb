"""CXX interfaces exposed to C++ callers; Rust targets live in each crate."""

load("//rust/sql-nio:features.bzl", "FEATURES")

MODULES = {
    "local": {
        "bridge": "src/bridge.rs",
        "headers": ["include/callbacks.h"],
    },
    "config": {
        "bridge": "generated_config.rs",
        "headers": ["include/check_callback.h", "config_checkers.h"],
        "deps": [":local_headers"],
    },
    "logger": {"bridge": "src/bridge.rs"},
    "sql-nio": {
        "bridge": "src/bridge.rs",
        "headers": ["include/callbacks.h"],
        "features": FEATURES,
    },
    "regex-engine": {"bridge": "src/bridge.rs"},
    "uuid-generator": {"bridge": "src/bridge.rs"},
    "in-process": {
        "bridge": "src/bridge.rs",
        "headers": ["include/in_process_server.h"],
        "exclude_platforms": ["windows", "emscripten"],
    },
}
