"""Declarations for OBLib's semantic public header interfaces."""

load(
    ":oblib_header_inventory.bzl",
    "OBLIB_ALL_HEADER_TARGETS",
    "OBLIB_ALL_THIRD_PARTY_HEADER_DEPS",
    "OBLIB_HEADER_TARGETS",
    "OBLIB_PRIVATE_HEADERS",
)
load("//bazel:defs.bzl", cc_library = "seekdb_cc_library")


def declare_oblib_header_interfaces(interface_visibility):
    """Declares the public semantic owner DAG and private implementation seam."""

    public_headers = []
    for spec in OBLIB_HEADER_TARGETS.values():
        public_headers += spec["hdrs"]

    for name, spec in OBLIB_HEADER_TARGETS.items():
        cc_library(
            name = name,
            hdrs = spec["hdrs"],
            deps = spec["deps"],
            includes = ["."],
            features = ["layering_check"],
            tags = ["manual"],
            visibility = interface_visibility,
        )

    # This is the only complete OBLib header aggregation.  It is intentionally
    # package-private and may be used only by OBLib implementation actions and
    # OBLib's test-only seam.
    cc_library(
        name = "_oblib_implementation_headers",
        hdrs = OBLIB_PRIVATE_HEADERS,
        textual_hdrs = public_headers,
        deps = (
            OBLIB_ALL_HEADER_TARGETS +
            OBLIB_ALL_THIRD_PARTY_HEADER_DEPS +
            ["//src/oblib/easy:easy"]
        ),
        includes = ["."],
        features = ["layering_check"],
        tags = ["manual"],
    )
