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

#ifndef OCEANBASE_SHARE_OB_TABLET_AUTOINCREMENT_PARAM_H_
#define OCEANBASE_SHARE_OB_TABLET_AUTOINCREMENT_PARAM_H_

#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/hash_func/murmur_hash.h"
#include "common/object/ob_obj_type.h"
#include "common/ob_tablet_id.h"

namespace oceanbase
{
namespace share
{

static const uint64_t DEFAULT_HANDLE_CACHE_SIZE = 10;
static const uint64_t DEFAULT_TABLET_INCREMENT_CACHE_SIZE = 10000;

struct ObTabletAutoincKey final
{
public:
  ObTabletAutoincKey() : tablet_id_(0) {}
  void reset() { tablet_id_.reset(); }
  bool operator==(const ObTabletAutoincKey &other) const
  {
    return other.tablet_id_ == tablet_id_;
  }
  int compare(const ObTabletAutoincKey &other)
  {
    return tablet_id_.compare(other.tablet_id_);
  }
  uint64_t hash() const { return tablet_id_.hash(); }
  bool is_valid() const { return tablet_id_.is_valid(); }
  TO_STRING_KV(K_(tablet_id));

public:
  common::ObTabletID tablet_id_;
};

struct ObTabletAutoincInterval final
{
  OB_UNIS_VERSION(1);
public:
  ObTabletAutoincInterval() : tablet_id_(), start_(0), end_(0) {}
  bool is_valid() const { return tablet_id_.is_valid() && end_ >= start_; }
  void reset()
  {
    tablet_id_.reset();
    start_ = 0;
    end_ = 0;
  }
  TO_STRING_KV(K_(tablet_id), K_(start), K_(end));

public:
  common::ObTabletID tablet_id_;
  uint64_t start_;
  uint64_t end_;
};

struct ObTabletCacheInterval final
{
public:
  ObTabletCacheInterval()
      : tablet_id_(OB_INVALID_ID),
        cache_size_(0),
        task_id_(-1),
        next_value_(0),
        start_(0),
        end_(0)
  {}
  ObTabletCacheInterval(common::ObTabletID tablet_id, uint64_t cache_size)
      : tablet_id_(tablet_id),
        cache_size_(cache_size),
        task_id_(-1),
        next_value_(0),
        start_(0),
        end_(0)
  {}
  ~ObTabletCacheInterval() {}
  void reset();
  void set(uint64_t start, uint64_t end);
  int next_value(uint64_t &next_value);
  int get_value(uint64_t &value);
  int fetch(uint64_t count, ObTabletCacheInterval &dest);
  uint64_t count() const { return end_ - start_ + 1; }
  uint64_t remain_count() const { return end_ - next_value_ + 1; }
  bool operator<(const ObTabletCacheInterval &other) { return tablet_id_ < other.tablet_id_; }
  TO_STRING_KV(K_(tablet_id), K_(start), K_(end), K_(cache_size), K_(next_value), K_(task_id));

public:
  common::ObTabletID tablet_id_;
  uint64_t cache_size_;
  int64_t task_id_;
private:
  uint64_t next_value_;
  uint64_t start_;
  uint64_t end_;
};

struct ObTabletAutoincParam final
{
public:
  ObTabletAutoincParam() : auto_increment_cache_size_(DEFAULT_TABLET_INCREMENT_CACHE_SIZE) {}
  bool is_valid() const { return auto_increment_cache_size_ > 0; }
  TO_STRING_KV(K_(auto_increment_cache_size));

public:
  int64_t auto_increment_cache_size_;
  OB_UNIS_VERSION(1);
};

struct ObTabletAutoincSeqCopyParam final
{
  OB_UNIS_VERSION(1);
public:
  ObTabletAutoincSeqCopyParam()
      : src_tablet_id_(),
        dest_tablet_id_(),
        ret_code_(OB_SUCCESS),
        autoinc_seq_(0)
  {}
  bool is_valid() const { return src_tablet_id_.is_valid(); }
  TO_STRING_KV(K_(src_tablet_id), K_(dest_tablet_id), K_(ret_code), K_(autoinc_seq));

public:
  common::ObTabletID src_tablet_id_;
  common::ObTabletID dest_tablet_id_;
  int ret_code_;
  uint64_t autoinc_seq_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_TABLET_AUTOINCREMENT_PARAM_H_
