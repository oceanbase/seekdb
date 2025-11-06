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

#include "common/rowkey/ob_store_rowkey.h"

namespace oceanbase
{
namespace common
{
ObObj ObStoreRowkey::MIN_OBJECT = ObObj::make_min_obj();
ObObj ObStoreRowkey::MAX_OBJECT = ObObj::make_max_obj();

ObStoreRowkey ObStoreRowkey::MIN_STORE_ROWKEY(&ObStoreRowkey::MIN_OBJECT, 1);
ObStoreRowkey ObStoreRowkey::MAX_STORE_ROWKEY(&ObStoreRowkey::MAX_OBJECT, 1);

//FIXME-yangsuli: this method need to be removed later
//we should NOT allow ObStoreRowkey to be converted to ObRowkey,
//as conceptually it will lose column order info
//now it is used in liboblog,which is OK as liboblog only tests equality

void ObStoreRowkey::destroy(ObIAllocator &allocator)
{
  key_.destroy(allocator);
  hash_ = 0;
  group_idx_ = 0;
}

uint64_t ObStoreRowkey::murmurhash(const uint64_t hash) const
{
  uint64_t hash_ret = hash;
  if (0 == hash_) {
    hash_ = key_.hash();
  }
  if (0 == hash_ret) {
    hash_ret = hash_;
  } else {
    hash_ret = common::murmurhash(&hash_ret, sizeof(int64_t), hash_);
  }
  return hash_ret;
}

bool ObStoreRowkey::contains_min_or_max_obj() const
{
  bool found = false;
  for(int64_t i = 0; !found && i < key_.get_obj_cnt(); i++) {
    if (key_.get_obj_ptr()[i].is_min_value() || key_.get_obj_ptr()[i].is_max_value()) {
      found = true;
    }
  }
  return found;
}


ObExtStoreRowkey::ObExtStoreRowkey()
  : store_rowkey_(),
    collation_free_store_rowkey_(),
    range_cut_pos_(-1),
    first_null_pos_(-1),
    range_check_min_(true),
    group_idx_(0)
{
}

ObExtStoreRowkey::ObExtStoreRowkey(const ObStoreRowkey &store_rowkey)
  : store_rowkey_(store_rowkey),
    collation_free_store_rowkey_(),
    range_cut_pos_(-1),
    first_null_pos_(-1),
    range_check_min_(true),
    group_idx_(0)
{
}

ObExtStoreRowkey::ObExtStoreRowkey(const ObStoreRowkey &store_rowkey,
                                   const ObStoreRowkey &collation_free_store_rowkey)
  : store_rowkey_(store_rowkey),
    collation_free_store_rowkey_(collation_free_store_rowkey),
    range_cut_pos_(-1),
    first_null_pos_(-1),
    range_check_min_(true),
    group_idx_(0)
{
}


int ObExtStoreRowkey::get_possible_range_pos()
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!store_rowkey_.is_valid())) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "ObExtStoreRowkey is not init", K(ret));
  } else {
    for (int64_t i = 0; range_cut_pos_ < 0 && i < store_rowkey_.get_obj_cnt(); i++) {
      if (store_rowkey_.get_obj_ptr()[i].is_min_value()) {
        range_cut_pos_ = i;
        range_check_min_ = true;
      } else if (store_rowkey_.get_obj_ptr()[i].is_max_value()) {
        range_cut_pos_ = i;
        range_check_min_ = false;
      } else if (store_rowkey_.get_obj_ptr()[i].is_null()) {
        if (first_null_pos_ < 0) {
          first_null_pos_ = i;
        }
      }
    }
    if (range_cut_pos_ < 0) {
      range_cut_pos_ = store_rowkey_.get_obj_cnt();
      range_check_min_ = true;
    }
  }

  return ret;
}



int ObExtStoreRowkey::to_collation_free_store_rowkey_on_demand(ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(store_rowkey_.to_collation_free_store_rowkey_on_demand(collation_free_store_rowkey_,
                                                                     allocator))) {
    COMMON_LOG(WARN, "fail to get collation free store rowkey.", K(ret), K(store_rowkey_));
  }

  return ret;
}







} //end namespace common
} //end namespace oceanbase
