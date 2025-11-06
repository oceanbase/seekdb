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

#ifndef OCEANBASE_TABLE_REDIS_OB_REDIS_RKEY_H
#define OCEANBASE_TABLE_REDIS_OB_REDIS_RKEY_H

#include "share/table/redis/ob_redis_common.h"
#include "share/table/ob_table.h"

namespace oceanbase
{
namespace table
{
class ObRedisRKeyUtil;

class ObRedisRKey
{
public:
  ObRedisRKey(ObIAllocator &alloc, const ObString &key, bool is_data, const ObString &subkey)
      : allocator_(alloc), key_(key), is_data_(is_data), subkey_(subkey)
  {}
  virtual ~ObRedisRKey()
  {}
  int encode(ObString &encoded_rkey);
  int encode_next_prefix(ObString &encoded_rkey);
  // static int encode(const ObRedisRKey &rkey, ObString &encoded_rkey);

  TO_STRING_KV(K_(key), K_(is_data), K_(subkey));

private:
  int key_length_to_hex(ObString &key_length_str);
  int encode_inner(bool is_data, const ObString &key, ObString &encoded_rkey);

  ObIAllocator &allocator_;
  ObString key_;
  bool is_data_;
  ObString subkey_;
  DISALLOW_COPY_AND_ASSIGN(ObRedisRKey);
};

class ObRedisRKeyUtil
{
public:
  static const int32_t KEY_LENGTH_HEX_LENGTH = 8;
  static const int32_t IS_DATA_LENGTH = 1;

  // only decode part of encoded_rkey
  static int decode_is_data(const ObString &encoded_rkey, bool &is_data);
  static int decode_subkey(const ObString &encoded_rkey, ObString &subkey);
  static int decode_key(const ObString &encoded_rkey, ObString &key);
  static int gen_partition_key_by_rowkey(ObRedisDataModel model, ObIAllocator &allocator, const ObRowkey &rowkey, ObRowkey &partition_key);

private:
  static int hex_to_key_length(const ObString &hex_str, uint32_t &key_length);
};
}  // namespace table
}  // namespace oceanbase
#endif /* OCEANBASE_TABLE_REDIS_OB_REDIS_RKEY_H */
