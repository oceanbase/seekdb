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
#ifndef OB_STORAGE_TRUNCATE_INFO_TRUNCATE_INFO_KV_CACHE_H_
#define OB_STORAGE_TRUNCATE_INFO_TRUNCATE_INFO_KV_CACHE_H_
#include "share/cache/ob_kvcache_struct.h"
#include "share/cache/ob_kv_storecache.h"
namespace oceanbase
{
namespace storage
{
struct ObTruncateInfo;
struct ObTruncateInfoArray;
class ObTruncateInfoCacheKey final : public common::ObIKVCacheKey
{
public:
  ObTruncateInfoCacheKey(
    const uint64_t tenant_id,
    const ObTabletID &tablet_id,
    const int64_t schema_version,
    const int64_t last_major_snapshot);
  virtual ~ObTruncateInfoCacheKey() = default;
  bool is_valid() const;
  virtual int equal(const ObIKVCacheKey &other, bool &equal) const override;
  virtual int hash(uint64_t &hash_value) const override;
  virtual uint64_t get_tenant_id() const override { return tenant_id_; }
  virtual int64_t size() const override { return sizeof(*this); }
  virtual int deep_copy(char *buf, const int64_t buf_len, ObIKVCacheKey *&key) const override;
  TO_STRING_KV(K_(tenant_id), K_(tablet_id), K_(schema_version), K_(last_major_snapshot));
private:
  uint64_t tenant_id_;
  ObTabletID tablet_id_;
  int64_t schema_version_;
  int64_t last_major_snapshot_; // truncate info array in kv_cache is related to last_major
};

class ObTruncateInfoCacheValue final : public common::ObIKVCacheValue
{
public:
  ObTruncateInfoCacheValue();
  virtual ~ObTruncateInfoCacheValue() = default;
  bool is_valid() const { return count_ > 0 && nullptr != truncate_info_array_; }
  virtual int64_t size() const override { return sizeof(*this) + deep_copy_size_; }
  int init(const int64_t count, ObTruncateInfo *truncate_info_array);
  const ObTruncateInfo *get_truncate_info_array() const { return truncate_info_array_; };
  int64_t get_count() const { return count_; }
  virtual int deep_copy(char *buf, const int64_t buf_len, ObIKVCacheValue *&value) const override;
  TO_STRING_KV(K_(count), "array", ObArrayWrap<ObTruncateInfo>(truncate_info_array_, count_), K_(deep_copy_size));
private:
  int64_t count_;
  ObTruncateInfo *truncate_info_array_;
  int64_t deep_copy_size_;
};

struct ObTruncateInfoValueHandle final
{
  ObTruncateInfoValueHandle()
    : value_(nullptr), handle_()
  {}
  ~ObTruncateInfoValueHandle() = default;
  bool is_valid() const { return nullptr != value_ && value_->is_valid() && handle_.is_valid(); }
  void reset()
  {
    value_ = nullptr;
    handle_.reset();
  }
  TO_STRING_KV(KP_(value), K_(handle));
  const ObTruncateInfoCacheValue *value_;
  common::ObKVCacheHandle handle_;
};

class ObTruncateInfoKVCache final: public common::ObKVCache<ObTruncateInfoCacheKey, ObTruncateInfoCacheValue>
{
public:
  ObTruncateInfoKVCache() = default;
  virtual ~ObTruncateInfoKVCache() = default;
  void destroy()
  {
    common::ObKVCache<ObTruncateInfoCacheKey, ObTruncateInfoCacheValue>::destroy();
  }
  int init(const char *cache_name, const int64_t priority);
  int get_truncate_info_array(const ObTruncateInfoCacheKey &key, ObTruncateInfoValueHandle &handle);
  int put_truncate_info_array(const ObTruncateInfoCacheKey &key, ObTruncateInfoCacheValue &value);
private:
  DISALLOW_COPY_AND_ASSIGN(ObTruncateInfoKVCache);
};

struct ObTruncateInfoKVCacheUtil final
{
  static int get_truncate_info_array(
    ObIAllocator &allocator,
    const ObTruncateInfoCacheKey &key,
    storage::ObTruncateInfoArray &truncate_info_array);
  static int put_truncate_info_array(
    const ObTruncateInfoCacheKey &key,
    ObIArray<ObTruncateInfo *> &distinct_truncate_info_array);
};

} // namespace storage
} // namespace oceanbase

#endif // OB_STORAGE_TRUNCATE_INFO_TRUNCATE_INFO_KV_CACHE_H_
