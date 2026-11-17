# Copyright (c) 2026 OceanBase.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Expose the prebuilt jemalloc archive and headers on supported platforms."""

load("@rules_cc//cc:cc_import.bzl", "cc_import")
load("@rules_cc//cc:cc_library.bzl", "cc_library")

_SUPPORTED_PLATFORMS = select({
    "@platforms//os:linux": [],
    "@platforms//os:macos": [],
    "@platforms//os:android": [],
    "@platforms//os:windows": [],
    "//conditions:default": ["@platforms//:incompatible"],
})

def jemalloc(name):
    """Expose the platform's prebuilt jemalloc to C++ targets."""

    native.filegroup(
        name = "%s_archive" % name,
        srcs = select({
            "@platforms//os:linux": ["@third_party_native//:lib/libjemalloc_pic.a"],
            "@platforms//os:macos": ["@third_party_native//:lib/libjemalloc_pic.a"],
            "@platforms//os:android": ["@third_party_android//:lib/libjemalloc_pic.a"],
            "@platforms//os:windows": ["@third_party_native//:lib/jemalloc.lib"],
            "//conditions:default": [],
        }),
        target_compatible_with = _SUPPORTED_PLATFORMS,
        visibility = ["//src/observer:__pkg__"],
    )

    cc_library(
        name = "%s_headers" % name,
        deps = select({
            "@platforms//os:linux": ["@third_party_native//:jemalloc_headers"],
            "@platforms//os:macos": ["@third_party_native//:jemalloc_headers"],
            "@platforms//os:android": ["@third_party_android//:jemalloc_headers"],
            "@platforms//os:windows": ["@third_party_native//:jemalloc_headers"],
            "//conditions:default": [],
        }),
        target_compatible_with = _SUPPORTED_PLATFORMS,
        visibility = ["//src/oblib:__pkg__", "//src/observer:__pkg__"],
    )

    cc_import(
        name = "%s_library" % name,
        static_library = ":%s_archive" % name,
        target_compatible_with = _SUPPORTED_PLATFORMS,
        visibility = ["//src/oblib:__pkg__", "//unittest/oblib:__pkg__"],
    )
