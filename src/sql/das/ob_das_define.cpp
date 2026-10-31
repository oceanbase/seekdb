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

#define USING_LOG_PREFIX SQL_DAS
#include "ob_das_define.h"
#include "sql/pl/ob_pl.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace share::schema;
namespace sql
{

//not assign array member
void DASTableAccessMeta::light_assign(const DASTableAccessMeta &other)
{
  table_ref_id_ = other.table_ref_id_;
  ref_table_id_ = other.ref_table_id_;
  flags_ = other.flags_;
}

int DASTableAccessMeta::assign(const DASTableAccessMeta &other)
{
  int ret = OB_SUCCESS;
  light_assign(other);
  ret = related_table_ids_.assign(other.related_table_ids_);
  return ret;
}

int DASTableAccessMeta::init_related_meta(uint64_t related_table_id,
                                         DASTableAccessMeta &related_meta) const
{
  int ret = OB_SUCCESS;
  related_meta.light_assign(*this);
  related_meta.ref_table_id_ = related_table_id;
  related_meta.related_table_ids_.set_capacity(related_table_ids_.count());
  for (int64_t i = 0; OB_SUCC(ret) && i < related_table_ids_.count(); ++i) {
    //the related table ids of other table meta are the other related table id and source table id
    uint64_t tmp_related_id = (related_table_ids_.at(i) == related_table_id ?
                              ref_table_id_ : related_table_ids_.at(i));
    ret = related_meta.related_table_ids_.push_back(tmp_related_id);
  }
  return ret;
}

OB_SERIALIZE_MEMBER(DASTableAccessMeta,
                    table_ref_id_,
                    ref_table_id_,
                    related_table_ids_,
                    flags_);

OB_SERIALIZE_MEMBER(DASRuntimeTabletAccess,
                    tablet_id_,
                    partition_id_,
                    first_level_part_id_);

OB_DEF_SERIALIZE(DASRuntimeTableAccess)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(*access_meta_);
  OB_UNIS_ENCODE(flags_);
  OB_UNIS_ENCODE(tablet_accesses_.size());
  FOREACH_X(tmp_node, tablet_accesses_, OB_SUCC(ret)) {
    DASRuntimeTabletAccess *tablet_access = *tmp_node;
    OB_UNIS_ENCODE(*tablet_access);
  }
  return ret;
}

OB_DEF_DESERIALIZE(DASRuntimeTableAccess)
{
  int ret = OB_SUCCESS;
  int64_t size = 0;
  void *meta_buf = allocator_.alloc(sizeof(DASTableAccessMeta));
  DASTableAccessMeta *access_meta = nullptr;
  if (OB_ISNULL(meta_buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    access_meta = new(meta_buf) DASTableAccessMeta(allocator_);
    access_meta_ = access_meta;
  }
  OB_UNIS_DECODE(*access_meta);
  OB_UNIS_DECODE(flags_);
  OB_UNIS_DECODE(size);
  for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
    DASRuntimeTabletAccess *tablet_access = nullptr;
    void *tablet_buf = allocator_.alloc(sizeof(DASRuntimeTabletAccess));
    if (OB_ISNULL(tablet_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      tablet_access = new(tablet_buf) DASRuntimeTabletAccess();
      tablet_access->access_meta_ = access_meta_;
      if (OB_FAIL(tablet_accesses_.push_back(tablet_access))) {
      }
    }
    OB_UNIS_DECODE(*tablet_access);
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(DASRuntimeTableAccess)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(*access_meta_);
  OB_UNIS_ADD_LEN(flags_);
  OB_UNIS_ADD_LEN(tablet_accesses_.size());
  FOREACH(tmp_node, tablet_accesses_) {
    DASRuntimeTabletAccess *tablet_access = *tmp_node;
    OB_UNIS_ADD_LEN(*tablet_access);
  }
  return len;
}


int DASRuntimeTableAccess::find_tablet_access(const ObTabletID &tablet_id,
                                        DASRuntimeTabletAccess *&tablet_access)
{
  int ret = OB_SUCCESS;
  tablet_access = NULL;
  lookup_cnt_++;
  if (tablet_accesses_map_.created()) {
    if (OB_FAIL(tablet_accesses_map_.get(tablet_id, tablet_access))) {
      if (OB_HASH_NOT_EXIST != ret) {
      }
    }
  }
  if (OB_SUCC(ret) && NULL != tablet_access) {
    // found in hash map
  } else if (OB_HASH_NOT_EXIST == ret) {
    // key not found
    ret = OB_SUCCESS;
  } else {
    FOREACH(tmp_node, tablet_accesses_) {
      DASRuntimeTabletAccess *tmp_tablet_access = *tmp_node;
      if (tmp_tablet_access->tablet_id_ == tablet_id) {
        tablet_access = tmp_tablet_access;
        break;
      }
    }
  }
  if (OB_FAIL(ret) || tablet_accesses_map_.created()) {
    // do nothing
  } else if (lookup_cnt_ > DAS_TABLET_ACCESS_LOOKUP_THRESHOLD
             && tablet_accesses_.size() > DAS_TABLET_ACCESS_SIZE_THRESHOLD
             && OB_FAIL(create_tablet_accesses_map())) {
  }
  return ret;
}

int DASRuntimeTableAccess::add_tablet_access(DASRuntimeTabletAccess *tablet_access)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tablet_access)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(tablet_accesses_.push_back(tablet_access))) {
  } else if (tablet_accesses_map_.created()) {
    if (OB_FAIL(tablet_accesses_map_.set(tablet_access->tablet_id_, tablet_access))) {
    }
  }
  return ret;
}

int TabletHashMap::create(int64_t bucket_num)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(0 >= bucket_num)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (created()) {
    ret = OB_INIT_TWICE;
  } else if (FALSE_IT(bucket_num = hash::cal_next_prime(bucket_num))) {
  } else if (OB_ISNULL(buckets_ = static_cast<TabletHashNode **>(
          allocator_.alloc(bucket_num * sizeof(TabletHashNode *))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMSET(buckets_, 0, bucket_num * sizeof(TabletHashNode *));
    bucket_num_ = bucket_num;
    is_inited_ = true;
  }
  return ret;
}

int TabletHashMap::find_node(const ObTabletID key,
                             TabletHashNode *head,
                             TabletHashNode *&node) const
{
  int ret = OB_SUCCESS;
  node = NULL;
  if (!created()) {
    ret = OB_NOT_INIT;
  } else if (NULL == head) {
    // do nothing
  } else {
    TabletHashNode *cur = head;
    while (NULL != cur && NULL == node) {
      if (cur->key_ == key) {
        node = cur;
      } else {
        cur = cur->next_;
      }
    }
  }
  return ret;
}

int TabletHashMap::set(const ObTabletID key, DASRuntimeTabletAccess *value)
{
  int ret = OB_SUCCESS;
  uint64_t hash_val = 0;
  if (!created()) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(key.hash(hash_val))) {
  } else {
    TabletHashNode *&bucket = buckets_[hash_val % bucket_num_];
    TabletHashNode *dst_node = NULL;
    if (OB_FAIL(find_node(key, bucket, dst_node))) {
    } else if (NULL != dst_node) {
      ret = OB_HASH_EXIST;
    } else {
      TabletHashNode *new_node = static_cast<TabletHashNode *>(
              allocator_.alloc(sizeof(TabletHashNode)));
      if (OB_ISNULL(new_node)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        new_node->key_ = key;
        new_node->value_ = value;
        new_node->next_ = bucket;
        bucket = new_node;
      }
    }
  }
  return ret;
}

int TabletHashMap::get(const ObTabletID key, DASRuntimeTabletAccess *&value)
{
  int ret = OB_SUCCESS;
  uint64_t hash_val = 0;
  if (!created()) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(key.hash(hash_val))) {
  } else {
    TabletHashNode *&bucket = buckets_[hash_val % bucket_num_];
    TabletHashNode *dst_node = NULL;
    if (OB_FAIL(find_node(key, bucket, dst_node))) {
    } else if (NULL == dst_node) {
      ret = OB_HASH_NOT_EXIST;
    } else {
      value = dst_node->value_;
    }
  }
  return ret;
}

int DASRuntimeTableAccess::create_tablet_accesses_map()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(tablet_accesses_map_.created())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(tablet_accesses_map_.create(DAS_TABLET_ACCESS_MAP_BUCKET_SIZE))) {
  } else {
    for (DASRuntimeTabletListIter tablet_node = tablet_accesses_begin();
         OB_SUCC(ret) && tablet_node != tablet_accesses_end(); ++tablet_node) {
      DASRuntimeTabletAccess *tablet_access = *tablet_node;
      if (OB_FAIL(tablet_accesses_map_.set(tablet_access->tablet_id_, tablet_access))) {
      }
    }
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
