#!/usr/bin/env python3
"""Build pinned foundational iOS libraries without using macOS binary packages."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess

from vsag_packages import PACKAGES as VSAG_PACKAGES

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_PACKAGES = ("zlib", "openssl", "curl", "abseil", "s2", "roaring",
                    "lzma", "libxml2", "protobuf-c", "sqlite",
                    "icu", "openmp", "lapack", "vsag")

PACKAGES = {
    "lapack": ("3.12.0", "https://codeload.github.com/Reference-LAPACK/lapack/tar.gz/refs/tags/v3.12.0",
               "eac9570f8e0ad6f30ce4b963f4f033f0f643e7c3912fc9ee6cd99120675ad48b"),
    "openmp": ("21.1.8", "https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/openmp-21.1.8.src.tar.xz",
               "856b023748b41ac7b2c83fd8e9f765ff48a4df2fe6777d2811ef7c7ed8f2f977"),
    "llvm-cmake": ("21.1.8", "https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/cmake-21.1.8.src.tar.xz",
                   "85735f20fd8c81ecb0a09abb0c267018475420e93b65050cc5b7634eab744de9"),
    "lzma": ("5.4.7", "https://codeload.github.com/tukaani-project/xz/tar.gz/refs/tags/v5.4.7",
             "87ef1bb0183ce477ba3ddb2700000b4c1e723f8d984e8f678dddcb11c5bf4f99"),
    "icu": ("69.1", "https://github.com/unicode-org/icu/releases/download/release-69-1/icu4c-69_1-src.tgz",
            "4cba7b7acd1d3c42c44bb0c14be6637098c7faf2b330ce876bc5f3b915d09745"),
    "abseil": ('20211102.0', 'https://codeload.github.com/abseil/abseil-cpp/tar.gz/refs/tags/20211102.0',
        'dcf71b9cba8dc0ca9940c4b316a0c796be8fab42b070bb6b7cab62b48f0e66c4'),
    "s2": ('0.10.0', 'https://codeload.github.com/google/s2geometry/tar.gz/refs/tags/v0.10.0',
        '1c17b04f1ea20ed09a67a83151ddd5d8529716f509dde49a8190618d70532a3d'),
    "roaring": ('3.0.0', 'https://codeload.github.com/RoaringBitmap/CRoaring/tar.gz/refs/tags/v3.0.0',
        '25183bc54ab650d964256d547869a34573a13d06f7e6a369b79e77f5c1feb8ba'),
    "libxml2": ('2.10.4', 'https://codeload.github.com/GNOME/libxml2/tar.gz/refs/tags/v2.10.4',
        '6f6fb27f91bb65f9d7196e3c616901b3e18a7dea31ccc2ae857940b125faa780'),
    "protobuf-c": ('1.4.1', 'https://codeload.github.com/protobuf-c/protobuf-c/tar.gz/refs/tags/v1.4.1',
        '99be336cdb15dfc5827efe34e5ac9aaa962e2485db547dd254d2a122a7d23102'),
    "sqlite": ('3.38.1', 'https://www.sqlite.org/2022/sqlite-amalgamation-3380100.zip',
        '6fb55507d4517b5cbc80bd2db57b0cbe1b45880b28f2e4bd6dca4cfe3716a231'),

    "curl": ("8.12.1", "https://curl.se/download/curl-8.12.1.tar.gz",
             "7b40ea64947e0b440716a4d7f0b7aa56230a5341c8377d7b609649d4aea8dbcf"),
    "zlib": ("1.2.13", "https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.2.13",
             "1525952a0a567581792613a9723333d7f8cc20b87a81f920fb8bc7e3f2251428"),
    "openssl": ("1.1.1u", "https://codeload.github.com/openssl/openssl/tar.gz/refs/tags/OpenSSL_1_1_1u",
                "fafe27202bde4238dce258d82ec8a8592a657842e5431264620a933a0c9436b7"),
}

PACKAGES.update(VSAG_PACKAGES)


def run(command, directory, environment):
    """Execute one build stage and propagate failure without installing partial output."""
    print("+", " ".join(str(part) for part in command), flush=True)
    subprocess.run([str(part) for part in command], cwd=directory, env=environment, check=True)


def source_package(name, environment):
    """Fetch a checksum-pinned upstream archive and extract it inside this checkout."""
    version, url, digest = PACKAGES[name]
    cache = ROOT / "deps/ios"
    suffix = ".zip" if url.endswith(".zip") else (".tar.xz" if url.endswith(".xz") else (".tar.bz2" if url.endswith(".bz2") else ".tar.gz"))
    archive = cache / "downloads" / (name + "-" + version + suffix)
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        partial = archive.with_suffix(".partial")
        run(["curl", "--fail", "--location", "--retry", "2", "--max-time", "180",
             url, "--output", partial], ROOT, environment)
        if hashlib.sha256(partial.read_bytes()).hexdigest() != digest:
            raise RuntimeError("Downloaded checksum mismatch: " + name)
        partial.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
        raise RuntimeError("Cached checksum mismatch: " + str(archive))
    source = cache / "sources" / (name + "-" + version)
    marker = source / ".seekdb-source-sha256"
    if not marker.exists() or marker.read_text() != digest:
        source.mkdir(parents=True, exist_ok=True)
        if suffix == ".zip":
            run(["unzip", "-q", "-o", archive, "-d", source], ROOT, environment)
        else:
            run(["tar", "-xf", archive, "-C", source, "--strip-components=1"], ROOT, environment)
        marker.write_text(digest)
    return source


def verify_archive(archive, simulator, environment):
    """Reject archives with non-arm64 or wrong-platform Mach-O members."""
    arches = subprocess.check_output(["xcrun", "lipo", "-archs", str(archive)],
                                     env=environment, text=True).strip()
    if arches != "arm64":
        raise RuntimeError("Unexpected archive architectures: " + arches)
    output = subprocess.check_output(["xcrun", "otool", "-l", str(archive)],
                                     env=environment, text=True)
    platforms = set(re.findall(r"^\s*platform\s+(\S+)", output, re.MULTILINE))
    expected = {"7", "IOSSIMULATOR"} if simulator else {"2", "IOS"}
    if not platforms or not platforms.issubset(expected):
        raise RuntimeError("Unexpected archive platforms: " + repr(platforms))


def build_zlib(source, build, prefix, sdk, options, environment):
    """Compile zlib's static target and install its public headers and archive."""
    # Modern Apple SDKs define TARGET_OS_MAC even for iOS; this is not classic Mac OS.
    header = source / "zutil.h"
    original = "#if defined(MACOS) || defined(TARGET_OS_MAC)"
    patched = "#if defined(MACOS) && !defined(__APPLE__)"
    content = header.read_text()
    if original in content:
        header.write_text(content.replace(original, patched))
    elif patched not in content:
        raise RuntimeError("Unrecognized zlib platform guard")
    run(["cmake", "-S", source, "-B", build, "-DCMAKE_SYSTEM_NAME=iOS",
         "-DCMAKE_OSX_SYSROOT=" + sdk, "-DCMAKE_OSX_ARCHITECTURES=arm64",
         "-DCMAKE_OSX_DEPLOYMENT_TARGET=" + options.deployment_target,
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"], ROOT, environment)
    run(["cmake", "--build", build, "--target", "zlibstatic", "--parallel", str(options.jobs)], ROOT, environment)
    archive = build / "libz.a"
    verify_archive(archive, options.simulator, environment)
    shutil.copy2(archive, prefix / "lib/libz.a")
    shutil.copy2(source / "zlib.h", prefix / "include/zlib.h")
    shutil.copy2(build / "zconf.h", prefix / "include/zconf.h")


def build_openssl(source, build, prefix, sdk, options, environment):
    """Build and install OpenSSL with the SDK's iOS target and no dynamic libraries."""
    target = "iossimulator-xcrun" if options.simulator else "ios64-xcrun"
    triple = "arm64-apple-ios" + options.deployment_target
    if options.simulator:
        triple += "-simulator"
    environment = dict(environment)
    minimum_flag = "-mios-simulator-version-min=" if options.simulator else "-miphoneos-version-min="
    environment["CFLAGS"] = ("-O2 -target " + triple + " -isysroot " + shlex.quote(sdk)
                             + " " + minimum_flag + options.deployment_target)
    run(["perl", source / "Configure", target, "no-shared", "no-tests",
         "--prefix=" + str(prefix), "--openssldir=" + str(prefix / "ssl")], build, environment)
    run(["make", "-j" + str(options.jobs), "build_libs"], build, environment)
    for name in ("libssl.a", "libcrypto.a"):
        verify_archive(build / name, options.simulator, environment)
    run(["make", "install_dev"], build, environment)


def build_curl(source, build, prefix, sdk, options, environment):
    """Build libcurl against the verified local OpenSSL and zlib archives."""
    for library in ("libssl.a", "libcrypto.a", "libz.a"):
        verify_archive(prefix / "lib" / library, options.simulator, environment)
    run(["cmake", "-S", source, "-B", build, "-DCMAKE_SYSTEM_NAME=iOS",
         "-DCMAKE_OSX_SYSROOT=" + sdk, "-DCMAKE_OSX_ARCHITECTURES=arm64",
         "-DCMAKE_OSX_DEPLOYMENT_TARGET=" + options.deployment_target,
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
         "-DBUILD_CURL_EXE=OFF", "-DBUILD_SHARED_LIBS=OFF", "-DBUILD_TESTING=OFF",
         "-DCURL_USE_OPENSSL=ON", "-DCURL_USE_SECTRANSP=OFF", "-DCURL_USE_LIBPSL=OFF",
         "-DCURL_USE_LIBSSH2=OFF", "-DUSE_NGHTTP2=OFF", "-DCURL_ZSTD=OFF",
         "-DCURL_BROTLI=OFF", "-DCURL_USE_LIBIDN2=OFF", "-DCURL_DISABLE_LDAP=ON",
         "-DCURL_DISABLE_LDAPS=ON", "-DCURL_ZLIB=ON",
         "-DOPENSSL_INCLUDE_DIR=" + str(prefix / "include"),
         "-DOPENSSL_SSL_LIBRARY=" + str(prefix / "lib/libssl.a"),
         "-DOPENSSL_CRYPTO_LIBRARY=" + str(prefix / "lib/libcrypto.a"),
         "-DZLIB_INCLUDE_DIR=" + str(prefix / "include"),
         "-DZLIB_LIBRARY=" + str(prefix / "lib/libz.a")], ROOT, environment)
    run(["cmake", "--build", build, "--parallel", str(options.jobs)], ROOT, environment)
    verify_archive(build / "lib/libcurl.a", options.simulator, environment)
    run(["cmake", "--install", build], ROOT, environment)


def build_cmake_package(name, source, build, prefix, sdk, options, environment):
    """Build a static CMake dependency using only target SDK and prefix libraries."""
    if name == "s2":
        # S2 overrides the cache standard; Abseil's string_view ABI must match.
        project_file = source / "CMakeLists.txt"
        content = project_file.read_text()
        original = "set(CMAKE_CXX_STANDARD 11)"
        replacement = "set(CMAKE_CXX_STANDARD 17)"
        if original not in content and replacement not in content:
            raise RuntimeError("Unexpected S2 C++ standard configuration")
        project_file.write_text(content.replace(original, replacement))
    settings = {
        "lzma": [],
        "lapack": ["-DLAPACK_SOURCE=" + str(source)],
        "abseil": ["-DABSL_BUILD_TESTING=OFF", "-DABSL_PROPAGATE_CXX_STD=ON", "-DABSL_ENABLE_INSTALL=ON"],
        "s2": ["-DBUILD_EXAMPLES=OFF", "-DWITH_GLOG=OFF", "-DWITH_GFLAGS=OFF",
               "-Dabsl_DIR=" + str(prefix / "lib/cmake/absl")],
        "roaring": ["-DENABLE_ROARING_TESTS=OFF", "-DROARING_BUILD_STATIC=ON"],
        "libxml2": ["-DLIBXML2_WITH_PROGRAMS=OFF", "-DLIBXML2_WITH_PYTHON=OFF",
                    "-DLIBXML2_WITH_TESTS=OFF", "-DLIBXML2_WITH_LZMA=ON",
                    "-DLIBLZMA_INCLUDE_DIR=" + str(prefix / "include"),
                    "-DLIBLZMA_LIBRARY=" + str(prefix / "lib/liblzma.a")],
    }
    project = ROOT / "deps/ios-build/lapacke" if name == "lapack" else source
    run(["cmake", "-S", project, "-B", build, "-DCMAKE_SYSTEM_NAME=iOS",
         "-DCMAKE_OSX_SYSROOT=" + sdk, "-DCMAKE_OSX_ARCHITECTURES=arm64",
         "-DCMAKE_OSX_DEPLOYMENT_TARGET=" + options.deployment_target,
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
         "-DCMAKE_PREFIX_PATH=" + str(prefix), "-DCMAKE_INSTALL_LIBDIR=lib",
         "-DCMAKE_CXX_STANDARD=17", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
         "-DBUILD_SHARED_LIBS=OFF", "-DBUILD_TESTING=OFF",
         "-DOPENSSL_INCLUDE_DIR=" + str(prefix / "include"),
         "-DOPENSSL_SSL_LIBRARY=" + str(prefix / "lib/libssl.a"),
         "-DOPENSSL_CRYPTO_LIBRARY=" + str(prefix / "lib/libcrypto.a"),
         "-DZLIB_INCLUDE_DIR=" + str(prefix / "include"),
         "-DZLIB_LIBRARY=" + str(prefix / "lib/libz.a"), *settings[name]], ROOT, environment)
    run(["cmake", "--build", build, "--parallel", str(options.jobs)], ROOT, environment)
    archives = list(build.rglob("lib*.a"))
    if not archives:
        raise RuntimeError("No static library produced: " + name)
    for archive in archives:
        verify_archive(archive, options.simulator, environment)
    run(["cmake", "--install", build], ROOT, environment)


def build_c_package(name, source, build, prefix, sdk, options, environment):
    """Compile checked-in C runtimes without running host protocol generators."""
    if name == "sqlite":
        source = next(source.glob("sqlite-amalgamation-*"))
        implementation = source / "sqlite3.c"
        headers = [source / "sqlite3.h", source / "sqlite3ext.h"]
        destination = prefix / "lib/sqlite/libsqlite3.a"
        header_directory = prefix / "include"
        flags = ["-DSQLITE_THREADSAFE=1", "-DSQLITE_ENABLE_FTS5", "-DSQLITE_ENABLE_RTREE"]
    else:
        implementation = source / "protobuf-c/protobuf-c.c"
        headers = [source / "protobuf-c/protobuf-c.h"]
        destination = prefix / "lib/libprotobuf-c.a"
        header_directory = prefix / "include/protobuf-c"
        flags = []
    triple = "arm64-apple-ios" + options.deployment_target
    if options.simulator:
        triple += "-simulator"
    obj = build / (name + ".o")
    archive = build / destination.name
    run(["xcrun", "clang", "-target", triple, "-isysroot", sdk, "-O2", "-fPIC",
         "-I" + str(source), *flags, "-c", implementation, "-o", obj], ROOT, environment)
    run(["xcrun", "ar", "rcs", archive, obj], ROOT, environment)
    verify_archive(archive, options.simulator, environment)
    destination.parent.mkdir(parents=True, exist_ok=True)
    header_directory.mkdir(parents=True, exist_ok=True)
    shutil.copy2(archive, destination)
    for header in headers:
        shutil.copy2(header, header_directory / header.name)




def build_vsag(source, build, prefix, sdk, options, environment):
    """Compile pinned VSAG and C++ dependencies with the repository's iOS adapter."""
    for library in ("libomp.a", "liblapacke.a", "libroaring.a"):
        verify_archive(prefix / "lib" / library, options.simulator, environment)
    adapter = ROOT / "deps/ios-build/vsag"
    dependencies = []
    for name in VSAG_PACKAGES:
        if name not in ("vsag", "boost"):
            path = source_package(name, environment)
            variable = name.upper().replace("-", "_")
            dependencies.append("-DSEEKDB_" + variable + "=" + str(path))
    shutil.copyfile(adapter / "CMakeLists.txt", source / "CMakeLists.txt")
    for relative in ("src/simd/CMakeLists.txt", "extern/diskann/diskann.cmake"):
        file = source / relative
        content = file.read_text()
        original = "-fopenmp -fopenmp-simd"
        replacement = "-Xpreprocessor -fopenmp"
        if original not in content and replacement not in content:
            raise RuntimeError("Unexpected VSAG OpenMP flags in " + relative)
        file.write_text(content.replace(original, replacement))
    boost = source_package("boost", environment)
    if not (boost / "boost/version.hpp").exists():
        raise RuntimeError("Pinned Boost headers are missing")
    run(["cmake", "-S", source, "-B", build, "-DCMAKE_SYSTEM_NAME=iOS",
         "-DCMAKE_OSX_SYSROOT=" + sdk, "-DCMAKE_OSX_ARCHITECTURES=arm64",
         "-DCMAKE_OSX_DEPLOYMENT_TARGET=" + options.deployment_target,
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
         "-DSEEKDB_PREFIX=" + str(prefix), "-DSEEKDB_ADAPTER=" + str(adapter),
         "-DSEEKDB_BOOST_INCLUDE=" + str(boost), *dependencies], ROOT, environment)
    run(["cmake", "--build", build, "--target", "vsag_static", "--parallel",
         str(options.jobs)], ROOT, environment)
    destination = prefix / "lib/vsag_lib"
    destination.mkdir(parents=True, exist_ok=True)
    archives = list(build.rglob("lib*.a"))
    if not any(path.name == "libvsag_static.a" for path in archives):
        raise RuntimeError("VSAG archive was not produced")
    for archive in archives:
        verify_archive(archive, options.simulator, environment)
        shutil.copy2(archive, destination / archive.name)
    shutil.copytree(source / "include/vsag", prefix / "include/vsag", dirs_exist_ok=True)


def build_openmp(source, build, prefix, sdk, options, environment):
    """Build the static LLVM OpenMP runtime without host Homebrew dependencies."""
    utilities = source_package("llvm-cmake", environment)
    project = source / "CMakeLists.txt"
    original = "set(LLVM_COMMON_CMAKE_UTILS ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)"
    replacement = 'set(LLVM_COMMON_CMAKE_UTILS "${SEEKDB_LLVM_CMAKE}")'
    content = project.read_text()
    if original not in content and replacement not in content:
        raise RuntimeError("Unexpected OpenMP CMake utility path")
    project.write_text(content.replace(original, replacement))
    run(["cmake", "-S", source, "-B", build, "-DCMAKE_SYSTEM_NAME=iOS",
         "-DCMAKE_OSX_SYSROOT=" + sdk, "-DCMAKE_OSX_ARCHITECTURES=arm64",
         "-DCMAKE_OSX_DEPLOYMENT_TARGET=" + options.deployment_target,
         "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
         "-DSEEKDB_LLVM_CMAKE=" + str(utilities), "-DLIBOMP_ENABLE_SHARED=OFF",
         "-DOPENMP_ENABLE_LIBOMPTARGET=OFF", "-DOPENMP_ENABLE_OMPT_TOOLS=OFF",
         "-DLIBOMP_OMPT_SUPPORT=OFF", "-DLIBOMP_USE_HWLOC=OFF"], ROOT, environment)
    run(["cmake", "--build", build, "--parallel", str(options.jobs)], ROOT, environment)
    verify_archive(build / "runtime/src/libomp.a", options.simulator, environment)
    run(["cmake", "--install", build], ROOT, environment)


def build_icu(source, build, prefix, sdk, options, environment):
    """Build native ICU generators first, then cross-compile the target libraries."""
    source = source / "source"
    host_build = ROOT / "deps/ios/host/icu"
    host_build.mkdir(parents=True, exist_ok=True)
    host_env = dict(environment)
    host_env.pop("IPHONEOS_DEPLOYMENT_TARGET", None)
    host_env["SDKROOT"] = subprocess.check_output(
        ["xcrun", "--sdk", "macosx", "--show-sdk-path"], env=host_env, text=True).strip()
    common = ["--enable-static", "--disable-shared", "--disable-tests", "--disable-samples",
              "--disable-extras", "--disable-icuio", "--with-data-packaging=static"]
    run([source / "configure", *common], host_build, host_env)
    run(["make", "-j" + str(options.jobs)], host_build, host_env)
    cross_env = dict(environment)
    triple = "arm64-apple-ios" + options.deployment_target
    if options.simulator:
        triple += "-simulator"
    cross_env["CFLAGS"] = "-O2 -target " + triple + " -isysroot " + shlex.quote(sdk)
    cross_env["CXXFLAGS"] = cross_env["CFLAGS"]
    run([source / "configure", *common, "--disable-tools", "--host=aarch64-apple-darwin",
         "--with-cross-build=" + str(host_build), "--prefix=" + str(prefix)], build, cross_env)
    run(["make", "-j" + str(options.jobs)], build, cross_env)
    for name in ("libicuuc.a", "libicui18n.a", "libicudata.a"):
        verify_archive(build / "lib" / name, options.simulator, environment)
    run(["make", "install"], build, cross_env)



PACKAGE_OUTPUTS = {
    "zlib": ("lib/libz.a", "include/zlib.h"),
    "openssl": ("lib/libssl.a", "lib/libcrypto.a", "include/openssl/ssl.h"),
    "curl": ("lib/libcurl.a", "include/curl/curl.h"),
    "abseil": ("lib/libabsl_*.a", "include/absl/base/config.h"),
    "s2": ("lib/libs2.a", "include/s2/s2cell.h"),
    "roaring": ("lib/libroaring.a", "include/roaring/roaring.h"),
    "lzma": ("lib/liblzma.a", "include/lzma.h"),
    "libxml2": ("lib/libxml2.a", "include/libxml2/libxml/parser.h"),
    "protobuf-c": ("lib/libprotobuf-c.a", "include/protobuf-c/protobuf-c.h"),
    "sqlite": ("lib/sqlite/libsqlite3.a", "include/sqlite3.h"),
    "icu": ("lib/libicuuc.a", "lib/libicui18n.a", "lib/libicudata.a", "include/unicode/utypes.h"),
    "openmp": ("lib/libomp.a", "include/omp.h"),
    "lapack": ("lib/liblapacke.a", "include/lapacke.h"),
    "vsag": ("lib/vsag_lib/lib*.a", "lib/vsag_lib/libvsag_static.a", "include/vsag/vsag.h"),
}


def installed_output_hashes(name, prefix):
    """Hash required installed outputs, returning no identity for missing/empty files."""
    outputs = {}
    for pattern in PACKAGE_OUTPUTS[name]:
        paths = sorted(prefix.glob(pattern))
        if not paths or any(not path.is_file() or path.stat().st_size == 0 for path in paths):
            return None
        for path in paths:
            outputs[str(path.relative_to(prefix))] = hashlib.sha256(path.read_bytes()).hexdigest()
    return outputs


def reusable_package(marker, identity, prefix):
    """Accept only matching build settings and unchanged installed output bytes."""
    try:
        recorded = json.loads(marker.read_text())
        outputs = installed_output_hashes(identity["package"], prefix)
        return outputs is not None and recorded == dict(identity, outputs=outputs)
    except (OSError, ValueError, TypeError):
        return False


def builder_identity():
    """Bind cache reuse to tracked dependency recipes and package version definitions."""
    digest = hashlib.sha256()
    for path in sorted((ROOT / "deps/ios-build").rglob("*")):
        if path.is_file() and path.suffix in {".py", ".txt", ".cmake"}:
            digest.update(str(path.relative_to(ROOT)).encode())
            digest.update(path.read_bytes())
    return digest.hexdigest()

def main():
    """Select a target SDK, isolate host flags, and build only requested packages."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reuse", action="store_true", help="Reuse matching verified installed packages")
    parser.add_argument("--simulator", action="store_true")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--deployment-target", default="18.0")
    parser.add_argument("packages", nargs="*", metavar="PACKAGE")
    options = parser.parse_args()
    unknown = set(options.packages) - (set(PACKAGES) - {"llvm-cmake"} - (set(VSAG_PACKAGES) - {"vsag"}))
    if unknown:
        parser.error("unknown packages: " + ", ".join(sorted(unknown)))
    if options.jobs < 1 or not re.fullmatch(r"[0-9]+(?:\.[0-9]+){0,2}", options.deployment_target):
        parser.error("jobs must be positive and deployment target must be an iOS version")
    environment = dict(os.environ)
    environment.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
    for name in ("CC", "CXX", "AR", "RANLIB", "CFLAGS", "CXXFLAGS", "CPPFLAGS", "LDFLAGS", "SDKROOT"):
        environment.pop(name, None)
    sdk_name = "iphonesimulator" if options.simulator else "iphoneos"
    sdk = subprocess.check_output(["xcrun", "--sdk", sdk_name, "--show-sdk-path"],
                                  env=environment, text=True).strip()
    environment["SDKROOT"] = sdk
    environment["IPHONEOS_DEPLOYMENT_TARGET"] = options.deployment_target
    prefix = ROOT / "deps/ios" / sdk_name / "devel"
    for directory in (prefix / "lib", prefix / "include"):
        directory.mkdir(parents=True, exist_ok=True)
    recipe = builder_identity()
    rebuilt = False
    for name in options.packages or DEFAULT_PACKAGES:
        build_name = name + "-" + PACKAGES[name][0] if name == "openmp" else name
        build = ROOT / "deps/ios" / sdk_name / "build" / build_name
        identity = {
            "package": name, "version": PACKAGES[name][0], "sha256": PACKAGES[name][2],
            "sdk": sdk, "deployment_target": options.deployment_target,
            "platform": sdk_name, "architecture": "arm64", "builder": recipe}
        marker = build / "verified.json"
        if options.reuse and not rebuilt and reusable_package(marker, identity, prefix):
            print("Reusing verified iOS dependency: " + name, flush=True)
            continue
        # Rebuild downstream packages when an earlier dependency has changed.
        rebuilt = True
        marker.unlink(missing_ok=True)
        source = source_package(name, environment)
        build.mkdir(parents=True, exist_ok=True)
        if name == "vsag":
            build_vsag(source, build, prefix, sdk, options, environment)
        elif name == "openmp":
            build_openmp(source, build, prefix, sdk, options, environment)
        elif name == "icu":
            build_icu(source, build, prefix, sdk, options, environment)
        elif name in ("abseil", "s2", "roaring", "libxml2", "lzma", "lapack"):
            build_cmake_package(name, source, build, prefix, sdk, options, environment)
        elif name in ("sqlite", "protobuf-c"):
            build_c_package(name, source, build, prefix, sdk, options, environment)
        else:
            {"zlib": build_zlib, "openssl": build_openssl, "curl": build_curl}[name](
                source, build, prefix, sdk, options, environment)
        outputs = installed_output_hashes(name, prefix)
        if outputs is None:
            raise RuntimeError("Missing installed dependency outputs: " + name)
        marker.write_text(json.dumps(dict(identity, outputs=outputs), indent=2) + "\n")


if __name__ == "__main__":
    main()
