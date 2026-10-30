/*
 * Copyright (c) 2025 OceanBase.
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

#pragma once
#include "common/datum/datum_value.h"
#include "lib/utility/ob_unify_serialize.h"
namespace oceanbase { namespace storage {
using common::SqlTypeMeta;
using common::ObIAllocator;
struct ObStorageColumnSchema
{
public:
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int serialize_(char *buf, const int64_t buf_len, int64_t &pos) const;
  int64_t get_serialize_size() const;
  int64_t get_serialize_size_() const;
  // The record never borrows the input wire. Every decoded payload is owned
  // by this explicit allocator, including inline-sized scalars and arrays.
  int deserialize(ObIAllocator &allocator, const char *buf, const int64_t data_len, int64_t &pos);
private:
  static const int64_t UNIS_VERSION = 1;
  int decode_fields(ObIAllocator &allocator, const char *buf, const int64_t data_len, int64_t &pos);
public:
  ObStorageColumnSchema();
  ~ObStorageColumnSchema();

  // Only reset empty or transferred descriptors; destroy(allocator) releases ownership.
  void reset();
  void clear_default_value(ObIAllocator &allocator);
  void destroy(ObIAllocator &allocator);
  bool is_valid() const;
  inline common::SqlValueType get_data_type() const { return meta_type_.get_type(); }
  inline bool is_generated_column() const { return is_generated_column_; }
  inline bool is_column_stored_in_sstable() const { return is_column_stored_in_sstable_;}
  inline bool is_rowkey_column() const { return is_rowkey_column_; }
  inline const common::DatumValue &get_orig_default_value()  const { return orig_default_value_; }
  int deep_copy_default_val(ObIAllocator &allocator, const common::DatumValue &default_val);

  TO_STRING_KV(K_(meta_type), K_(is_column_stored_in_sstable), K_(is_rowkey_column),
      K_(is_generated_column), K_(orig_default_value));

private:
  static const int32_t SCS_ONE_BIT = 1;
  static const int32_t SCS_RESERVED_BITS = 29;

public:
  union {
    uint32_t info_;
    struct {
      uint32_t is_column_stored_in_sstable_     :SCS_ONE_BIT;
      uint32_t is_rowkey_column_                :SCS_ONE_BIT;
      uint32_t is_generated_column_             :SCS_ONE_BIT;
      uint32_t reserved_                        :SCS_RESERVED_BITS;
    };
  };
  int64_t default_checksum_;
  SqlTypeMeta meta_type_;
  common::DatumValue orig_default_value_;
};

} }
