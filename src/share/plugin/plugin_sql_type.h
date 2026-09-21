/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OCEANBASE_SHARE_PLUGIN_PLUGIN_SQL_TYPE_H_
#define OCEANBASE_SHARE_PLUGIN_PLUGIN_SQL_TYPE_H_

#include <cstring>
#include <limits>

#include "lib/container/ob_iarray.h"
#include "lib/string/ob_string.h"
#include "seekdb/plugin/sql_catalog.h"

namespace oceanbase
{
namespace share
{
namespace plugin
{

inline bool parse_plugin_sql_type_number(const common::ObString &value,
                                         const uint64_t maximum,
                                         uint64_t &number)
{
  bool valid = !value.empty();
  number = 0;
  for (int64_t i = 0; valid && i < value.length(); ++i) {
    const char digit = value.ptr()[i];
    valid = digit >= '0' && digit <= '9';
    if (valid) {
      const uint64_t next = static_cast<uint64_t>(digit - '0');
      valid = number <= (maximum - next) / 10;
      if (valid) number = number * 10 + next;
    }
  }
  return valid && number != 0;
}

template <typename TypeInfo>
inline bool is_plugin_sql_type(const TypeInfo &type_info)
{
  if (type_info.count() != SEEKDB_PLUGIN_SQL_TYPE_METADATA_FIELD_COUNT) return false;
  const common::ObString &marker = type_info.at(SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER_FIELD);
  const bool legacy = marker == common::ObString::make_string(SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER);
  const bool stable = marker == common::ObString::make_string(SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER_V2);
  bool is_plugin_type = legacy || stable;
  for (int64_t i = 1; is_plugin_type && i < type_info.count(); ++i) {
    is_plugin_type = !type_info.at(i).empty() &&
                     type_info.at(i).length() <=
                         SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES &&
                     nullptr == std::memchr(type_info.at(i).ptr(), '\0', type_info.at(i).length());
  }
  uint64_t number = 0;
  if (is_plugin_type) {
    const common::ObString &generation =
        type_info.at(SEEKDB_PLUGIN_SQL_TYPE_METADATA_OWNER_GENERATION_FIELD);
    is_plugin_type = stable ? generation == common::ObString::make_string("0")
                            : parse_plugin_sql_type_number(generation,
                                  std::numeric_limits<uint64_t>::max(), number);
  }
  if (is_plugin_type) {
    is_plugin_type = parse_plugin_sql_type_number(
        type_info.at(
            SEEKDB_PLUGIN_SQL_TYPE_METADATA_PHYSICAL_FORMAT_VERSION_FIELD),
        std::numeric_limits<uint32_t>::max(), number);
  }
  return is_plugin_type;
}

inline const common::ObString &plugin_sql_type_name(
    const common::ObIArray<common::ObString> &type_info)
{
  return type_info.at(SEEKDB_PLUGIN_SQL_TYPE_METADATA_SQL_NAME_FIELD);
}

template <typename TypeInfo>
inline bool decode_plugin_sql_type(
    const TypeInfo &type_info,
    seekdb_plugin_sql_binding_v1_t &binding)
{
  const bool valid = is_plugin_sql_type(type_info);
  if (valid) {
    std::memset(&binding, 0, sizeof(binding));
    binding.struct_size = sizeof(binding);
    binding.kind = SEEKDB_PLUGIN_EXTENSION_TYPE;
    binding.flags = SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT |
                    SEEKDB_PLUGIN_EXTENSION_FLAG_REQUIRES_CATALOG;
    const int fields[] = {
        SEEKDB_PLUGIN_SQL_TYPE_METADATA_SQL_NAME_FIELD,
        SEEKDB_PLUGIN_SQL_TYPE_METADATA_OBJECT_ID_FIELD,
        SEEKDB_PLUGIN_SQL_TYPE_METADATA_OWNER_PLUGIN_ID_FIELD,
        SEEKDB_PLUGIN_SQL_TYPE_METADATA_PHYSICAL_FORMAT_ID_FIELD};
    char *destinations[] = {binding.sql_name, binding.object_id,
                            binding.owner_plugin_id,
                            binding.physical_format_id};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
      const common::ObString &field = type_info.at(fields[i]);
      std::memcpy(destinations[i], field.ptr(), field.length());
    }
    uint64_t physical_format_version = 0;
    // v1 generations are validated for legacy format integrity but are not
    // current execution fences. Both formats decode to a logical identity.
    // This binding must not be used as an executable runtime binding.
    binding.owner_generation = 0;
    (void)parse_plugin_sql_type_number(
        type_info.at(
            SEEKDB_PLUGIN_SQL_TYPE_METADATA_PHYSICAL_FORMAT_VERSION_FIELD),
        std::numeric_limits<uint32_t>::max(), physical_format_version);
    binding.physical_format_version =
        static_cast<uint32_t>(physical_format_version);
  }
  return valid;
}

template <typename Left, typename Right>
inline bool same_plugin_sql_type_metadata(const Left &left, const Right &right)
{
  seekdb_plugin_sql_binding_v1_t lhs = {};
  seekdb_plugin_sql_binding_v1_t rhs = {};
  return decode_plugin_sql_type(left, lhs) && decode_plugin_sql_type(right, rhs) &&
         std::strcmp(lhs.sql_name, rhs.sql_name) == 0 &&
         std::strcmp(lhs.object_id, rhs.object_id) == 0 &&
         std::strcmp(lhs.owner_plugin_id, rhs.owner_plugin_id) == 0 &&
         std::strcmp(lhs.physical_format_id, rhs.physical_format_id) == 0 &&
         lhs.physical_format_version == rhs.physical_format_version;
}

} // namespace plugin
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_PLUGIN_PLUGIN_SQL_TYPE_H_
