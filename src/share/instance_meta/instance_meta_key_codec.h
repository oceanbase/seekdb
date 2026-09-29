/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef OCEANBASE_SHARE_INSTANCE_META_KEY_CODEC_H_
#define OCEANBASE_SHARE_INSTANCE_META_KEY_CODEC_H_

#include <cstdint>
#include <string>
#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"
#include "share/instance_meta/instance_meta_collection.h"

namespace oceanbase
{
namespace share
{
namespace instance_meta
{

enum class KeyFieldType { UINT64_BE, UTF8 };

struct KeyFieldDesc
{
  const char *name;
  KeyFieldType type;
};

struct CollectionKeyDesc
{
  const char *name;
  const KeyFieldDesc *fields;
  int64_t field_count;
};

// The typed metadata accessors and the diagnostic view use the same persistent
// key contract. InstanceMetaStore remains unaware of these field meanings.
class InstanceMetaKeyCodec final
{
public:
  static const CollectionKeyDesc *describe(MetaCollection collection)
  {
    static const KeyFieldDesc namespace_id[] = {{"namespace_id", KeyFieldType::UINT64_BE}};
    static const KeyFieldDesc namespace_name[] = {{"name", KeyFieldType::UTF8}};
    static const KeyFieldDesc snapshot_id[] = {{"snapshot_id", KeyFieldType::UINT64_BE}};
    static const KeyFieldDesc exception[] = {
        {"namespace_id", KeyFieldType::UINT64_BE}, {"tablet_id", KeyFieldType::UINT64_BE}};
    static const KeyFieldDesc page_id[] = {{"page_id", KeyFieldType::UINT64_BE}};
    static const KeyFieldDesc record_id[] = {{"record_id", KeyFieldType::UINT64_BE}};
    static const CollectionKeyDesc descriptions[] = {
        {"NAMESPACES", namespace_id, 1},
        {"NAMESPACE_NAMES", namespace_name, 1},
        {"SNAPSHOTS", snapshot_id, 1},
        {"EXCEPTIONS", exception, 2},
        {"PAGES", page_id, 1},
        {"COUNTERS", record_id, 1},
        {"SNAPSHOT_COORDINATION", record_id, 1},
        {"SNAPSHOT_PINS", snapshot_id, 1},
    };
    const uint64_t id = static_cast<uint64_t>(collection);
    return id >= 1 && id <= sizeof(descriptions) / sizeof(descriptions[0])
        ? &descriptions[id - 1] : nullptr;
  }

  static int encode_u64(MetaCollection collection, uint64_t id, std::string &key)
  {
    const CollectionKeyDesc *desc = describe(collection);
    if (desc == nullptr || desc->field_count != 1
        || desc->fields[0].type != KeyFieldType::UINT64_BE) {
      return common::OB_INVALID_ARGUMENT;
    }
    key.clear();
    append_u64(key, id);
    return common::OB_SUCCESS;
  }

  static int encode_pair(MetaCollection collection, uint64_t first, uint64_t second,
                         std::string &key)
  {
    const CollectionKeyDesc *desc = describe(collection);
    if (desc == nullptr || desc->field_count != 2
        || desc->fields[0].type != KeyFieldType::UINT64_BE
        || desc->fields[1].type != KeyFieldType::UINT64_BE) {
      return common::OB_INVALID_ARGUMENT;
    }
    key.clear();
    append_u64(key, first);
    append_u64(key, second);
    return common::OB_SUCCESS;
  }

  static int encode_first_u64(MetaCollection collection, uint64_t first, std::string &prefix)
  {
    const CollectionKeyDesc *desc = describe(collection);
    if (desc == nullptr || desc->field_count < 1
        || desc->fields[0].type != KeyFieldType::UINT64_BE) {
      return common::OB_INVALID_ARGUMENT;
    }
    prefix.clear();
    append_u64(prefix, first);
    return common::OB_SUCCESS;
  }

  static int encode_name(MetaCollection collection, const std::string &name, std::string &key)
  {
    const CollectionKeyDesc *desc = describe(collection);
    if (desc == nullptr || desc->field_count != 1
        || desc->fields[0].type != KeyFieldType::UTF8) {
      return common::OB_INVALID_ARGUMENT;
    }
    key = name;
    return common::OB_SUCCESS;
  }

  static int decode_json(MetaCollection collection, const common::ObString &key,
                         std::string &json)
  {
    const CollectionKeyDesc *desc = describe(collection);
    if (desc == nullptr) { return common::OB_INVALID_ARGUMENT; }
    int64_t pos = 0;
    json = "{";
    for (int64_t i = 0; i < desc->field_count; ++i) {
      if (i != 0) { json += ','; }
      json += '"';
      json += desc->fields[i].name;
      json += "\":\"";
      if (desc->fields[i].type == KeyFieldType::UINT64_BE) {
        if (key.length() - pos < 8) { return common::OB_CHECKSUM_ERROR; }
        uint64_t value = 0;
        for (int j = 0; j < 8; ++j) {
          value = (value << 8) | static_cast<unsigned char>(key.ptr()[pos++]);
        }
        json += std::to_string(value);
      } else if (desc->fields[i].type == KeyFieldType::UTF8) {
        append_json_string(key.ptr() + pos, key.length() - pos, json);
        pos = key.length();
      }
      json += '"';
    }
    if (pos != key.length()) { return common::OB_CHECKSUM_ERROR; }
    json += '}';
    return common::OB_SUCCESS;
  }

  static int decode_u64(MetaCollection collection, const common::ObString &key, uint64_t &value)
  {
    const CollectionKeyDesc *desc = describe(collection);
    if (desc == nullptr || desc->field_count != 1
        || desc->fields[0].type != KeyFieldType::UINT64_BE || key.length() != 8) {
      return common::OB_CHECKSUM_ERROR;
    }
    value = 0;
    for (int i = 0; i < 8; ++i) {
      value = (value << 8) | static_cast<unsigned char>(key.ptr()[i]);
    }
    return common::OB_SUCCESS;
  }

  static int decode_pair(MetaCollection collection, const common::ObString &key,
                         uint64_t &first, uint64_t &second)
  {
    const CollectionKeyDesc *desc = describe(collection);
    if (desc == nullptr || desc->field_count != 2
        || desc->fields[0].type != KeyFieldType::UINT64_BE
        || desc->fields[1].type != KeyFieldType::UINT64_BE || key.length() != 16) {
      return common::OB_CHECKSUM_ERROR;
    }
    first = second = 0;
    for (int i = 0; i < 8; ++i) {
      first = (first << 8) | static_cast<unsigned char>(key.ptr()[i]);
      second = (second << 8) | static_cast<unsigned char>(key.ptr()[i + 8]);
    }
    return common::OB_SUCCESS;
  }

private:
  static void append_u64(std::string &out, uint64_t value)
  {
    for (int i = 7; i >= 0; --i) {
      out.push_back(static_cast<char>(value >> (i * 8)));
    }
  }

  static void append_json_string(const char *data, int64_t length, std::string &out)
  {
    static const char hex[] = "0123456789abcdef";
    for (int64_t i = 0; i < length; ++i) {
      const unsigned char c = static_cast<unsigned char>(data[i]);
      if (c == '"' || c == '\\') {
        out += '\\';
        out += static_cast<char>(c);
      } else if (c < 0x20) {
        out += "\\u00";
        out += hex[c >> 4];
        out += hex[c & 0xf];
      } else {
        out += static_cast<char>(c);
      }
    }
  }
};

} // namespace instance_meta
} // namespace share
} // namespace oceanbase
#endif
