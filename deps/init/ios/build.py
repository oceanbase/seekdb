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

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_PACKAGES = ("zlib", "openssl", "curl", "lzma", "libxml2", "protobuf-c", "icu")
ICU_SOURCE = json.loads((ROOT / "bazel/sdk_sources/dependencies.json").read_text())["sources"]["icu"]

PACKAGES = {
    "lzma": ("5.4.7", "https://codeload.github.com/tukaani-project/xz/tar.gz/refs/tags/v5.4.7",
             "87ef1bb0183ce477ba3ddb2700000b4c1e723f8d984e8f678dddcb11c5bf4f99"),
    "icu": tuple(ICU_SOURCE[key] for key in ("version", "url", "sha256")),
    "libxml2": ('2.10.4', 'https://codeload.github.com/GNOME/libxml2/tar.gz/refs/tags/v2.10.4',
        '6f6fb27f91bb65f9d7196e3c616901b3e18a7dea31ccc2ae857940b125faa780'),
    "protobuf-c": ('1.4.1', 'https://codeload.github.com/protobuf-c/protobuf-c/tar.gz/refs/tags/v1.4.1',
        '99be336cdb15dfc5827efe34e5ac9aaa962e2485db547dd254d2a122a7d23102'),

    "curl": ("8.12.1", "https://curl.se/download/curl-8.12.1.tar.gz",
             "7b40ea64947e0b440716a4d7f0b7aa56230a5341c8377d7b609649d4aea8dbcf"),
    "zlib": ("1.2.13", "https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.2.13",
             "1525952a0a567581792613a9723333d7f8cc20b87a81f920fb8bc7e3f2251428"),
    "openssl": ("1.1.1u", "https://codeload.github.com/openssl/openssl/tar.gz/refs/tags/OpenSSL_1_1_1u",
                "fafe27202bde4238dce258d82ec8a8592a657842e5431264620a933a0c9436b7"),
}


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
        if source.exists():
            shutil.rmtree(source)
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
                                     env=environment, universal_newlines=True).strip()
    if arches != "arm64":
        raise RuntimeError("Unexpected archive architectures: " + arches)
    output = subprocess.check_output(["xcrun", "otool", "-l", str(archive)],
                                     env=environment, universal_newlines=True)
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
    environment = target_environment(sdk, options, environment)
    run([source / "configure", "--static", "--prefix=" + str(prefix)], build, environment)
    run(["make", "-j" + str(options.jobs), "libz.a"], build, environment)
    verify_archive(build / "libz.a", options.simulator, environment)
    run(["make", "install"], build, environment)


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
    environment = target_environment(sdk, options, environment, prefix)
    run([source / "configure", "--host=aarch64-apple-darwin", "--disable-shared",
         "--enable-static", "--disable-dependency-tracking", "--disable-ldap",
         "--disable-ldaps", "--without-libpsl", "--without-libssh2", "--without-libssh",
         "--without-wolfssh", "--without-nghttp2", "--without-nghttp3", "--without-ngtcp2",
         "--without-zstd", "--without-brotli", "--without-libidn2", "--without-librtmp",
         "--without-gssapi", "--without-apple-idn", "--without-secure-transport",
         "--without-quiche", "--without-msh3", "--without-libuv", "--without-libgsasl",
         "--with-openssl=" + str(prefix), "--with-zlib=" + str(prefix),
         "--prefix=" + str(prefix)], build, environment)
    run(["make", "-C", "lib", "-j" + str(options.jobs)], build, environment)
    verify_archive(build / "lib/.libs/libcurl.a", options.simulator, environment)
    run(["make", "-C", "lib", "install"], build, environment)
    run(["make", "-C", "include", "install"], build, environment)


def target_environment(sdk, options, environment, prefix=None):
    result = dict(environment)
    sdk_name = "iphonesimulator" if options.simulator else "iphoneos"
    triple = "arm64-apple-ios" + options.deployment_target
    if options.simulator:
        triple += "-simulator"
    for variable, tool in (("CC", "clang"), ("CXX", "clang++"), ("AR", "ar"), ("RANLIB", "ranlib")):
        result[variable] = subprocess.check_output(
            ["xcrun", "--sdk", sdk_name, "--find", tool], env=environment, universal_newlines=True).strip()
    flags = "-O2 -fPIC -target " + triple + " -isysroot " + shlex.quote(sdk)
    result["CFLAGS"] = flags
    result["CXXFLAGS"] = flags
    result["LDFLAGS"] = "-target " + triple + " -isysroot " + shlex.quote(sdk)
    result["PKG_CONFIG"] = "pkg-config"
    result["PKG_CONFIG_LIBDIR"] = str(prefix / "lib/pkgconfig") if prefix else ""
    result["PKG_CONFIG_PATH"] = ""
    if prefix:
        result["CPPFLAGS"] = "-I" + shlex.quote(str(prefix / "include"))
        result["LDFLAGS"] += " -L" + shlex.quote(str(prefix / "lib"))
    return result


def build_lzma(source, build, prefix, sdk, options, environment):
    if not (source / "configure").exists():
        run(["sh", "autogen.sh", "--no-po4a", "--no-doxygen"], source, environment)
    environment = target_environment(sdk, options, environment)
    run([source / "configure", "--host=aarch64-apple-darwin", "--enable-static",
         "--disable-shared", "--disable-dependency-tracking", "--disable-nls",
         "--disable-xz", "--disable-xzdec", "--disable-lzmadec", "--disable-lzmainfo",
         "--disable-lzma-links", "--disable-scripts", "--with-pic",
         "--prefix=" + str(prefix)], build, environment)
    run(["make", "-C", "src/liblzma", "-j" + str(options.jobs)], build, environment)
    verify_archive(build / "src/liblzma/.libs/liblzma.a", options.simulator, environment)
    run(["make", "-C", "src/liblzma", "install"], build, environment)


def build_libxml2(source, build, prefix, sdk, options, environment):
    if not (source / "configure").exists():
        run(["autoreconf", "--install", "--force"], source, environment)
    for library in ("libz.a", "liblzma.a"):
        verify_archive(prefix / "lib" / library, options.simulator, environment)
    environment = target_environment(sdk, options, environment, prefix)
    run([source / "configure", "--host=aarch64-apple-darwin", "--enable-static",
         "--disable-shared", "--disable-dependency-tracking", "--without-python",
         "--without-readline", "--with-zlib=" + str(prefix), "--with-lzma=" + str(prefix),
         "--prefix=" + str(prefix)], build, environment)
    run(["make", "-j" + str(options.jobs), "libxml2.la"], build, environment)
    verify_archive(build / ".libs/libxml2.a", options.simulator, environment)
    run(["make", "install-libLTLIBRARIES"], build, environment)
    run(["make", "-C", "include", "install"], build, environment)


def build_protobuf_c(source, build, prefix, sdk, options, environment):
    """Compile the C runtime without running host protocol generators."""
    implementation = source / "protobuf-c/protobuf-c.c"
    headers = [source / "protobuf-c/protobuf-c.h"]
    destination = prefix / "lib/libprotobuf-c.a"
    header_directory = prefix / "include/protobuf-c"
    triple = "arm64-apple-ios" + options.deployment_target
    if options.simulator:
        triple += "-simulator"
    obj = build / "protobuf-c.o"
    archive = build / destination.name
    run(["xcrun", "clang", "-target", triple, "-isysroot", sdk, "-O2", "-fPIC",
         "-I" + str(source), "-c", implementation, "-o", obj], ROOT, environment)
    run(["xcrun", "ar", "rcs", archive, obj], ROOT, environment)
    verify_archive(archive, options.simulator, environment)
    destination.parent.mkdir(parents=True, exist_ok=True)
    header_directory.mkdir(parents=True, exist_ok=True)
    shutil.copy2(archive, destination)
    for header in headers:
        shutil.copy2(header, header_directory / header.name)


def build_icu(source, build, prefix, sdk, options, environment):
    """Build native ICU generators first, then cross-compile the target libraries."""
    source = source / "source"
    host_build = ROOT / "deps/ios/host/icu"
    host_build.mkdir(parents=True, exist_ok=True)
    host_env = dict(environment)
    host_env.pop("IPHONEOS_DEPLOYMENT_TARGET", None)
    host_env["SDKROOT"] = subprocess.check_output(
        ["xcrun", "--sdk", "macosx", "--show-sdk-path"], env=host_env, universal_newlines=True).strip()
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
    "zlib": ("lib/libz.a", "include/zlib.h", "include/zconf.h"),
    "openssl": ("lib/libssl.a", "lib/libcrypto.a", "include/openssl/ssl.h"),
    "curl": ("lib/libcurl.a", "include/curl/curl.h"),
    "lzma": ("lib/liblzma.a", "include/lzma.h"),
    "libxml2": ("lib/libxml2.a", "include/libxml2/libxml/parser.h",
                "include/libxml2/libxml/xmlversion.h"),
    "protobuf-c": ("lib/libprotobuf-c.a", "include/protobuf-c/protobuf-c.h"),
    "icu": ("lib/libicuuc.a", "lib/libicui18n.a", "lib/libicudata.a", "include/unicode/utypes.h"),
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


def publish_metadata(name, source, marker, prefix):
    notices = set()
    for pattern in ("LICENSE*", "COPYING*", "Copyright", "license.html"):
        notices.update(path for path in source.glob(pattern) if path.is_file())
    if name == "zlib":
        notices.add(source / "zlib.h")
    if not notices:
        raise RuntimeError("No upstream license notice found: " + name)
    destination = prefix / "licenses" / name
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    for notice in sorted(notices):
        shutil.copy2(notice, destination / notice.name)
    records = prefix / "verified"
    records.mkdir(parents=True, exist_ok=True)
    shutil.copy2(marker, records / (name + ".json"))


def builder_identity():
    """Bind cache reuse to tracked dependency recipes and package version definitions."""
    digest = hashlib.sha256()
    digest.update(Path(__file__).read_bytes())
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
    unknown = set(options.packages) - set(PACKAGES)
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
                                  env=environment, universal_newlines=True).strip()
    environment["SDKROOT"] = sdk
    environment["IPHONEOS_DEPLOYMENT_TARGET"] = options.deployment_target
    prefix = ROOT / "deps/ios" / sdk_name / "devel"
    for directory in (prefix / "lib", prefix / "include"):
        directory.mkdir(parents=True, exist_ok=True)
    recipe = builder_identity()
    rebuilt = False
    for name in options.packages or DEFAULT_PACKAGES:
        build = ROOT / "deps/ios" / sdk_name / "build" / name
        identity = {
            "package": name, "version": PACKAGES[name][0], "sha256": PACKAGES[name][2],
            "sdk": sdk, "deployment_target": options.deployment_target,
            "platform": sdk_name, "architecture": "arm64", "builder": recipe}
        marker = build / "verified.json"
        if options.reuse and not rebuilt and reusable_package(marker, identity, prefix):
            print("Reusing verified iOS dependency: " + name, flush=True)
            publish_metadata(name, source_package(name, environment), marker, prefix)
            continue
        # Rebuild downstream packages when an earlier dependency has changed.
        rebuilt = True
        if build.exists():
            shutil.rmtree(build)
        source = source_package(name, environment)
        build.mkdir(parents=True, exist_ok=True)
        {"zlib": build_zlib, "openssl": build_openssl, "curl": build_curl,
         "lzma": build_lzma, "libxml2": build_libxml2, "icu": build_icu,
         "protobuf-c": build_protobuf_c}[name](source, build, prefix, sdk, options, environment)
        outputs = installed_output_hashes(name, prefix)
        if outputs is None:
            raise RuntimeError("Missing installed dependency outputs: " + name)
        marker.write_text(json.dumps(dict(identity, outputs=outputs), indent=2) + "\n")
        publish_metadata(name, source, marker, prefix)


if __name__ == "__main__":
    main()
