// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "share/plugin/plugin_sql_type.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace oceanbase::share::plugin;
using oceanbase::common::ObString;
#define CHECK(expr) do { if (!(expr)) { \
  std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); \
} } while (false)

// The production helpers operate on read-only count()/at() collections. This
// adapter uses the actual ObString and avoids a host allocator just for tests.
struct TypeInfo {
  std::vector<std::string> fields = {SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER,
      "payload", "test.type.payload", "test.plugin", "7", "test.format", "1"};
  int64_t count() const { return fields.size(); }
  ObString at(int64_t index) const {
    const auto &value = fields.at(index);
    return ObString(static_cast<int32_t>(value.size()), value.data());
  }
};

int main()
{
  TypeInfo old;
  CHECK(is_plugin_sql_type(old));
  seekdb_plugin_sql_binding_v1_t identity = {};
  CHECK(decode_plugin_sql_type(old, identity));
  CHECK(identity.owner_generation == 0 && identity.physical_format_version == 1);
  CHECK(std::string(identity.object_id) == "test.type.payload");
  TypeInfo newer = old;
  newer.fields[4] = "18446744073709551615";
  CHECK(is_plugin_sql_type(newer));
  CHECK(same_plugin_sql_type_metadata(old, newer));
  TypeInfo stable = old;
  stable.fields[0] = SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER_V2;
  stable.fields[4] = "0";
  CHECK(is_plugin_sql_type(stable));
  CHECK(decode_plugin_sql_type(stable, identity));
  CHECK(identity.owner_generation == 0);
  CHECK(same_plugin_sql_type_metadata(old, stable));
  for (const char *invalid : {"", "00", "1", "-1", "18446744073709551616"}) {
    TypeInfo bad = stable;
    bad.fields[4] = invalid;
    CHECK(!is_plugin_sql_type(bad));
  }
  for (const char *invalid : {"", "0", "-1", "18446744073709551616"}) {
    TypeInfo bad = old;
    bad.fields[4] = invalid;
    CHECK(!is_plugin_sql_type(bad));
  }
  for (const char *invalid : {"0", "-1", "4294967296", "1x"}) {
    TypeInfo bad = stable;
    bad.fields[6] = invalid;
    CHECK(!is_plugin_sql_type(bad));
  }
  for (size_t index : {1u, 2u, 3u, 5u, 6u}) {
    TypeInfo different = stable;
    different.fields[index] = index == 6 ? "2" : "different";
    CHECK(!same_plugin_sql_type_metadata(old, different));
  }
  TypeInfo malformed = stable;
  malformed.fields[2] = std::string("type\0suffix", 11);
  CHECK(!is_plugin_sql_type(malformed));
  malformed = stable;
  malformed.fields[2] = std::string(SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1, 'a');
  CHECK(!is_plugin_sql_type(malformed));
  malformed = stable;
  malformed.fields[0] = "seekdb.plugin.type:v3";
  CHECK(!is_plugin_sql_type(malformed));
  malformed.fields.clear();
  CHECK(!is_plugin_sql_type(malformed));
  CHECK(!same_plugin_sql_type_metadata(stable, malformed));
  return 0;
}
