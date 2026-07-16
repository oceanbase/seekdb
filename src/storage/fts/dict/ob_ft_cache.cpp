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

#define USING_LOG_PREFIX STORAGE_FTS

#include "storage/fts/dict/ob_ft_cache.h"

#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace storage
{
namespace
{
bool string_equal(const ObString &left, const ObString &right)
{
  return left.length() == right.length()
      && (left.empty() || 0 == MEMCMP(left.ptr(), right.ptr(), left.length()));
}

int copy_string(char *&pos, const ObString &source, ObString &target)
{
  int ret = OB_SUCCESS;
  if (!source.empty()) {
    MEMCPY(pos, source.ptr(), source.length());
    target.assign_ptr(pos, source.length());
    pos += source.length();
  }
  return ret;
}
} // namespace

int ObDictCache::get_dict(const ObDictCacheKey &key,
                          const ObDictCacheValue *&value,
                          common::ObKVCacheHandle &handle)
{
  int ret = OB_SUCCESS;
  handle.reset();
  if (OB_FAIL(get(key, value, handle))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("get dict from cache failed", K(ret));
    }
  }
  return ret;
}


int ObDictCache::put_and_fetch_dict(const ObDictCacheKey &key,
                                    const ObDictCacheValue &value,
                                    const ObDictCacheValue *&pvalue,
                                    common::ObKVCacheHandle &handle)
{
  int ret = OB_SUCCESS;
  handle.reset();
  if (OB_FAIL(put_and_fetch(key, value, pvalue, handle))) {
    LOG_WARN("put dict to cache failed", K(ret));
  }
  return ret;
}

ObFTTokenCacheKey::ObFTTokenCacheKey()
    : tenant_id_(OB_INVALID_TENANT_ID),
      dictionary_epoch_(0),
      collation_type_(CS_TYPE_INVALID),
      parser_name_(),
      parser_properties_(),
      fulltext_()
{
}

ObFTTokenCacheKey::ObFTTokenCacheKey(const uint64_t tenant_id,
                                     const uint64_t dictionary_epoch,
                                     const ObCollationType collation_type,
                                     const ObString &parser_name,
                                     const ObString &parser_properties,
                                     const ObString &fulltext)
    : tenant_id_(tenant_id),
      dictionary_epoch_(dictionary_epoch),
      collation_type_(collation_type),
      parser_name_(parser_name),
      parser_properties_(parser_properties),
      fulltext_(fulltext)
{
}

int ObFTTokenCacheKey::equal(const ObIKVCacheKey &other, bool &equal) const
{
  const ObFTTokenCacheKey &other_key = reinterpret_cast<const ObFTTokenCacheKey &>(other);
  equal = tenant_id_ == other_key.tenant_id_
      && dictionary_epoch_ == other_key.dictionary_epoch_
      && collation_type_ == other_key.collation_type_
      && string_equal(parser_name_, other_key.parser_name_)
      && string_equal(parser_properties_, other_key.parser_properties_)
      && string_equal(fulltext_, other_key.fulltext_);
  return OB_SUCCESS;
}

int ObFTTokenCacheKey::hash(uint64_t &hash_value) const
{
  hash_value = common::murmurhash(&tenant_id_, sizeof(tenant_id_), 0);
  hash_value = common::murmurhash(&dictionary_epoch_, sizeof(dictionary_epoch_), hash_value);
  hash_value = common::murmurhash(&collation_type_, sizeof(collation_type_), hash_value);
  hash_value = common::murmurhash(parser_name_.ptr(), parser_name_.length(), hash_value);
  hash_value = common::murmurhash(parser_properties_.ptr(), parser_properties_.length(), hash_value);
  hash_value = common::murmurhash(fulltext_.ptr(), fulltext_.length(), hash_value);
  return OB_SUCCESS;
}

int64_t ObFTTokenCacheKey::size() const
{
  return sizeof(*this) + parser_name_.length() + parser_properties_.length() + fulltext_.length();
}

bool ObFTTokenCacheKey::is_valid() const
{
  return OB_INVALID_TENANT_ID != tenant_id_
      && CS_TYPE_INVALID != collation_type_
      && !parser_name_.empty()
      && fulltext_.length() >= 0;
}

int ObFTTokenCacheKey::deep_copy(char *buf,
                                 const int64_t buf_len,
                                 ObIKVCacheKey *&key) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size()) || OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fulltext token cache key", K(ret), KP(buf), K(buf_len), K(size()), KPC(this));
  } else {
    ObFTTokenCacheKey *new_key = new (buf) ObFTTokenCacheKey(
        tenant_id_, dictionary_epoch_, collation_type_, ObString(), ObString(), ObString());
    char *pos = buf + sizeof(*new_key);
    if (OB_FAIL(copy_string(pos, parser_name_, new_key->parser_name_))) {
      LOG_WARN("failed to copy parser name", K(ret));
    } else if (OB_FAIL(copy_string(pos, parser_properties_, new_key->parser_properties_))) {
      LOG_WARN("failed to copy parser properties", K(ret));
    } else if (OB_FAIL(copy_string(pos, fulltext_, new_key->fulltext_))) {
      LOG_WARN("failed to copy fulltext", K(ret));
    } else {
      key = new_key;
    }
  }
  return ret;
}

ObFTTokenCacheValue::ObFTTokenCacheValue()
    : document_length_(0), word_count_(0), serialized_words_()
{
}

ObFTTokenCacheValue::ObFTTokenCacheValue(const int64_t document_length,
                                         const int64_t word_count,
                                         const ObString &serialized_words)
    : document_length_(document_length),
      word_count_(word_count),
      serialized_words_(serialized_words)
{
}

int64_t ObFTTokenCacheValue::size() const
{
  return sizeof(*this) + serialized_words_.length();
}

bool ObFTTokenCacheValue::is_valid() const
{
  return document_length_ >= 0 && word_count_ >= 0 && serialized_words_.length() >= 0;
}

int ObFTTokenCacheValue::deep_copy(char *buf,
                                   const int64_t buf_len,
                                   ObIKVCacheValue *&value) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size()) || OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fulltext token cache value", K(ret), KP(buf), K(buf_len), K(size()), KPC(this));
  } else {
    char *data = buf + sizeof(ObFTTokenCacheValue);
    if (!serialized_words_.empty()) {
      MEMCPY(data, serialized_words_.ptr(), serialized_words_.length());
    }
    value = new (buf) ObFTTokenCacheValue(
        document_length_, word_count_, ObString(serialized_words_.length(), data));
  }
  return ret;
}

int ObFTTokenCacheValue::serialize(ObIAllocator &allocator,
                                   const int64_t document_length,
                                   const ObFTWordMap &word_map,
                                   ObFTTokenCacheValue &value)
{
  int ret = OB_SUCCESS;
  int64_t serialized_size = 0;
  char *serialized_buf = nullptr;
  static constexpr int64_t ENTRY_HEADER_SIZE = sizeof(uint32_t) + sizeof(int64_t);
  if (OB_UNLIKELY(document_length < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fulltext document length", K(ret), K(document_length));
  }
  for (ObFTWordMap::const_iterator iter = word_map.begin(); OB_SUCC(ret) && iter != word_map.end(); ++iter) {
    const int64_t word_length = iter->first.get_word().get_string().length();
    if (OB_UNLIKELY(word_length <= 0 || word_length > UINT32_MAX
                    || serialized_size > INT64_MAX - ENTRY_HEADER_SIZE - word_length)) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("fulltext token cache value is too large", K(ret), K(word_length), K(serialized_size));
    } else {
      serialized_size += ENTRY_HEADER_SIZE + word_length;
    }
  }
  if (OB_SUCC(ret) && serialized_size > 0
      && OB_ISNULL(serialized_buf = static_cast<char *>(allocator.alloc(serialized_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate serialized fulltext tokens", K(ret), K(serialized_size));
  }
  if (OB_SUCC(ret)) {
    int64_t pos = 0;
    for (ObFTWordMap::const_iterator iter = word_map.begin(); iter != word_map.end(); ++iter) {
      const ObString &word = iter->first.get_word().get_string();
      const uint32_t word_length = static_cast<uint32_t>(word.length());
      const int64_t word_count = iter->second;
      MEMCPY(serialized_buf + pos, &word_length, sizeof(word_length));
      pos += sizeof(word_length);
      MEMCPY(serialized_buf + pos, &word_count, sizeof(word_count));
      pos += sizeof(word_count);
      MEMCPY(serialized_buf + pos, word.ptr(), word.length());
      pos += word.length();
    }
    value = ObFTTokenCacheValue(document_length,
                                word_map.size(),
                                ObString(serialized_size, serialized_buf));
  }
  return ret;
}

int ObFTTokenCacheValue::deserialize(ObIAllocator &allocator,
                                     const ObObjMeta &word_meta,
                                     ObFTWordMap &word_map) const
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t decoded_count = 0;
  if (OB_UNLIKELY(!is_valid()) || OB_UNLIKELY(!word_map.created())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fulltext token cache value or word map", K(ret), KPC(this), K(word_map.created()));
  }
  while (OB_SUCC(ret) && pos < serialized_words_.length()) {
    uint32_t word_length = 0;
    int64_t word_count = 0;
    static constexpr int64_t ENTRY_HEADER_SIZE = sizeof(word_length) + sizeof(word_count);
    if (OB_UNLIKELY(serialized_words_.length() - pos < ENTRY_HEADER_SIZE)) {
      ret = OB_INVALID_DATA;
      LOG_WARN("truncated fulltext token cache entry", K(ret), K(pos), K(serialized_words_.length()));
    } else {
      MEMCPY(&word_length, serialized_words_.ptr() + pos, sizeof(word_length));
      pos += sizeof(word_length);
      MEMCPY(&word_count, serialized_words_.ptr() + pos, sizeof(word_count));
      pos += sizeof(word_count);
      if (OB_UNLIKELY(0 == word_length || word_count <= 0
                      || serialized_words_.length() - pos < word_length)) {
        ret = OB_INVALID_DATA;
        LOG_WARN("invalid fulltext token cache entry", K(ret), K(word_length), K(word_count),
            K(pos), K(serialized_words_.length()));
      } else {
        char *word_buf = static_cast<char *>(allocator.alloc(word_length));
        if (OB_ISNULL(word_buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failed to allocate cached fulltext token", K(ret), K(word_length));
        } else {
          MEMCPY(word_buf, serialized_words_.ptr() + pos, word_length);
          const ObFTWord word(word_length, word_buf, word_meta);
          if (OB_FAIL(word_map.set_refactored(word, word_count))) {
            LOG_WARN("failed to restore cached fulltext token", K(ret), K(word), K(word_count));
          } else {
            pos += word_length;
            ++decoded_count;
          }
        }
      }
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(decoded_count != word_count_)) {
    ret = OB_INVALID_DATA;
    LOG_WARN("fulltext token cache word count mismatch", K(ret), K(decoded_count), K_(word_count));
  }
  return ret;
}

int ObFTTokenCache::get_token(const ObFTTokenCacheKey &key,
                              const ObFTTokenCacheValue *&value,
                              ObKVCacheHandle &handle)
{
  int ret = OB_SUCCESS;
  value = nullptr;
  handle.reset();
  if (OB_FAIL(get(key, value, handle)) && OB_ENTRY_NOT_EXIST != ret) {
    LOG_WARN("failed to get fulltext tokens from cache", K(ret), K(key));
  }
  return ret;
}

int ObFTTokenCache::put_token(const ObFTTokenCacheKey &key,
                              const ObFTTokenCacheValue &value)
{
  int ret = put(key, value, false /* overwrite */);
  if (OB_ENTRY_EXIST == ret) {
    ret = OB_SUCCESS;
  } else if (OB_FAIL(ret)) {
    LOG_WARN("failed to put fulltext tokens into cache", K(ret), K(key), K(value));
  }
  return ret;
}

} //  namespace storage
} //  namespace oceanbase
