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

#ifndef _OCEANBASE_STORAGE_FTS_DICT_OB_FT_CACHE_H_
#define _OCEANBASE_STORAGE_FTS_DICT_OB_FT_CACHE_H_

#include "lib/utility/alloc_assist.h"
#include "lib/hash_func/murmur_hash.h"
#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/cache/ob_kv_storecache.h"
#include "share/cache/ob_kvcache_struct.h"
#include "storage/fts/dict/ob_ft_dat_dict.h"
#include "storage/fts/dict/ob_ft_dict_def.h"
#include "storage/fts/ob_fts_struct.h"

#include <cstdint>

namespace oceanbase
{
namespace storage
{
class ObDictCacheKey : public common::ObIKVCacheKey
{
public:
  ObDictCacheKey(const uint64_t name,
                 const ObFTDictType dict_type,
                 int32_t range_id)
      : name_(name), dict_type_(dict_type), range_id_(range_id)
  {
  }
  ~ObDictCacheKey() override {}

  bool operator==(const ObIKVCacheKey &other) const override
  {
    const ObDictCacheKey &other_key = reinterpret_cast<const ObDictCacheKey &>(other);
    return (&other == this)
           || ((other_key.name_ == name_) && (other_key.dict_type_ == dict_type_)
               && (other_key.range_id_ == range_id_));
  }

  uint64_t hash() const override
  {
    uint64_t hash_val = 0;
    hash_val = murmurhash(&name_, sizeof(name_), hash_val);
    hash_val = murmurhash(&dict_type_, sizeof(dict_type_), hash_val);
    hash_val = murmurhash(&range_id_, sizeof(range_id_), hash_val);
    return hash_val;
  }

  int equal(const ObIKVCacheKey &other, bool &equal) const override
  {
    equal = *this == other;
    return OB_SUCCESS;
  }
  int hash(uint64_t &hash_value) const override
  {
    hash_value = hash();
    return OB_SUCCESS;
  }
  
  int64_t size() const override { return sizeof(ObDictCacheKey); }

  int deep_copy(char *buf, const int64_t buf_len, ObIKVCacheKey *&key) const override
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size())) {
      ret = OB_INVALID_ARGUMENT;
      CLOG_LOG(WARN, "invalid argument for ob dict cache", K(ret), K(buf_len), K(size()));
    } else {
      ObDictCacheKey *new_key = new (buf) ObDictCacheKey(name_, dict_type_, range_id_);
      key = new_key;
    }
    return ret;
  }

  TO_STRING_KV(K_(name), K_(dict_type), K_(range_id));
private:
  // to change to name
  uint64_t name_; // when build dict
  ObFTDictType dict_type_;
  int32_t range_id_;
};

class ObDictCacheValue : public common::ObIKVCacheValue
{
public:
  ObDictCacheValue(ObFTDAT *dat_block) : dat_block_(dat_block) {}
  ~ObDictCacheValue() override {}
  int64_t size() const override { return sizeof(ObDictCacheValue) + dat_block_->mem_block_size_; }
  int deep_copy(char *buf, const int64_t buf_len, ObIKVCacheValue *&value) const override
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size())) {
      ret = OB_INVALID_ARGUMENT;
      CLOG_LOG(WARN, "invalid argument for ob dict cache", K(ret), K(buf_len), K(size()));
    } else {
      ObFTDAT *new_data = reinterpret_cast<ObFTDAT *>(buf + sizeof(ObDictCacheValue));
      MEMCPY(new_data, dat_block_, dat_block_->mem_block_size_);
      ObIKVCacheValue *new_value = new (buf) ObDictCacheValue(new_data);
      value = new_value;
    }
    return ret;
  }

public:
  ObFTDAT *dat_block_;
};

class ObDictCache : public common::ObKVCache<ObDictCacheKey, ObDictCacheValue>
{
public:
  ObDictCache() {}
  virtual ~ObDictCache() {}
  int get_dict(const ObDictCacheKey &key,
               const ObDictCacheValue *&value,
               common::ObKVCacheHandle &handle);

  int put_and_fetch_dict(const ObDictCacheKey &key,
                         const ObDictCacheValue &value,
                         const ObDictCacheValue *&pvalue,
                         common::ObKVCacheHandle &handle);

public:
  static ObDictCache &get_instance()
  {
    static ObDictCache cache;
    return cache;
  }

private:
  DISALLOW_COPY_AND_ASSIGN(ObDictCache);
};

class ObFTTokenCacheKey : public common::ObIKVCacheKey
{
public:
  ObFTTokenCacheKey();
  ObFTTokenCacheKey(const uint64_t tenant_id,
                    const uint64_t dictionary_epoch,
                    const common::ObCollationType collation_type,
                    const common::ObString &parser_name,
                    const common::ObString &parser_properties,
                    const common::ObString &fulltext);
  ~ObFTTokenCacheKey() override = default;

  int equal(const common::ObIKVCacheKey &other, bool &equal) const override;
  int hash(uint64_t &hash_value) const override;
  int64_t size() const override;
  int deep_copy(char *buf,
                const int64_t buf_len,
                common::ObIKVCacheKey *&key) const override;
  bool is_valid() const;

  TO_STRING_KV(K_(tenant_id), K_(dictionary_epoch), K_(collation_type),
      K_(parser_name), K_(parser_properties), K_(fulltext));

private:
  uint64_t tenant_id_;
  uint64_t dictionary_epoch_;
  common::ObCollationType collation_type_;
  common::ObString parser_name_;
  common::ObString parser_properties_;
  common::ObString fulltext_;
};

class ObFTTokenCacheValue : public common::ObIKVCacheValue
{
public:
  ObFTTokenCacheValue();
  ObFTTokenCacheValue(const int64_t document_length,
                      const int64_t word_count,
                      const common::ObString &serialized_words);
  ~ObFTTokenCacheValue() override = default;

  int64_t size() const override;
  int deep_copy(char *buf,
                const int64_t buf_len,
                common::ObIKVCacheValue *&value) const override;
  bool is_valid() const;
  int deserialize(common::ObIAllocator &allocator,
                  const common::ObObjMeta &word_meta,
                  ObFTWordMap &word_map) const;
  static int serialize(common::ObIAllocator &allocator,
                       const int64_t document_length,
                       const ObFTWordMap &word_map,
                       ObFTTokenCacheValue &value);

  int64_t get_document_length() const { return document_length_; }
  int64_t get_word_count() const { return word_count_; }
  const common::ObString &get_serialized_words() const { return serialized_words_; }

  TO_STRING_KV(K_(document_length), K_(word_count), K_(serialized_words));

private:
  int64_t document_length_;
  int64_t word_count_;
  common::ObString serialized_words_;
};

class ObFTTokenCache : public common::ObKVCache<ObFTTokenCacheKey, ObFTTokenCacheValue>
{
public:
  ObFTTokenCache() = default;
  ~ObFTTokenCache() override = default;

  int get_token(const ObFTTokenCacheKey &key,
                const ObFTTokenCacheValue *&value,
                common::ObKVCacheHandle &handle);
  int put_token(const ObFTTokenCacheKey &key, const ObFTTokenCacheValue &value);

  static ObFTTokenCache &get_instance()
  {
    static ObFTTokenCache cache;
    return cache;
  }

private:
  DISALLOW_COPY_AND_ASSIGN(ObFTTokenCache);
};

} //  namespace storage
} //  namespace oceanbase

#endif // _OCEANBASE_STORAGE_FTS_DICT_OB_FT_CACHE_H_
