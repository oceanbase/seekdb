"""Bazel repositories backed by packages published from seekdb-deps."""

def _seekdb_deps_repository_impl(repository_ctx):
    lock = json.decode(repository_ctx.read(repository_ctx.attr.lock_file))
    if lock.get("schema_version") != 1:
        fail("unsupported deps.lock.json schema")

    matches = [package for package in lock.get("packages", [])
               if package.get("name") == repository_ctx.attr.package_name and
               package.get("target") == repository_ctx.attr.target and
               package.get("artifact_type") == "binary"]
    if len(matches) != 1:
        fail("deps.lock.json must contain exactly one binary package for %s (%s)" %
             (repository_ctx.attr.package_name, repository_ctx.attr.target))

    package = matches[0]
    version = package.get("version")
    url = package.get("url")
    sha256 = package.get("sha256")
    if not version or not url or not sha256:
        fail("incomplete deps.lock.json entry for %s" % repository_ctx.attr.package_name)

    archive = repository_ctx.path("package.tar.gz")
    repository_ctx.download(
        url = url,
        output = archive,
        sha256 = sha256,
    )
    repository_ctx.extract(
        archive = archive,
        stripPrefix = "%s-%s" % (repository_ctx.attr.package_name, version),
    )
    repository_ctx.file("BUILD.bazel", """
load("@rules_cc//cc:cc_import.bzl", "cc_import")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_import(
    name = "gtest_archive",
    static_library = "lib/libgtest.a",
)

cc_library(
    name = "gtest",
    hdrs = glob(["include/gtest/**/*.h"]),
    includes = ["include"],
    defines = ["_GLIBCXX_USE_CXX11_ABI=1"],
    linkopts = select({
        "@platforms//os:linux": ["-pthread"],
        "//conditions:default": [],
    }),
    deps = [":gtest_archive"],
)
""")

seekdb_deps_repository = repository_rule(
    implementation = _seekdb_deps_repository_impl,
    attrs = {
        "lock_file": attr.label(mandatory = True, allow_single_file = True),
        "package_name": attr.string(mandatory = True),
        "target": attr.string(mandatory = True),
    },
)
