#/usr/bin/env python
# -*- coding: utf-8 -*-
"""
syspack_codegen.py
~~~~~~~~~~~~~~~~~~
This script is the configuration center for system packages, with the following functions:
1. Calls the wrap program to convert specified system package source code files from plaintext to ciphertext, with the ciphertext file extension being `.plw`
2. Embeds the content of specified system package source code files as strings into `syspack_source.cpp`, which is subsequently compiled and linked into observer
3. Copies the specified system package to the `syspack_release` folder in the compilation directory for subsequent RPM packaging and deployment
NOTE:
1. This script will be automatically called during the compilation phase (make stage) and complete the above three steps, generally no manual invocation is required. It can also be manually invoked using `python2 syspack_codegen.py [option]`.
2. RD only needs to modify the `syspack_config` variable, which is a list where each element is a `SysPackConfig` object containing all information about a system package.
    The current format of the `SysPackConfig` object constructor parameters is:
        `SysPackConfig(group, name, header_file, body_file, wrap_type, orc_build_req)`
    Where:
        - group: The grouping of the system package, optional values are `SysPackGroup` enumeration type
        - name: The name of the system package, used to identify the system package in `syspack_source.cpp`
        - header_file: The header file of the system package, must be a `.sql` file
        - body_file: The implementation file of the system package, optional (can be filled with None), must be a `.sql` file
        - wrap_type: The obfuscation method of the system package, optional values are `WrapType` enumeration type, controlling whether the header and body of the system package need to be encrypted and obfuscated, default is `WrapType.NONE`, i.e., no obfuscation
        - orc_build_req: Whether the system package needs to be compiled in the `OB_BUILD_ORACLE_PL` environment, default is False
"""

import os
import sys
import subprocess
import argparse
import shutil
import codecs

g_script_dir = os.path.dirname(os.path.abspath(__file__))
g_verbose = False
g_orc_build = False

class SysPackGroup:
    ORACLE         = "oracle"
    MYSQL          = "mysql"
    ORACLE_SPECIAL = "oracle_special"
    MYSQL_SPECIAL  = "mysql_special"

class WrapType:
    NONE        = 1
    HEADER_ONLY = 2
    BODY_ONLY   = 3
    BOTH        = 4

class SysPackConfig:
    def __init__(self, group, name, header_file, body_file, **kwargs):
        assert group in [SysPackGroup.ORACLE, SysPackGroup.MYSQL, SysPackGroup.ORACLE_SPECIAL, SysPackGroup.MYSQL_SPECIAL]
        self.group = group
        assert isinstance(name, str)
        self.name = name
        assert isinstance(header_file, str) and header_file.endswith(".sql")
        self.header_file = header_file
        self.body_file = None
        if body_file:
            assert isinstance(body_file, str) and body_file.endswith(".sql")
            self.body_file = body_file

        self.wrap = WrapType.NONE
        self.orc_build_req = False
        for key, value in kwargs.items():
            if key == "wrap":
                assert value in [WrapType.NONE, WrapType.HEADER_ONLY, WrapType.BODY_ONLY, WrapType.BOTH]
                self.wrap = value
            elif key == "orc_build_req":
                assert isinstance(value, bool)
                self.orc_build_req = value
            else:
                assert False, "Unknown key: {}".format(key)

        if body_file is None:
            assert self.wrap != WrapType.BODY_ONLY, "WrapType.BODY_ONLY is not allowed when body_file is None"
            assert self.wrap != WrapType.BOTH, "WrapType.BOTH is not allowed when body_file is None"

        self.release_header = self.header_file
        self.release_body = self.body_file
        if self.wrap == WrapType.HEADER_ONLY or self.wrap == WrapType.BOTH:
            self.release_header = self.header_file[:-3] + "plw"
        if self.body_file and (self.wrap == WrapType.BODY_ONLY or self.wrap == WrapType.BOTH):
            self.release_body = self.body_file[:-3] + "plw"

    def export_release_file_list(self):
        return [self.release_header] + ([self.release_body] if self.release_body else [])

    def export_source_file_config(self):
        return "{{\"{}\", \"{}\", {}}},".format(
            self.name,
            self.release_header,
            "\"{}\"".format(self.release_body) if self.release_body else "nullptr")

    def export_files_to_wrap(self):
        if self.wrap == WrapType.NONE:
            return []
        elif self.wrap == WrapType.HEADER_ONLY:
            return [self.header_file]
        elif self.wrap == WrapType.BODY_ONLY:
            return [self.body_file]
        elif self.wrap == WrapType.BOTH:
            return [self.header_file, self.body_file]

def install_syspack_files(syspack_config, release_dir):
    file_to_install = []
    for config in syspack_config:
        if not config.orc_build_req or g_orc_build:
            file_to_install += config.export_release_file_list()
    if g_verbose:
        print("installing syspack files to {}".format(release_dir))
        print("{} files to install: {}".format(len(file_to_install), file_to_install))
    for file in file_to_install:
        src_file = os.path.join(g_script_dir, file)
        dst_file = os.path.join(release_dir, file)
        shutil.copyfile(src_file, dst_file)

def wrap_syspack(syspack_config, wrap_bin_path):
    wrapped_files = []
    for config in syspack_config:
        wrapped_files += config.export_files_to_wrap()
    for file in wrapped_files:
        if g_verbose:
            print("wrap {} -o {}".format(file, file[:-3] + "plw"))
        subprocess.check_call([wrap_bin_path, file, "-o", file[:-3] + "plw"], cwd=g_script_dir)

def embed_syspack(syspack_config):
    def gen_syspack_file_list(syspack_config, group):
        orc_req_lst = []
        lst = []
        for config in syspack_config:
            if config.group == group:
                if config.orc_build_req:
                    orc_req_lst.append(config.export_source_file_config())
                else:
                    lst.append(config.export_source_file_config())
        return  "int {}_syspack_file_list_length = {};\n".format(group, len(lst)) \
                + "ObSysPackageFile {}_syspack_file_list[] = {{\n".format(group) \
                + "\n".join(lst) + "\n"\
                + "};\n\n"

    syspack_source_file = os.path.join(g_script_dir, "syspack_source.cpp")
    with codecs.open(syspack_source_file, "w", encoding="utf8") as f:
        # 1. write header
        f.write("// This file is generated by `syspack_codegen.py`, do not edit it!\n\n"
                "#include <cstdint>\n"
                "#include <utility>\n\n"
                "namespace oceanbase {\n"
                "namespace pl {\n\n"
                "struct ObSysPackageFile {\n"
                "  const char *const package_name;\n"
                "  const char *const package_spec_file_name;\n"
                "  const char *const package_body_file_name;\n"
                "};\n\n")
        # 2. write `xxx_syspack_file_list`
        f.write(gen_syspack_file_list(syspack_config, SysPackGroup.ORACLE))
        f.write(gen_syspack_file_list(syspack_config, SysPackGroup.MYSQL))
        f.write(gen_syspack_file_list(syspack_config, SysPackGroup.ORACLE_SPECIAL))
        f.write(gen_syspack_file_list(syspack_config, SysPackGroup.MYSQL_SPECIAL))
        # 3. write `syspack_source_count`
        release_files = []
        for config in syspack_config:
            release_files += config.export_release_file_list()
        f.write("int64_t syspack_source_count = {};\n".format(len(release_files)))
        # 4. write `syspack_source_contents`
        f.write("std::pair<const char * const, const char* const> syspack_source_contents[] = {\n")
        for file in release_files:
            # 4.1 write file name
            f.write("{{\n"
                    "\"{}\",\n"
                    "R\"sys_pack_del(".format(file))
            # 4.2 write file content
            file_path = os.path.join(g_script_dir, file)
            if os.path.exists(file_path):
                try:
                    with codecs.open(file_path, "r", encoding="utf8") as file_content:
                        f.write(file_content.read())
                except Exception as e:
                    sys.stderr.write("[wrap] error! failed to embed file: {}\n".format(file_path))
                    sys.stderr.write("[wrap] error! " + str(e) + "\n")
                    sys.exit(1)
            else:
                pass # file not found
            # 4.3 write file end
            f.write(")sys_pack_del\"\n"
                    "},\n")
        f.write("};\n\n")
        # 5. write tail
        f.write("} // namespace pl\n"
                "} // namespace oceanbase\n")

syspack_config = [
    # MySQL
    SysPackConfig(SysPackGroup.MYSQL, "dbms_stats", "dbms_stats_mysql.sql", "dbms_stats_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_scheduler", "dbms_scheduler_mysql.sql", "dbms_scheduler_mysql_body.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_ischeduler", "dbms_ischeduler_mysql.sql", "dbms_ischeduler_mysql_body.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_application", "dbms_application_mysql.sql", "dbms_application_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_session", "dbms_session_mysql.sql", "dbms_session_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_monitor", "dbms_monitor_mysql.sql", "dbms_monitor_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_xplan", "dbms_xplan_mysql.sql", "dbms_xplan_mysql_body.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_udr", "dbms_udr_mysql.sql", "dbms_udr_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_workload_repository", "dbms_workload_repository_mysql.sql", "dbms_workload_repository_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_mview", "dbms_mview_mysql.sql", "dbms_mview_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_mview_stats", "dbms_mview_stats_mysql.sql", "dbms_mview_stats_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_trusted_certificate_manager", "dbms_trusted_certificate_manager_mysql.sql", "dbms_trusted_certificate_manager_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_ob_limit_calculator", "dbms_ob_limit_calculator_mysql.sql", "dbms_ob_limit_calculator_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_external_table", "dbms_external_table_mysql.sql", "dbms_external_table_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "external_table_alert_log", "external_table_alert_log.sql", None),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_vector", "dbms_vector_mysql.sql", "dbms_vector_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_hybrid_search", "dbms_hybrid_vector_mysql.sql", "dbms_hybrid_vector_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_space", "dbms_space_mysql.sql", "dbms_space_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_partition", "dbms_partition_mysql.sql", "dbms_partition_body_mysql.sql"),
    SysPackConfig(SysPackGroup.MYSQL, "dbms_ai_service", "dbms_ai_service_mysql.sql", "dbms_ai_service_body_mysql.sql"),
    # MySQL Special
    SysPackConfig(SysPackGroup.MYSQL_SPECIAL, "__dbms_upgrade", "__dbms_upgrade_mysql.sql", "__dbms_upgrade_body_mysql.sql"),
]

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="System Package Config Center")
    parser.add_argument("-wp", "--wrap-bin-path", type=str, default=None,
                        help="The path to the wrap binary. If empty, it indicates that OB_BUILD_ORACLE_PL is not set.")
    parser.add_argument("-rd", "--release-dir", type=str, default=None,
                        help="The path to the system package release directory. If empty, installation is not required.")
    parser.add_argument("-v", "--verbose", action="store_true", help="Print verbose information")
    args = parser.parse_args()

    wrap_bin_path = args.wrap_bin_path
    release_dir = args.release_dir
    g_orc_build = wrap_bin_path is not None
    g_verbose = args.verbose

    # 1. wrap syspack files
    if wrap_bin_path:
        assert os.path.exists(wrap_bin_path), "{} not exists".format(wrap_bin_path)
        wrap_syspack(syspack_config, wrap_bin_path)
    # 2. embed syspack files
    embed_syspack(syspack_config)
    # 3. install syspack files
    if release_dir:
        if not os.path.exists(release_dir):
            os.makedirs(release_dir)
        install_syspack_files(syspack_config, release_dir)
