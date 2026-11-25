"""Optional ownership diagnostics shared by Rust and its generated CXX bridge."""

FEATURES = {
    "//bazel:storage_io_ownership_test_support_enabled": ["cxx", "ownership-test-support"],
    "//conditions:default": ["cxx"],
}
