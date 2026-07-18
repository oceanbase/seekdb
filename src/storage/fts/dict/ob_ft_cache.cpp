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
#include "storage/fts/dict/ob_ft_dict_hub.h"
#include "storage/fts/ob_fts_plugin_helper.h"

#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace storage
{
namespace
{
bool cache_string_equal(const ObString &left, const ObString &right)
{
  return left.length() == right.length()
      && (left.empty() || 0 == MEMCMP(left.ptr(), right.ptr(), left.length()));
}

void copy_cache_string(char *&position, const ObString &source, ObString &target)
{
  if (!source.empty()) {
    MEMCPY(position, source.ptr(), source.length());
    target.assign_ptr(position, source.length());
    position += source.length();
  }
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

ObFTSegmentCacheKey::ObFTSegmentCacheKey()
  : tenant_id_(OB_INVALID_TENANT_ID),
    dictionary_epoch_(0),
    collation_type_(CS_TYPE_INVALID),
    parser_name_(),
    parser_properties_(),
    fulltext_()
{
}

ObFTSegmentCacheKey::ObFTSegmentCacheKey(const uint64_t tenant_id,
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

int ObFTSegmentCacheKey::equal(const ObIKVCacheKey &other, bool &equal) const
{
  const ObFTSegmentCacheKey &other_key = static_cast<const ObFTSegmentCacheKey &>(other);
  equal = tenant_id_ == other_key.tenant_id_
      && dictionary_epoch_ == other_key.dictionary_epoch_
      && collation_type_ == other_key.collation_type_
      && cache_string_equal(parser_name_, other_key.parser_name_)
      && cache_string_equal(parser_properties_, other_key.parser_properties_)
      && cache_string_equal(fulltext_, other_key.fulltext_);
  return OB_SUCCESS;
}

int ObFTSegmentCacheKey::hash(uint64_t &hash_value) const
{
  hash_value = murmurhash(&tenant_id_, sizeof(tenant_id_), 0);
  hash_value = murmurhash(&dictionary_epoch_, sizeof(dictionary_epoch_), hash_value);
  hash_value = murmurhash(&collation_type_, sizeof(collation_type_), hash_value);
  hash_value = murmurhash(parser_name_.ptr(), parser_name_.length(), hash_value);
  hash_value = murmurhash(parser_properties_.ptr(), parser_properties_.length(), hash_value);
  hash_value = murmurhash(fulltext_.ptr(), fulltext_.length(), hash_value);
  return OB_SUCCESS;
}

int64_t ObFTSegmentCacheKey::size() const
{
  return sizeof(*this) + parser_name_.length() + parser_properties_.length() + fulltext_.length();
}

bool ObFTSegmentCacheKey::is_valid() const
{
  return OB_INVALID_TENANT_ID != tenant_id_
      && dictionary_epoch_ > 0
      && CS_TYPE_INVALID != collation_type_
      && !parser_name_.empty()
      && fulltext_.length() >= 0;
}

int ObFTSegmentCacheKey::deep_copy(char *buf,
                                   const int64_t buf_len,
                                   ObIKVCacheKey *&key) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size()) || OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid segment cache key copy", K(ret), KP(buf), K(buf_len), K(size()));
  } else {
    ObFTSegmentCacheKey *new_key = new (buf) ObFTSegmentCacheKey(
        tenant_id_, dictionary_epoch_, collation_type_, ObString(), ObString(), ObString());
    char *position = buf + sizeof(*new_key);
    copy_cache_string(position, parser_name_, new_key->parser_name_);
    copy_cache_string(position, parser_properties_, new_key->parser_properties_);
    copy_cache_string(position, fulltext_, new_key->fulltext_);
    key = new_key;
  }
  return ret;
}

ObFTSegmentCacheValue::ObFTSegmentCacheValue()
  : document_length_(0), token_count_(0), payload_()
{
}

ObFTSegmentCacheValue::ObFTSegmentCacheValue(const int64_t document_length,
                                             const int64_t token_count,
                                             const ObString &payload)
  : document_length_(document_length), token_count_(token_count), payload_(payload)
{
}

int64_t ObFTSegmentCacheValue::size() const
{
  return sizeof(*this) + payload_.length();
}

bool ObFTSegmentCacheValue::is_valid() const
{
  return document_length_ >= 0 && token_count_ >= 0 && payload_.length() >= 0;
}

int ObFTSegmentCacheValue::deep_copy(char *buf,
                                     const int64_t buf_len,
                                     ObIKVCacheValue *&value) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < size()) || OB_UNLIKELY(!is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid segment cache value copy", K(ret), KP(buf), K(buf_len), K(size()));
  } else {
    char *payload_buf = buf + sizeof(ObFTSegmentCacheValue);
    if (!payload_.empty()) {
      MEMCPY(payload_buf, payload_.ptr(), payload_.length());
    }
    value = new (buf) ObFTSegmentCacheValue(
        document_length_, token_count_, ObString(payload_.length(), payload_buf));
  }
  return ret;
}

int ObFTSegmentCacheValue::build(ObIAllocator &allocator,
                                 const int64_t document_length,
                                 const ObFTWordMap &word_map,
                                 ObFTSegmentCacheValue &value)
{
  int ret = OB_SUCCESS;
  static constexpr int64_t ENTRY_HEADER_SIZE = sizeof(uint32_t) + sizeof(int64_t);
  int64_t payload_size = 0;
  char *payload_buf = nullptr;
  if (OB_UNLIKELY(document_length < 0)) {
    ret = OB_INVALID_ARGUMENT;
  }
  for (ObFTWordMap::const_iterator iter = word_map.begin();
       OB_SUCC(ret) && iter != word_map.end(); ++iter) {
    const int64_t word_length = iter->first.get_word().get_string().length();
    if (OB_UNLIKELY(word_length <= 0 || word_length > UINT32_MAX
                    || payload_size > INT64_MAX - ENTRY_HEADER_SIZE - word_length)) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("segment cache payload too large", K(ret), K(word_length), K(payload_size));
    } else {
      payload_size += ENTRY_HEADER_SIZE + word_length;
    }
  }
  if (OB_SUCC(ret) && payload_size > 0
      && OB_ISNULL(payload_buf = static_cast<char *>(allocator.alloc(payload_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  }
  if (OB_SUCC(ret)) {
    int64_t position = 0;
    for (ObFTWordMap::const_iterator iter = word_map.begin(); iter != word_map.end(); ++iter) {
      const ObString word = iter->first.get_word().get_string();
      const uint32_t word_length = static_cast<uint32_t>(word.length());
      const int64_t word_frequency = iter->second;
      MEMCPY(payload_buf + position, &word_length, sizeof(word_length));
      position += sizeof(word_length);
      MEMCPY(payload_buf + position, &word_frequency, sizeof(word_frequency));
      position += sizeof(word_frequency);
      MEMCPY(payload_buf + position, word.ptr(), word.length());
      position += word.length();
    }
    value = ObFTSegmentCacheValue(
        document_length, word_map.size(), ObString(payload_size, payload_buf));
  }
  return ret;
}

int ObFTSegmentCacheValue::restore(ObIAllocator &allocator,
                                   const ObObjMeta &word_meta,
                                   ObFTWordMap &word_map) const
{
  int ret = OB_SUCCESS;
  int64_t position = 0;
  int64_t entry_count = 0;
  int64_t *entry_offsets = nullptr;
  static constexpr int64_t ENTRY_HEADER_SIZE = sizeof(uint32_t) + sizeof(int64_t);
  if (OB_UNLIKELY(!is_valid()) || OB_UNLIKELY(!word_map.created())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(token_count_ > INT64_MAX / static_cast<int64_t>(sizeof(int64_t)))) {
    ret = OB_SIZE_OVERFLOW;
  } else if (token_count_ > 0
      && OB_ISNULL(entry_offsets = static_cast<int64_t *>(
                       allocator.alloc(token_count_ * sizeof(int64_t))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  }
  while (OB_SUCC(ret) && position < payload_.length()) {
    uint32_t word_length = 0;
    int64_t word_frequency = 0;
    if (OB_UNLIKELY(entry_count >= token_count_)) {
      ret = OB_INVALID_DATA;
    } else if (OB_UNLIKELY(payload_.length() - position < ENTRY_HEADER_SIZE)) {
      ret = OB_INVALID_DATA;
    } else {
      entry_offsets[entry_count] = position;
      MEMCPY(&word_length, payload_.ptr() + position, sizeof(word_length));
      position += sizeof(word_length);
      MEMCPY(&word_frequency, payload_.ptr() + position, sizeof(word_frequency));
      position += sizeof(word_frequency);
      if (OB_UNLIKELY(0 == word_length || word_frequency <= 0
                      || payload_.length() - position < word_length)) {
        ret = OB_INVALID_DATA;
      } else {
        position += word_length;
        ++entry_count;
      }
    }
  }
  if (OB_SUCC(ret) && OB_UNLIKELY(entry_count != token_count_)) {
    ret = OB_INVALID_DATA;
  }
  // ObHashMap inserts at the head of each bucket chain.  The payload is written
  // in map iteration order, so reverse insertion preserves the original orde
  // inside every bucket while bucket traversal itself remains unchanged.
  for (int64_t i = entry_count - 1; OB_SUCC(ret) && i >= 0; --i) {
    int64_t entry_position = entry_offsets[i];
    uint32_t word_length = 0;
    int64_t word_frequency = 0;
    MEMCPY(&word_length, payload_.ptr() + entry_position, sizeof(word_length));
    entry_position += sizeof(word_length);
    MEMCPY(&word_frequency, payload_.ptr() + entry_position, sizeof(word_frequency));
    entry_position += sizeof(word_frequency);
    char *word_buf = static_cast<char *>(allocator.alloc(word_length));
    if (OB_ISNULL(word_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(word_buf, payload_.ptr() + entry_position, word_length);
      ObFTWord word(word_length, word_buf, word_meta);
      if (OB_FAIL(word_map.set_refactored(word, word_frequency))) {
        LOG_WARN("failed to restore cached token", K(ret), K(word_length));
      }
    }
  }
  return ret;
}

int ObFTSegmentCache::get_segment(const ObFTSegmentCacheKey &key,
                                  const ObFTSegmentCacheValue *&value,
                                  ObKVCacheHandle &handle)
{
  value = nullptr;
  handle.reset();
  return get(key, value, handle);
}

int ObFTSegmentCache::put_segment(const ObFTSegmentCacheKey &key,
                                  const ObFTSegmentCacheValue &value)
{
  int ret = put(key, value, false);
  if (OB_ENTRY_EXIST == ret) {
    ret = OB_SUCCESS;
  }
  return ret;
}

ObSpinLock &ObFTSegmentCache::get_lock(const ObFTSegmentCacheKey &key)
{
  uint64_t hash_value = 0;
  static_cast<void>(key.hash(hash_value));
  return locks_[hash_value % LOCK_COUNT];
}

int ObFTSegmentCache::segment_with_cache(ObIAllocator &allocator,
                                         ObFTParseHelper &helper,
                                         const ObObjMeta &word_meta,
                                         const ObString &parser_name,
                                         const ObString &parser_properties,
                                         const ObString &fulltext,
                                         int64_t &document_length,
                                         ObFTWordMap &word_map)
{
  int ret = OB_SUCCESS;
  ObFTDictHub *dict_hub = nullptr;
  const ObFTSegmentCacheValue *cache_value = nullptr;
  ObKVCacheHandle cache_handle;
  bool cache_hit = false;
  const bool cacheable = fulltext.length() <= MAX_CACHE_TEXT_LENGTH
      && OB_SUCCESS == ObFTParsePluginData::instance().get_dict_hub(dict_hub);

  if (OB_UNLIKELY(!word_meta.is_valid() || parser_name.empty() || !word_map.created())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid segment cache arguments", K(ret), K(word_meta), K(parser_name),
        K(fulltext.length()), K(word_map.created()));
  } else if (!cacheable) {
    if (!fulltext.empty()
        && OB_FAIL(helper.segment(word_meta, fulltext.ptr(), fulltext.length(),
                                  document_length, word_map))) {
      LOG_WARN("failed to segment uncached fulltext", K(ret), K(fulltext.length()));
    }
  } else {
    const ObFTSegmentCacheKey key(OB_SERVER_TENANT_ID,
                                  dict_hub->get_dictionary_epoch(),
                                  word_meta.get_collation_type(),
                                  parser_name,
                                  parser_properties,
                                  fulltext);
    int cache_ret = get_segment(key, cache_value, cache_handle);
    if (OB_SUCCESS == cache_ret && OB_NOT_NULL(cache_value)) {
      if (OB_SUCCESS == cache_value->restore(allocator, word_meta, word_map)) {
        document_length = cache_value->get_document_length();
        cache_hit = true;
      } else {
        word_map.reuse();
      }
    }
    if (!cache_hit) {
      ObSpinLockGuard lock_guard(get_lock(key));
      cache_handle.reset();
      cache_value = nullptr;
      cache_ret = get_segment(key, cache_value, cache_handle);
      if (OB_SUCCESS == cache_ret && OB_NOT_NULL(cache_value)
          && OB_SUCCESS == cache_value->restore(allocator, word_meta, word_map)) {
        document_length = cache_value->get_document_length();
        cache_hit = true;
      } else {
        word_map.reuse();
      }
      if (!cache_hit) {
        document_length = 0;
        if (!fulltext.empty()
            && OB_FAIL(helper.segment(word_meta, fulltext.ptr(), fulltext.length(),
                                      document_length, word_map))) {
          LOG_WARN("failed to segment fulltext", K(ret), K(fulltext.length()));
        } else {
          ObFTSegmentCacheValue new_value;
          cache_ret = ObFTSegmentCacheValue::build(
              allocator, document_length, word_map, new_value);
          if (OB_SUCCESS == cache_ret) {
            cache_ret = put_segment(key, new_value);
          }
          if (OB_SUCCESS != cache_ret) {
            LOG_DEBUG("failed to publish segment cache value", K(cache_ret), K(key));
          }
        }
      }
    }
  }
  return ret;
}

} //  namespace storage
} //  namespace oceanbase
