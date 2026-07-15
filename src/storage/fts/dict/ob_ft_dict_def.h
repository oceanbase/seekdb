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

#ifndef _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_DEF_H_
#define _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_DEF_H_

#include "lib/charset/ob_charset.h"
#include "lib/hash_func/murmur_hash.h"

#include <cstdint>

namespace oceanbase
{
namespace storage
{
typedef int32_t ObFTWordCode;
typedef int32_t ObFTWordBase;
typedef uint32_t ObFTWordStateIndex;

/**
 * @class ObFTSingleWord
 * @brief sturct to store a single character of a charset;
 */
struct ObFTSingleWord
{
public:
  ObFTSingleWord() : word(""), word_len(0) {}
  ObFTSingleWord(const ObFTSingleWord &other) = default;
  ObFTSingleWord &operator=(const ObFTSingleWord &other) = default;

  int32_t set_word(const char *word, int32_t word_len);
  ObString get_word() const;
  bool operator==(const ObFTSingleWord &other) const;

public:
  char word[common::ObCharset::MAX_MB_LEN];
  uint8_t word_len;
} __attribute__((packed));

enum class ObFTDictType : uint32_t
{
  DICT_TYPE_INVALID = 0,
  DICT_IK_MAIN = 1,
  DICT_IK_QUAN = 2,
  DICT_IK_STOP = 3,
};

class ObFTDictDesc
{
public:
  ObFTDictDesc(const ObString &name,
               const ObFTDictType type,
               const ObCharsetType charset,
               const ObCollationType coll_type,
               const uint64_t tenant_id = 0,
               const uint64_t table_id = 0,
               const int64_t version = 0,
               const bool is_builtin = true)
      : name_(name), type_(type), charset_(charset), coll_type_(coll_type),
        tenant_id_(tenant_id), table_id_(table_id), version_(version), is_builtin_(is_builtin)
  {
  }

  // 内置词典保持原有按类型共享的缓存身份；用户词典优先按稳定 schema 身份隔离。
  bool is_builtin() const { return is_builtin_; }
  uint64_t get_cache_identity() const
  {
    uint64_t hash = 0;
    hash = common::murmurhash(&type_, sizeof(type_), hash);
    if (!is_builtin_) {
      if (0 == table_id_) {
        // 旧属性链路暂未携带 table ID 时，以全限定表名兜底，避免不同用户词典共享缓存。
        hash = common::murmurhash(name_.ptr(), name_.length(), hash);
      } else {
        hash = common::murmurhash(&tenant_id_, sizeof(tenant_id_), hash);
        hash = common::murmurhash(&table_id_, sizeof(table_id_), hash);
        hash = common::murmurhash(&version_, sizeof(version_), hash);
      }
    }
    return hash;
  }

public:
  ObString name_;
  ObFTDictType type_;
  ObCharsetType charset_;
  ObCollationType coll_type_;
  uint64_t tenant_id_;
  uint64_t table_id_;
  int64_t version_;
  bool is_builtin_;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_DEF_H_
