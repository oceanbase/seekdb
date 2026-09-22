#!/usr/bin/env python3
"""Build the production CMake profile and load its auto-bound DSO in a real host.

The compiler context is a small controlled host interface, not the production
optimizer. The in-tree server_dev_reference separately tests that header closure.
"""
import argparse
import pathlib
import shutil
import subprocess
import tempfile


def run(command, ok=True):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True, timeout=120)
    if (result.returncode == 0) != ok:
        raise AssertionError(f"{command}\n{result.stdout}\n{result.stderr}")
    return result.stdout + result.stderr


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--loader", required=True, type=pathlib.Path)
    parser.add_argument("--cargo", required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[3]
    run([args.cargo, "build", "--offline", "--manifest-path", root / "rust/Cargo.toml",
         "-p", "seekdb-plugin-runtime", "--example", "server_dev_contract"])
    tool = root / "rust/target/debug/examples/server_dev_contract"
    with tempfile.TemporaryDirectory(prefix="seekdb-server-profile-") as temporary:
        stage = pathlib.Path(temporary)
        shutil.copytree(root / "cmake", stage / "cmake")
        shutil.copytree(root / "include/seekdb", stage / "include/seekdb")
        plugin = stage / "plugins/example"
        plugin.mkdir(parents=True)
        header = stage / "src/sql/private.h"
        header.parent.mkdir(parents=True)
        header.write_text('struct Private { int value = HOST_VALUE; };\n')
        (stage / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(ServerProfile VERSION 1.0 LANGUAGES C CXX)
add_executable(seekdb IMPORTED GLOBAL)
set_target_properties(seekdb PROPERTIES IMPORTED_LOCATION "''' + str(args.loader.resolve()) + '''")
add_library(host_context INTERFACE)
target_include_directories(host_context INTERFACE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(host_context INTERFACE HOST_VALUE=23)
target_compile_options(host_context INTERFACE -Werror)
add_library(ob_sql INTERFACE)
target_link_libraries(ob_sql INTERFACE host_context)
set(SEEKDB_SERVER_DEV_CONTRACT_TOOL "''' + str(tool) + '''")
include(cmake/Plugin.cmake)
add_subdirectory(plugins/example)
''')
        manifest = 'api_profile="server-dev"\nserver_headers=["sql/private.h"]\nexports=["probe"]\n'
        (plugin / "plugin.toml").write_text(manifest)
        cmake = 'seekdb_add_plugin(example SOURCES entry.cpp MANIFEST plugin.toml NO_INSTALL)\n'
        (plugin / "CMakeLists.txt").write_text(cmake)
        (plugin / "entry.cpp").write_text('''#include "seekdb/plugin/seekdb_plugin_abi.h"
#include "sql/private.h"
#include <cstdlib>
extern "C" SEEKDB_PLUGIN_EXPORT int probe() { return Private{}.value; }
extern "C" SEEKDB_PLUGIN_EXPORT int not_declared() { return -1; }
static int instance;
static seekdb_plugin_status_t init(const seekdb_plugin_host_api_v1_t *, seekdb_plugin_instance_handle_t **out) {
  if (probe() != 23) std::abort();
  *out = reinterpret_cast<seekdb_plugin_instance_handle_t *>(&instance);
  return SEEKDB_PLUGIN_STATUS_OK;
}
static seekdb_plugin_status_t lifecycle(seekdb_plugin_instance_handle_t *) { return SEEKDB_PLUGIN_STATUS_OK; }
static void deinit(seekdb_plugin_instance_handle_t *) {}
extern "C" SEEKDB_PLUGIN_EXPORT const seekdb_plugin_manifest_v1_t *seekdb_plugin_entry_v1() {
  static const seekdb_plugin_manifest_v1_t manifest = {
    sizeof(manifest), SEEKDB_PLUGIN_ABI_MAJOR, SEEKDB_PLUGIN_ABI_MINOR,
    "org.seekdb.sql_extension", "test", {1, 0, 0}, "sql-extension-catalog-v1",
    1, 1, 0, nullptr, 0, nullptr, 0, init, lifecycle, lifecycle, deinit, {0}};
  return &manifest;
}
''')
        build = stage / "build"
        run(["cmake", "-S", stage, "-B", build])
        run(["cmake", "--build", build, "-j2"])
        binary = build / "plugins/example/example.so"
        exports = run(["nm", "-D", "--defined-only", binary])
        assert "not_declared" not in exports and "entry_impl" not in exports, exports
        assert " probe" in exports and "seekdb_plugin_entry_v1" in exports, exports
        run([args.loader.resolve(), binary.parent, binary.name, "serverdev"])
        # The undeclared global was hidden, not accepted by relaxing the audit.
        rejected = run(["python3", root / "cmake/plugin_binary_check.py", "--binary", binary,
                        "--nm", "nm"], ok=False)
        assert "unexpected dynamic exports: probe" in rejected, rejected
        # Bind the same module sources to another actual ELF, then verify that
        # this host cannot activate it. Restore project content afterwards.
        original = (stage / "CMakeLists.txt").read_text()
        alternate = pathlib.Path(shutil.which("cmake")).resolve()
        (stage / "CMakeLists.txt").write_text(original.replace(str(args.loader.resolve()), str(alternate)))
        run(["cmake", "-S", stage, "-B", build])
        run(["cmake", "--build", build, "-j2"])
        run([args.loader.resolve(), binary.parent, binary.name, "serverdev-reject"])
        (stage / "CMakeLists.txt").write_text(original)

        negatives = [
            ('target_link_libraries(example PRIVATE ob_sql)', "not explicitly registered"),
            ('target_include_directories(example PRIVATE "${PROJECT_SOURCE_DIR}/include")', "escapes"),
            ('target_compile_options(example PRIVATE -include sql/private.h)', "COMPILE_OPTIONS"),
            ('target_sources(example PRIVATE "${PROJECT_SOURCE_DIR}/src/sql/private.h")', "source escapes"),
        ]
        for index, (mutation, message) in enumerate(negatives):
            (plugin / "CMakeLists.txt").write_text(cmake + mutation + "\n")
            output = run(["cmake", "-S", stage, "-B", stage / f"negative-{index}"], ok=False)
            assert message in output, output
        (plugin / "CMakeLists.txt").write_text(cmake)
        (plugin / "plugin.toml").write_text(manifest.replace('"server-dev"', '"public"'))
        output = run(["cmake", "-S", stage, "-B", stage / "negative-public"], ok=False)
        assert "require server-dev" in output, output
    print("Server-dev CMake compilation, export isolation, host binding and late-boundary checks passed")


if __name__ == "__main__":
    main()
