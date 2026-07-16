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

#include <cstdint>

namespace oceanbase
{
namespace storage
{
typedef int32_t ObFTTokenCode;
typedef int32_t ObFTWordBase;
typedef uint32_t ObFTWordStateIndex;

enum class ObFTDictType : uint32_t
{
  DICT_TYPE_INVALID = 0,
  DICT_IK_MAIN = 1,
  DICT_IK_QUAN = 2,
  DICT_IK_STOP = 3,
};

/**
 * @class ObFTSingleToken
 * @brief struct to store a single character of a charset;
 */
struct ObFTSingleToken
{
public:
  ObFTSingleToken() : token_(""), token_char_len_(0) { }
  ObFTSingleToken &operator=(const ObFTSingleToken &other) = default;
  int set_token(const char *token, int32_t token_len);
  ObString get_token() const { return ObString(token_char_len_, token_); }
  bool operator==(const ObFTSingleToken &other) const;

public:
  char token_[common::ObCharset::MAX_MB_LEN];
  uint8_t token_char_len_;
} __attribute__((packed));

class ObFTDictDesc
{
public:
  enum BuildMode {
    DDL_EXE = 0,
    REFRESH_ONLY,
    DML_OR_SELECT_EXE
  };
public:
  ObFTDictDesc(const ObString &name,
               const ObFTDictType type,
               const ObCharsetType charset,
               const ObCollationType coll_type)
      : name_(name), type_(type), table_id_(OB_INVALID_ID), table_name_(),
        charset_(charset), coll_type_(coll_type), need_casedown_(is_ci_collation(coll_type))
  {
  }
  ObFTDictDesc(const ObCharsetType charset,
               const ObCollationType coll_type,
               const uint64_t table_id,
               const common::ObString &table_name,
               const bool need_casedown)
      : name_(), type_(ObFTDictType::DICT_TYPE_INVALID),
        table_id_(table_id), table_name_(table_name), charset_(charset), coll_type_(coll_type),
        need_casedown_(need_casedown)
  {
  }
  ObFTDictDesc(const ObCharsetType charset,
               const ObCollationType coll_type,
               const uint64_t table_id,
               const common::ObString &table_name)
      : name_(), type_(ObFTDictType::DICT_TYPE_INVALID),
        table_id_(table_id), table_name_(table_name), charset_(charset), coll_type_(coll_type),
        need_casedown_(is_ci_collation(coll_type))
  {
  }

  static bool is_ci_collation(const ObCollationType coll_type)
  {
    const ObCharsetInfo *cs = common::ObCharset::get_charset(coll_type);
    return (cs != nullptr) && (cs->state & OB_CS_CI);
  }

  uint64_t get_cache_name() const
  {
    static constexpr uint64_t IDENTITY_PAYLOAD_MASK = (1ULL << 62) - 1;
    static constexpr uint64_t TABLE_IDENTITY_TAG = 1ULL << 62;
    static constexpr uint64_t NAME_IDENTITY_TAG = 1ULL << 63;
    uint64_t cache_name = static_cast<uint64_t>(type_);
    if (OB_INVALID_ID != table_id_) {
      cache_name = TABLE_IDENTITY_TAG | (table_id_ & IDENTITY_PAYLOAD_MASK);
    } else if (!table_name_.empty()) {
      const uint64_t name_hash =
          common::ObCharset::hash(common::CS_TYPE_UTF8MB4_GENERAL_CI, table_name_, 0);
      cache_name = NAME_IDENTITY_TAG | (name_hash & IDENTITY_PAYLOAD_MASK);
    }
    return cache_name;
  }

  TO_STRING_KV(K_(name), K_(type), K_(charset), K_(coll_type), K_(table_id), K_(table_name), K_(need_casedown));

public:
  common::ObString name_;
  ObFTDictType type_;
  uint64_t table_id_;
  common::ObString table_name_;
  ObCharsetType charset_;
  ObCollationType coll_type_;
  bool need_casedown_;
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_DICT_OB_FT_DICT_DEF_H_
