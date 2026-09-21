"""Module-level C++ unit-test binaries with generated Unity translation units."""

load(
    "//bazel:seekdb_cc_library.bzl",
    "seekdb_cc_library",
    "seekdb_generated_unity_source",
)
load("//bazel:seekdb_cc_test.bzl", "seekdb_cc_test")


def _partition(values, size):
    if size <= 0:
        fail("unity_size must be positive")
    return [
        values[offset:offset + size]
        for offset in range(0, len(values), size)
    ]


def seekdb_module_cc_test(
        name,
        srcs,
        test_hdrs,
        module_interface,
        unity_size = 8,
        unity_exceptions = {},
        shard_count = 8,
        data = [],
        tags = [],
        visibility = None):
    """Builds one Module-owned test ELF from private Unity compile groups.

    Test registration objects are always linked into the final ELF; the only
    entry point is //unittest:all_tests_main.cpp.
    """

    if not srcs:
        fail("%s has no unit-test sources" % name)
    unknown_unity_exceptions = [
        src
        for src in unity_exceptions
        if src not in srcs
    ]
    if unknown_unity_exceptions:
        fail("%s unity_exceptions are not present in srcs: %s" % (name, unknown_unity_exceptions))
    missing_reasons = [
        src
        for src, reason in unity_exceptions.items()
        if not reason.strip()
    ]
    if missing_reasons:
        fail("%s unity_exceptions must explain why isolation is required: %s" % (name, missing_reasons))

    header_target = "_" + name + "_headers"
    seekdb_cc_library(
        name = header_target,
        hdrs = test_hdrs,
        testonly = True,
        tags = tags,
        visibility = ["//visibility:private"],
    )

    unity_targets = []
    unity_members = sorted([
        src
        for src in srcs
        if src not in unity_exceptions
    ])
    unity_groups = _partition(unity_members, unity_size)
    unity_groups.extend([[src] for src in sorted(unity_exceptions)])
    for index, members in enumerate(unity_groups):
        group_name = "_%s_unity_%d" % (name, index)
        member_target = group_name + "_members"
        seekdb_cc_library(
            name = member_target,
            textual_hdrs = members,
            testonly = True,
            tags = tags,
            visibility = ["//visibility:private"],
        )
        unity_source = seekdb_generated_unity_source(
            name = group_name,
            unity_members = members,
            tags = tags,
        )
        seekdb_cc_library(
            name = group_name,
            srcs = [unity_source],
            alwayslink = True,
            deps = [
                "@seekdb_3rd_headers//:gmock",
                "@seekdb_3rd_headers//:gtest",
            ],
            implementation_deps = [
                ":" + header_target,
                ":" + member_target,
                module_interface,
            ],
            testonly = True,
            tags = tags,
            visibility = ["//visibility:private"],
        )
        unity_targets.append(":" + group_name)

    # Keep cc_test link-only.  All translation units must compile through
    # seekdb_cc_library so Sanity's pass plugin remains a declared input under
    # the Bazel 8 cc_test API.
    main_target = "_" + name + "_main"
    seekdb_cc_library(
        name = main_target,
        srcs = ["//unittest:all_tests_main.cpp"],
        alwayslink = True,
        deps = [
            "@seekdb_3rd_headers//:gtest",
        ],
        testonly = True,
        tags = tags,
        visibility = ["//visibility:private"],
    )

    seekdb_cc_test(
        name = name,
        data = data,
        deps = [":" + main_target] + unity_targets + [
            module_interface,
            "@seekdb_3rd_headers//:gmock",
            "@seekdb_3rd_headers//:gtest",
        ],
        linkopts = [
            "-pthread",
            "-ldl",
        ],
        shard_count = shard_count,
        tags = tags,
        visibility = visibility,
    )
