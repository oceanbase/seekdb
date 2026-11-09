"""CXX interfaces exposed to C++ callers; Rust targets live in each crate."""

load("//rust/sql-nio:features.bzl", "FEATURES")

MODULES = {
    "mvt": {"bridge": "src/bridge.rs"},
    "standby-rpc": {
        "bridge": "src/bridge.rs",
        "headers": ["include/service.h"],
        "exclude_platforms": ["emscripten"],
    },
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
    "number-parser": {"bridge": "src/bridge.rs"},
    "sql-nio": {
        "bridge": "src/bridge.rs",
        "headers": ["include/callbacks.h"],
        "features": FEATURES,
    },
    "regex-engine": {"bridge": "src/bridge.rs"},
    "xml-parser": {"bridge": "src/bridge.rs", "headers": ["include/callbacks.h"]},
    "compression": {"bridge": "src/bridge.rs"},
    "crypto": {
        "bridge": "src/bridge.rs",
        "headers": ["include/sdk.h"],
        "deps": ["//rust/crypto:sdk_headers"],
    },
    "spatial-covering": {"bridge": "src/bridge.rs"},
    "http-client": {
        "bridge": "src/bridge.rs",
        "headers": ["include/sdk.h"],
        "deps": select({
            "@platforms//os:emscripten": [],
            "//conditions:default": ["//rust/http-client:sdk_headers"],
        }),
        "exclude_platforms": ["emscripten"],
    },
    "json": {"bridge": "src/bridge.rs", "headers": ["include/binary_source.h", "include/schema_pattern.h", "include/read.h"], "deps": ["//rust/json:schema_pattern"]},
    "uuid-generator": {"bridge": "src/bridge.rs"},
    "in-process": {
        "bridge": "src/bridge.rs",
        "headers": ["include/in_process_server.h"],
        "exclude_platforms": ["windows", "emscripten"],
    },
}
