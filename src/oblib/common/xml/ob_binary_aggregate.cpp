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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_binary_aggregate.h"

namespace oceanbase {
namespace common {

struct ObJsonBinAggKeyCompare {
  ObStringBuffer *buff_;

  int operator()(const ObAggBinKeyInfo *left, const ObAggBinKeyInfo *right) {
    int res = 0;
    if (left->key_len_ != right->key_len_) {
      res = left->key_len_ < right->key_len_;
    } else {
      ObString left_str = ObString(left->key_len_, buff_->ptr() + left->offset_);
      ObString right_str = ObString(right->key_len_, buff_->ptr() + right->offset_);
      res = (left_str.compare(right_str) < 0);
    }
    return res;
  }

};

ObJsonBinAggSerializer::ObJsonBinAggSerializer(ObIAllocator *allocator,
                                               uint8_t header_type,
                                               ObIAllocator *back_allocator,
                                               ObIAllocator *arr_allocator)
  : value_(allocator),
    key_(allocator),
    buff_(allocator),
    sort_and_unique_(false),
    header_type_(header_type),
    alloc_flag_(ObBinAggAllocFlag::AGG_ALLOC_A),
    key_len_(0),
    value_len_(0),
    count_(0),
    index_start_(0),
    key_entry_start_(0),
    key_entry_size_(0),
    value_entry_start_(0),
    value_entry_size_(0),
    key_start_(0),
    allocator_(allocator),
    back_allocator_(back_allocator),
    arr_allocator_(arr_allocator),
    page_allocator_(*(arr_allocator == nullptr ? allocator : arr_allocator), common::ObModIds::OB_MODULE_PAGE_ALLOCATOR),
    key_info_(OB_MALLOC_NORMAL_BLOCK_SIZE, page_allocator_)
{
  new (&header_) ObMulBinHeaderSerializer();
}

int ObJsonBinAggSerializer::append_key_and_value(ObString key, ObStringBuffer &value, ObJsonBin *json_val)
{
  INIT_SUCC(ret);
  value.reuse();
  ObAggBinKeyInfo *key_info = nullptr;
  int64_t value_record = value_.length();

  ObIAllocator * arr_allocator = get_array_allocator();
  if (OB_ISNULL(key_info = static_cast<ObAggBinKeyInfo*>
                          (arr_allocator->alloc(sizeof(ObAggBinKeyInfo))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("allocate key info struct failed", K(ret));
  } else {
    int key_count = key_info_.count();
    key_info->key_len_ = key.length();
    key_info->is_duplicate_ = false;
    key_info->type_ = static_cast<uint8_t>(json_val->json_type());
    key_info->value_offset_ = value_record;
    key_info->offset_ = key_count == 0 ? 
                        0 : key_info_.at(key_count-1)->offset_ + key_info_.at(key_count-1)->key_len_;

    if (OB_FAIL(json_val->get_total_value(value))) {
    } else if (OB_FAIL(key_.append(key.ptr(), key.length()))) {
    } else {
      uint64_t need_size = value_.length() + value.length() + 8;
      if (check_three_allocator() || need_size <= value_.capacity() || need_size < REPLACE_MEMORY_SIZE_THRESHOLD) {
        if (OB_FAIL(value_.append(value.ptr(), value.length(), 0))) {
        }
      } else {
        if (first_alloc_flag()) {
          if (OB_FAIL(copy_and_reset(back_allocator_, allocator_, value))) {
          } else {
            set_second_alloc();
          }
        } else {
          if (OB_FAIL(copy_and_reset(allocator_, back_allocator_, value))) {
          } else {
            set_first_alloc();
          }
        }
      }
      key_info->value_len_ = value.length();
      if (OB_SUCC(ret) && OB_FAIL(key_info_.push_back(key_info))) {
        LOG_WARN("failed to push back key_info.", K(ret));
      }
    }
  }

  return ret;
}

int64_t ObJsonBinAggSerializer::estimate_total(int64_t base_length, int64_t count)
{
  int64_t res = 0;
  uint8_t estimate_smaller_type = ObMulModeVar::get_var_type(base_length);
  uint8_t estimate_smaller = ObMulModeVar::get_var_size(estimate_smaller_type);
  uint8_t estimated_size_type = 0;
  do {
    estimate_smaller = ObMulModeVar::get_var_size(estimate_smaller_type);
    uint8_t count_type = ObMulModeVar::get_var_type(count);
    uint8_t count_size = ObMulModeVar::get_var_size(count_type);

    // for head_
    uint8_t header_obj_var_size_type = ObMulModeVar::get_var_type(res > 0 ? res : base_length);
    uint8_t header_obj_var_size = ObMulModeVar::get_var_size(header_obj_var_size_type);
    uint8_t header_obj_var_offset = MUL_MODE_BIN_HEADER_LEN + count_size;
    uint64_t header_size = header_obj_var_offset + header_obj_var_size;

    // for total
    int64_t total = base_length + (sizeof(uint8_t) + estimate_smaller) * count + 
                    2 * estimate_smaller * count + header_size;
    estimated_size_type = ObMulModeVar::get_var_type(total);
    res = total;
  } while (estimate_smaller_type < ObMulModeBinLenSize::MBL_UINT64 
            && estimate_smaller_type++ < estimated_size_type);
  return res;
}

int ObJsonBinAggSerializer::construct_header()
{
  INIT_SUCC(ret);
  ObStringBuffer header_buff(allocator_);

  uint64_t count = key_info_.count();
  int64_t key_len = key_.length();
  int64_t value_len = value_.length();

  if (has_unique_flag()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < key_info_.count() - 1; i++) {
      ObAggBinKeyInfo *key_info = key_info_.at(i);
      ObAggBinKeyInfo *next_key_info = key_info_.at(i + 1);
      if (key_info->key_len_ == next_key_info->key_len_) {
        int64_t this_key_start = i == 0 ? 0 : key_info_.at(i - 1)->offset_ + key_info_.at(i - 1)->key_len_;
        int64_t next_key_start = this_key_start + key_info->key_len_;
        ObString this_key(key_info->key_len_, key_.ptr() + key_info->offset_);
        ObString next_key(next_key_info->key_len_, key_.ptr() + next_key_info->offset_);
        if (this_key.compare(next_key) == 0) {
          key_info->is_duplicate_ = true;
          count--;
          key_len -= key_info->key_len_;
          value_len -= key_info->value_len_;
        }
      }
    }
  }
  count_ = count;
  key_len_ = key_len;
  value_len_ = value_len;

  ObString header_str;
  int64_t total_size = ObJsonBinAggSerializer::estimate_total(value_len_ + key_len_, count_);
  ObMulBinHeaderSerializer header_serializer(&header_buff, 
                                              static_cast<ObMulModeNodeType>(header_type_), 
                                              total_size, 
                                              count_);
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(header_serializer.serialize())) {
  } else if (OB_FALSE_IT(header_str = header_serializer.buffer()->string())) {
  } else if (OB_FAIL(buff_.reserve(total_size))) {
  } else if (OB_FAIL(buff_.append(header_str.ptr(), header_str.length(), 0))) {
  } else {
    header_ = header_serializer;
  }

  return ret;
}

void ObJsonBinAggSerializer::set_key_entry(int64_t entry_idx,  int64_t key_offset, int64_t key_len)
{
  int64_t offset = key_entry_start_ + entry_idx * (key_entry_size_ * 2);
  char* write_buf = buff_.ptr() + offset;
  ObMulModeVar::set_var(key_offset, header_.get_entry_var_size_type(), write_buf);

  write_buf += key_entry_size_;
  ObMulModeVar::set_var(key_len, header_.get_entry_var_size_type(), write_buf);
}

void ObJsonBinAggSerializer::set_value_entry(int64_t entry_idx,  uint8_t type, int64_t value_offset)
{
  int64_t offset = value_entry_start_ + entry_idx * (value_entry_size_ + sizeof(uint8_t));
  char* write_buf = buff_.ptr() + offset;
  ObMulModeVar::set_var(value_offset, header_.get_entry_var_size_type(), write_buf);
  write_buf += value_entry_size_;
  *reinterpret_cast<uint8_t*>(write_buf) = type;
}

int ObJsonBinAggSerializer::set_key(int64_t key_offset, int64_t key_len)
{
  INIT_SUCC(ret);
  char* write_buf = key_.ptr() + key_offset;
  if (OB_FAIL(buff_.append(write_buf, key_len, 0))) {
  }
  return ret;
}

int ObJsonBinAggSerializer::set_value(int64_t value_offset, int64_t value_len)
{
  INIT_SUCC(ret);
  char* write_buf = value_.ptr() + value_offset;
  if (OB_FAIL(buff_.append(write_buf, value_len, 0))) {
  }
  return ret;
}

int ObJsonBinAggSerializer::reserve_meta()
{
  INIT_SUCC(ret);
  int64_t pos = buff_.length();
  uint32_t reserve_size = key_start_ - index_start_;
  if (OB_FAIL(buff_.set_length(pos + reserve_size))) {
  }
  return ret;
}

int ObJsonBinAggSerializer::construct_meta()
{
  INIT_SUCC(ret);
  index_start_ = header_.header_size();
  key_entry_start_ = index_start_;
  key_entry_size_ = value_entry_size_ = header_.get_entry_var_size();
  value_entry_start_ = is_json_array()
                         ? key_entry_start_
                         : key_entry_start_ + (key_entry_size_ * 2) * count_;
  key_start_ = value_entry_start_ + (sizeof(uint8_t) + value_entry_size_) * count_;
  int64_t value_start = key_start_ + key_len_;
  
  if (key_start_ > header_.total_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("key start unexpected.", K(ret), K(key_start_));
  } else if (OB_FAIL(reserve_meta())) {
  } else {
    int64_t key_offset = 0;
    int64_t i_offset = 0;
    int64_t value_offset = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < key_info_.count(); i++) {
      ObAggBinKeyInfo *key_info = key_info_.at(i);
      if (!has_unique_flag()) {
        if (header_type_ == static_cast<uint8_t>(ObJsonNodeType::J_OBJECT)) {
          set_key_entry(i, key_start_ + key_offset, key_info->key_len_);
        }
        set_value_entry(i, key_info->type_, value_start + key_info->value_offset_);
        key_offset += key_info->key_len_;
      } else if (!key_info->is_duplicate_) {
        if (header_type_ == static_cast<uint8_t>(ObJsonNodeType::J_OBJECT)) {
          set_key_entry(i_offset, key_start_ + key_offset, key_info->key_len_);
        } 
        set_value_entry(i_offset, key_info->type_, value_offset + value_start);
        key_offset += key_info->key_len_;
        value_offset += key_info->value_len_;
        i_offset++;
      }
      
    }
  }

  return ret;
}

int ObJsonBinAggSerializer::construct_key_and_value()
{
  INIT_SUCC(ret);
  if (!is_json_array()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < key_info_.count(); i++) {
      ObAggBinKeyInfo *key_info = key_info_.at(i);
      if ((has_unique_flag() && key_info->is_duplicate_)) {
        // do nothing
      } else if (OB_FAIL(set_key(key_info->offset_, key_info->key_len_))) {
      }
    }
  }

  if (!has_unique_flag()) {
    if (OB_FAIL(buff_.append(value_.ptr(), value_.length(), 0))) {
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < key_info_.count(); i++) {
      ObAggBinKeyInfo *key_info = key_info_.at(i);
      if (key_info->is_duplicate_) {
        // do nothing
      } else if (OB_FAIL(set_value(key_info->value_offset_, key_info->value_len_))) {
      }
    }
  }
  return ret;
}

int ObJsonBinAggSerializer::copy_and_reset(ObIAllocator* new_allocator,
                                       ObIAllocator* old_allocator, 
                                       ObStringBuffer &add_value)
{
  INIT_SUCC(ret);
  if (OB_ISNULL(new_allocator)) {
    // do nothing
  } else {
    ObStringBuffer new_key(new_allocator);
    ObStringBuffer new_value(new_allocator);
    ObAggBinKeyArray new_key_info;

    if (OB_FAIL(new_value.reserve(value_.length() + add_value.length()))) {
    } else if (OB_FAIL(new_value.append(value_.ptr(), value_.length(), 0))) {
    } else if (OB_FAIL(new_value.append(add_value.ptr(), add_value.length(), 0))) {
    } else if (OB_FAIL(new_key.append(key_.ptr(), key_.length(), 0))) {
    } else {
      key_.reset();
      value_.reset();
      old_allocator->reset();
      if (OB_FAIL(key_.deep_copy(new_allocator, new_key))) {
      } else if (OB_FAIL(value_.deep_copy(new_allocator, new_value))) {
      }
    }

  }

  return ret;
}

int ObJsonBinAggSerializer::rewrite_total_size()
{
  INIT_SUCC(ret);
  int64_t actual_total_size = buff_.length();
  int64_t calculate_total_size = header_.get_obj_size();
  if (calculate_total_size == actual_total_size) {
    // do nothing
  } else if (ObMulModeVar::get_var_type(calculate_total_size) < 
              ObMulModeVar::get_var_type(actual_total_size)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("header size invalided", K(ret));
  } else {
    if (header_.obj_var_size_ == 1) {
      *reinterpret_cast<uint8_t*>(buff_.ptr() + header_.begin_ + header_.obj_var_offset_) = static_cast<uint8_t>(actual_total_size);
    } else if (header_.obj_var_size_ == 2) {
      *reinterpret_cast<uint16_t*>(buff_.ptr() + header_.begin_ + header_.obj_var_offset_) = static_cast<uint16_t>(actual_total_size);
    } else if (header_.obj_var_size_ == 4) {
      *reinterpret_cast<uint32_t*>(buff_.ptr() + header_.begin_ + header_.obj_var_offset_) = static_cast<uint32_t>(actual_total_size);
    } else {
      *reinterpret_cast<uint64_t*>(buff_.ptr() + header_.begin_ + header_.obj_var_offset_) = actual_total_size;
    }
  }
  return ret;
}

int ObJsonBinAggSerializer::serialize()
{
  INIT_SUCC(ret);

  if (!json_not_sort() && OB_FALSE_IT(do_json_sort())) {
  } else if (OB_FAIL(construct_header())) {
  } else if (OB_FAIL(construct_meta())) {
  } else if (OB_FAIL(construct_key_and_value())) { // merge key_ and value_
  } else if (OB_FAIL(rewrite_total_size())) {
  }

  return ret;
}

void ObJsonBinAggSerializer::do_json_sort()
{
  ObJsonBinAggKeyCompare cmp;
  cmp.buff_ = &key_;
  std::stable_sort(key_info_.begin(), key_info_.end(), cmp);
}


}; // namespace common

}; // namespace oceanbase
