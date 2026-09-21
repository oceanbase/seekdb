// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real C++ source adapter -> Rust parser/filesystem/owned snapshot. No SQL server.
#include "share/plugin/extension_package.h"
#include "lib/ob_errno.h"
#include <cstdlib>
#include <fstream>
#include <iostream>

#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); } } while (false)

int main(int argc, char **argv)
{
  CHECK(argc == 3);
  using namespace oceanbase::common;
  using namespace oceanbase::share::plugin;
  ExtensionPackageSource source;
  std::string error;
  // Real C++ layout -> Rust in-memory source, with no filesystem entry point.
  ExtensionPackageSource memory;
  memory.name_ = "memory_ops";
  memory.version_ = "2";
  memory.native_module_ = "org.memory";
  memory.requires_ = {"base"};
  memory.prerequisites_ = {"migration"};
  memory.scripts_ = {{"", "1", "SELECT 'a;b'; -- no newline"}, {"1", "2", ""}};
  CHECK(validate_extension_package_source(memory, error) == OB_SUCCESS && error.empty());
  for (int test = 0; test < 16; ++test) {
    auto bad = memory;
    switch (test) {
      case 0: bad.name_ = "../escape"; break;
      case 1: bad.version_ = "3"; break;
      case 2: bad.scripts_[1].from_version_ = "another"; break;
      case 3: bad.scripts_[0].sql_ = " \n"; break;
      case 4: bad.scripts_[0].sql_ = std::string(1, '\xff'); break;
      case 5: bad.scripts_[0].sql_ = std::string("x\0y", 3); break;
      case 6: bad.scripts_[0].sql_.assign(4 * 1024 * 1024, 'x'); bad.scripts_[1].sql_ = "x"; break;
      case 7: bad.requires_ = {"base", "base"}; break;
      case 8: bad.requires_ = {"memory_ops"}; break;
      case 9: bad.native_module_ = "Other.Module"; break;
      case 10: bad.schema_ = "fixed"; bad.relocatable_ = true; break;
      case 11: bad.from_version_ = "2"; break;
      case 12: bad.prerequisites_ = {"base"}; break;
      case 13: bad.prerequisites_ = {"memory_ops"}; break;
      case 14: bad.prerequisites_ = {"migration", "migration"}; break;
      case 15: bad.prerequisites_.assign(64, "migration"); break;
    }
    CHECK(validate_extension_package_source(bad, error) == OB_INVALID_ARGUMENT && !error.empty());
  }
  memory.from_version_ = "1";
  memory.scripts_.erase(memory.scripts_.begin());
  CHECK(validate_extension_package_source(memory, error) == OB_SUCCESS);
  memory.from_version_ = "2";
  memory.scripts_.clear();
  CHECK(validate_extension_package_source(memory, error) == OB_INVALID_ARGUMENT);
  memory.prerequisites_.clear();
  CHECK(validate_extension_package_source(memory, error) == OB_SUCCESS);
  memory.from_version_.clear();
  CHECK(validate_extension_package_source(memory, error) == OB_INVALID_ARGUMENT);
  const std::string root = argv[1];
  CHECK(read_extension_package(root, "native_only", "", source, error) == OB_SUCCESS);
  CHECK(source.native_install_ && source.native_module_ == "org.test" && source.version_ == "1.0" && source.scripts_.empty());
  CHECK(validate_extension_package_source(source, error) == OB_SUCCESS);
  auto native_source = source;
  native_source.native_install_ = false;
  CHECK(validate_extension_package_source(native_source, error) == OB_INVALID_ARGUMENT);
  native_source = source; native_source.native_module_.clear();
  CHECK(validate_extension_package_source(native_source, error) == OB_INVALID_ARGUMENT);
  CHECK(read_extension_package(root, "native_only", "1.1", source, error) == OB_FILE_NOT_EXIST && source.name_.empty());
  CHECK(read_extension_update(root, "native_only", "1.0", "1.1", source, error) == OB_SUCCESS);
  CHECK(source.native_install_ && source.scripts_.size() == 1 && source.scripts_[0].sql_ == "SELECT 2;");
  CHECK(validate_extension_package_source(source, error) == OB_SUCCESS);
  CHECK(read_extension_update(root, "native_only", "1.0", "1.0", source, error) == OB_SUCCESS);
  CHECK(source.native_install_ && source.scripts_.empty() && source.from_version_ == "1.0");
  CHECK(read_extension_package(root, "plain", "", source, error) == OB_SUCCESS);
  CHECK(source.name_ == "plain" && source.version_ == "1.0");
  CHECK(source.native_module_.empty() && source.schema_.empty() && source.relocatable_);
  CHECK(source.requires_ == std::vector<std::string>({"dep1", "dep2"}));
  CHECK(source.scripts_.size() == 1 && source.scripts_[0].sql_ == "SELECT 'first';\n");
  CHECK(source.scripts_[0].from_version_.empty() && source.scripts_[0].to_version_ == "1.0");
  // Returned strings outlive the Rust handle and later filesystem changes.
  std::ofstream(root + "/plain/plain--1.0.sql") << "SELECT 'changed';\n";
  CHECK(source.scripts_[0].sql_ == "SELECT 'first';\n");
  CHECK(read_extension_package(root, "plain", "2.0", source, error) == OB_SUCCESS);
  CHECK(source.version_ == "2.0" && source.scripts_[0].sql_ == "SELECT 'second';\n");
  for (const char *name : {"escape", "script_escape", "bad_utf8", "sql_nul", "empty_sql",
                          "control_utf8", "control_large", "large", "bad_control", "directory", "selfdep"}) {
    CHECK(read_extension_package(root, name, "", source, error) == OB_INVALID_ARGUMENT);
    CHECK(source.name_.empty() && source.scripts_.empty() && source.requires_.empty());
    CHECK(!error.empty());
  }
  CHECK(read_extension_package(root, "exact_limit", "", source, error) == OB_SUCCESS);
  CHECK(source.scripts_[0].sql_.size() == 4 * 1024 * 1024);
  CHECK(read_extension_package(std::string(32769, 'x'), "plain", "", source, error) == OB_INVALID_ARGUMENT);
  CHECK(source.scripts_.empty() && !error.empty());
  CHECK(read_extension_package(root, "../plain", "", source, error) == OB_INVALID_ARGUMENT);
  CHECK(read_extension_package(root, "plain", "../2.0", source, error) == OB_INVALID_ARGUMENT);
  CHECK(read_extension_package(root, "plain", "99", source, error) == OB_FILE_NOT_EXIST);
  CHECK(read_extension_package(root, "no_default", "", source, error) == OB_INVALID_ARGUMENT);
  CHECK(read_extension_package(root, "no_default", "1", source, error) == OB_SUCCESS);
  CHECK(read_extension_package(root, "chain", "", source, error) == OB_SUCCESS);
  CHECK(source.version_ == "tip" && source.scripts_.size() == 3);
  CHECK(source.scripts_[0].from_version_.empty() && source.scripts_[0].to_version_ == "1.0");
  CHECK(source.scripts_[0].sql_ == "SELECT 'base'; -- tail without newline");
  CHECK(source.scripts_[1].from_version_ == "1.0" && source.scripts_[1].to_version_ == "middle");
  CHECK(source.scripts_[1].sql_ == "SELECT 'middle';");
  CHECK(source.scripts_[2].from_version_ == "middle" && source.scripts_[2].to_version_ == "tip");
  CHECK(source.scripts_[2].sql_ == "SELECT 'tip';");
  std::ofstream(root + "/chain/chain--middle--tip.sql") << "SELECT 'changed';";
  CHECK(source.scripts_[2].sql_ == "SELECT 'tip';");
  CHECK(read_extension_package(root, "chain", "direct", source, error) == OB_SUCCESS);
  CHECK(source.scripts_.size() == 1 && source.scripts_[0].sql_ == "SELECT 'direct';");
  CHECK(read_extension_package(root, "chain_override", "", source, error) == OB_SUCCESS);
  CHECK(source.requires_ == std::vector<std::string>({"hidden_dependency"}) && source.prerequisites_.empty());
  for (const char *name : {"chain_escape", "chain_invalid", "chain_total", "secondary_default",
                          "secondary_directory", "secondary_self", "secondary_schema",
                          "secondary_module", "secondary_large", "secondary_escape", "dependency_overflow"}) {
    CHECK(read_extension_package(root, name, "", source, error) == OB_INVALID_ARGUMENT);
    CHECK(source.name_.empty() && source.scripts_.empty() && !error.empty());
  }
  CHECK(read_extension_package(root, "orphan", "", source, error) == OB_FILE_NOT_EXIST);
  CHECK(source.scripts_.empty());
  CHECK(read_extension_package(argv[2], "text_ops", "", source, error) == OB_SUCCESS);
  CHECK(source.native_module_.empty());
  CHECK(source.scripts_[0].sql_.find("CREATE FUNCTION seekdb_char_count") != std::string::npos);
  CHECK(read_extension_package(argv[2], "text_ops", "1.1", source, error) == OB_SUCCESS);
  CHECK(source.version_ == "1.1" && source.scripts_.size() == 2);
  CHECK(source.scripts_[1].sql_.find("CREATE FUNCTION seekdb_is_empty") != std::string::npos);
  CHECK(source.from_version_.empty());
  CHECK(read_extension_update(argv[2], "text_ops", "1.0", "1.1", source, error) == OB_SUCCESS);
  CHECK(source.from_version_ == "1.0" && source.version_ == "1.1" && source.scripts_.size() == 1);
  CHECK(source.scripts_[0].sql_.find("CREATE FUNCTION seekdb_char_count") == std::string::npos);
  CHECK(source.scripts_[0].sql_.find("CREATE FUNCTION seekdb_is_empty") != std::string::npos);
  CHECK(read_extension_update(root, "plain", "1.0", "2.0", source, error) == OB_FILE_NOT_EXIST);
  CHECK(source.from_version_.empty() && source.scripts_.empty() && !error.empty());
  CHECK(read_extension_update(root, "update_only", "new", "", source, error) == OB_SUCCESS);
  CHECK(source.from_version_ == "new" && source.version_ == "old" && source.scripts_.size() == 2);
  CHECK(source.scripts_[0].from_version_ == "new" && source.scripts_[0].to_version_ == "middle");
  CHECK(source.scripts_[1].from_version_ == "middle" && source.scripts_[1].to_version_ == "old");
  CHECK(source.scripts_[1].sql_ == "SELECT 'old';");
  std::ofstream(root + "/update_only/update_only--middle--old.sql") << "SELECT 'changed';";
  CHECK(source.scripts_[1].sql_ == "SELECT 'old';");
  for (const char *from : {"", "../x", "a--b"}) {
    CHECK(read_extension_update(root, "update_only", from, "old", source, error) == OB_INVALID_ARGUMENT);
    CHECK(source.name_.empty() && source.from_version_.empty() && source.scripts_.empty());
  }
  CHECK(read_extension_update(root, "noop", "v1", "", source, error) == OB_SUCCESS);
  CHECK(source.from_version_ == "v1" && source.version_ == "v1" && source.scripts_.empty());
  CHECK(read_extension_update(root, "noop_override", "v1", "", source, error) == OB_SUCCESS);
  CHECK(source.requires_ == std::vector<std::string>({"hidden"}) && source.prerequisites_.empty());
  CHECK(source.from_version_ == "v1" && source.scripts_.empty());
  CHECK(read_extension_update(root, "chain_override", "1.0", "", source, error) == OB_SUCCESS);
  CHECK(source.requires_ == std::vector<std::string>({"hidden_dependency"}) && source.prerequisites_.empty());
  for (const char *name : {"chain_escape", "chain_invalid", "update_total", "secondary_default",
                          "secondary_directory", "secondary_self", "secondary_large"}) {
    CHECK(read_extension_update(root, name, "1.0", "", source, error) == OB_INVALID_ARGUMENT);
    CHECK(source.name_.empty() && source.from_version_.empty() && source.scripts_.empty() && !error.empty());
  }
  CHECK(read_extension_update(root, "chain_empty", "1.0", "2", source, error) == OB_SUCCESS);
  CHECK(source.scripts_.size() == 1 && source.scripts_[0].sql_ == " \n");
  CHECK(read_extension_package(root, "chain_empty", "", source, error) == OB_SUCCESS);
  CHECK(source.from_version_.empty() && source.scripts_.size() == 2);
  CHECK(read_extension_update(root, "blank_update", "1.0", "", source, error) == OB_SUCCESS);
  CHECK(source.scripts_.size() == 1 && source.scripts_[0].sql_.empty());
  CHECK(source.from_version_ == "1.0" && source.version_ == "2");
  CHECK(read_extension_package(root, "versioned", "", source, error) == OB_SUCCESS);
  CHECK(source.requires_ == std::vector<std::string>({"gamma"}));
  CHECK(source.prerequisites_ == std::vector<std::string>({"alpha", "beta"}));
  CHECK(validate_extension_package_source(source, error) == OB_SUCCESS);
  const auto versioned_source = source;
  CHECK(read_extension_package(root, "dependency_limit", "", source, error) == OB_SUCCESS);
  CHECK(source.requires_.size() == 32 && source.prerequisites_.size() == 32);
  CHECK(validate_extension_package_source(source, error) == OB_SUCCESS);
  CHECK(read_extension_update(root, "dependency_overflow", "1.0", "2", source, error) == OB_SUCCESS);
  CHECK(source.requires_.size() == 33 && source.prerequisites_.empty());
  CHECK(versioned_source.prerequisites_ == std::vector<std::string>({"alpha", "beta"}));
  CHECK(read_extension_update(root, "versioned", "1.0", "tip", source, error) == OB_SUCCESS);
  CHECK(source.requires_ == std::vector<std::string>({"gamma"}));
  CHECK(source.prerequisites_ == std::vector<std::string>({"beta"}));
  CHECK(read_extension_update(root, "versioned", "middle", "tip", source, error) == OB_SUCCESS);
  CHECK(source.prerequisites_.empty());
  CHECK(read_extension_update(root, "versioned", "middle", "middle", source, error) == OB_SUCCESS);
  CHECK(source.requires_ == std::vector<std::string>({"beta"}) && source.prerequisites_.empty());
  std::ofstream(root + "/versioned/versioned--tip.control") << "requires = ''";
  CHECK(read_extension_package(root, "versioned", "", source, error) == OB_SUCCESS);
  CHECK(source.requires_.empty() && source.prerequisites_ == std::vector<std::string>({"alpha", "beta"}));
  std::ofstream(root + "/versioned/versioned--tip.control") << "# inherit primary, not middle";
  CHECK(read_extension_package(root, "versioned", "", source, error) == OB_SUCCESS);
  CHECK(source.requires_ == std::vector<std::string>({"base"}));
  CHECK(source.prerequisites_ == std::vector<std::string>({"alpha", "beta"}));
}
