"""Transport features shared by the Rust library and its CXX generator."""

FEATURES = {
    "@platforms//os:emscripten": ["memory-transport"],
    "//conditions:default": ["native-network"],
}
