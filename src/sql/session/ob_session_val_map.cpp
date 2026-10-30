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

#define USING_LOG_PREFIX SQL_SESSION
#include "sql/session/ob_session_val_map.h"

#include "share/datum/typed_datum_compare.h"
#include "common/datum/datum_parameter_codec.h"
#include "lib/container/ob_se_array.h"
using namespace oceanbase;
using namespace sql;
using namespace common;

namespace {
// Keep the existing alternating StringBuf arenas while providing aligned native
// scalar storage through the explicit allocator interface used by Datum cloning.
class SessionValueAllocator final : public ObIAllocator {
public:
  explicit SessionValueAllocator(ObStringBuf &buffer) : buffer_(buffer) {}
  void *alloc(int64_t bytes) override { return buffer_.get_arena().alloc_aligned(bytes); }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *address) override { buffer_.free(address); }
private:
  ObStringBuf &buffer_;
};


}

int ObSessionVariable::serialize(char *buf, int64_t buf_len, int64_t &pos) const
{
  int ret = serialization::encode(buf, buf_len, pos, meta_);
  if (OB_SUCC(ret)) {
    ret = DatumParameterCodec::encode_value(value_.datum_, value_.meta_, buf, buf_len, pos);
  }
  return ret;
}

int ObSessionVariable::deserialize(const char *buf, int64_t data_len, int64_t &pos,
                                   ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  int64_t decoded_pos = pos;
  ObSessionVariable candidate;
  // Preserve the existing UNIS member-record convention for omitted tail fields.
  if (decoded_pos < data_len) {
    ret = serialization::decode(buf, data_len, decoded_pos, candidate.meta_);
  }
  if (OB_SUCC(ret) && decoded_pos < data_len) {
    ret = DatumParameterCodec::decode_unis_value(buf, data_len, decoded_pos, owner,
                                                candidate.value_.datum_, candidate.value_.meta_);
  }
  if (OB_SUCC(ret)) { *this = candidate; pos = decoded_pos; }
  return ret;
}

int64_t ObSessionVariable::get_serialize_size() const
{
  int64_t value_size = 0;
  const int ret = DatumParameterCodec::value_size(value_.datum_, value_.meta_, value_size);
  if (ret != OB_SUCCESS) {
    LOG_ERROR("invalid native session variable value", K(ret), K(value_));
    return 0;
  }
  return serialization::encoded_length(meta_) + value_size;
}

ObSessionValMap::ObSessionValMap()
    : block_allocator_(SMALL_BLOCK_SIZE, common::OB_MALLOC_NORMAL_BLOCK_SIZE,
                       ObMalloc(ObModIds::OB_SQL_SESSION_VAR_MAP)),
    var_name_val_map_allocer_(SMALL_BLOCK_SIZE, ObWrapperAllocator(&block_allocator_)),
    str_buf1_(ObModIds::OB_SQL_SESSION_VAR_MAP, 64 * 1024),
    str_buf2_(ObModIds::OB_SQL_SESSION_VAR_MAP, 64 * 1024),
    current_buf_index_(0),
    bucket_allocator_(ObModIds::OB_SQL_SESSION_VAR_MAP),
    bucket_allocator_wrapper_(&bucket_allocator_),
    str_buf_free_threshold_(0),
    next_free_mem_point_(str_buf_free_threshold_)
{
  str_buf_[0] = &str_buf1_;
  str_buf_[1] = &str_buf2_;
}

ObSessionValMap::ObSessionValMap(const int64_t block_size,
                                 const ObWrapperAllocator &block_allocator)
    : block_allocator_(SMALL_BLOCK_SIZE, common::OB_MALLOC_NORMAL_BLOCK_SIZE,
                       ObMalloc(ObMemAttr(ObModIds::OB_SQL_SESSION_VAR_MAP))),
    var_name_val_map_allocer_(block_size, block_allocator),
    str_buf1_(ObModIds::OB_SQL_SESSION_VAR_MAP),
    str_buf2_(ObModIds::OB_SQL_SESSION_VAR_MAP),
    current_buf_index_(0),
    bucket_allocator_(ObMemAttr(ObModIds::OB_SQL_SESSION_VAR_MAP)),
    bucket_allocator_wrapper_(&bucket_allocator_),
    str_buf_free_threshold_(0),
    next_free_mem_point_(str_buf_free_threshold_)
{
  str_buf_[0] = &str_buf1_;
  str_buf_[1] = &str_buf2_;
}

ObSessionValMap::~ObSessionValMap()
{
}

int ObSessionValMap::init(int64_t free_threshold,
                          int64_t bucket_num,
                          common::ObWrapperAllocator *bucket_allocator)
{
  int ret = OB_SUCCESS;
  str_buf_free_threshold_ = free_threshold;
  next_free_mem_point_ = str_buf_free_threshold_;
  if (NULL != bucket_allocator) {
    bucket_allocator_wrapper_.set_alloc(bucket_allocator);
  }

  if (OB_FAIL(map_.create(bucket_num, &var_name_val_map_allocer_,
                                       &bucket_allocator_wrapper_))) {
  }
  return ret;
}

void ObSessionValMap::reuse()
{
  map_.clear();
  str_buf1_.reset();
  str_buf2_.reset();
}

void ObSessionValMap::reset()
{
  reuse();
  map_.destroy();
  var_name_val_map_allocer_.reset();
  bucket_allocator_.reset();
  block_allocator_.reset();
}
// Success then return OB_SUCCESS
// Other return codes indicate failure
int ObSessionValMap::set_refactored(const common::ObString &name, const ObSessionVariable &sess_var)
{
  int ret = OB_SUCCESS;
  // The incoming view can refer to our current arena or even an existing entry.
  // Compaction stages it before releasing that arena, along with all live entries.
  ObString tmp_name = name;
  ObSessionVariable tmp_sess_var = sess_var;
  bool already_owned = false;
  if (current_buf_index_ >= 2 || current_buf_index_ < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid current_buf_index_", K(current_buf_index_), K(name), K(sess_var));
  } else if (OB_ISNULL(str_buf_[current_buf_index_])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid internal args", KP(str_buf_[current_buf_index_]), K(name), K(sess_var));
  } else if (OB_FAIL(free_mem(&tmp_name, &tmp_sess_var, &already_owned))) {
  } else {
    SessionValueAllocator owner(*str_buf_[current_buf_index_]);
    if (!already_owned) {
      if (OB_FAIL(str_buf_[current_buf_index_]->write_string(tmp_name, &tmp_name))) {
      } else if (OB_FAIL(clone_typed_value(tmp_sess_var.value_, tmp_sess_var.value_, owner))) {
      }
    }
    if (OB_SUCC(ret)) { ret = map_.set_refactored(tmp_name, tmp_sess_var, 1); }
  }
  return ret;
}

int ObSessionValMap::get_refactored(const ObString &name, ObSessionVariable &sess_var) const
{
  return map_.get_refactored(name, sess_var);
}

const ObSessionVariable *ObSessionValMap::get(const common::ObString &name) const
{
  return map_.get(name);
}

int ObSessionValMap::erase_refactored(const ObString &key, ObSessionVariable *sess_var)
{
  return map_.erase_refactored(key, sess_var);
}

int ObSessionValMap::assign(const ObSessionValMap &other)
{
  int ret = OB_SUCCESS;
  reuse();
  if (other.size() > 0) {
    for (VarNameValMap::const_iterator iter = other.map_.begin();
       (OB_SUCCESS ==ret ) && iter != other.map_.end(); iter++) {
       const ObString &key = iter->first;
       const ObSessionVariable &value = iter->second;
       ret = set_refactored(key, value);
    }
  }
  return ret;
}

int ObSessionValMap::free_mem(ObString *pending_name,
                               ObSessionVariable *pending_value,
                               bool *pending_owned)
{
  int ret = OB_SUCCESS;
  if (pending_owned != nullptr) { *pending_owned = false; }
  if (current_buf_index_ >= 2 || current_buf_index_ < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid current_buf_index_", K(current_buf_index_));
  } else if (OB_ISNULL(str_buf_[current_buf_index_])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid internal args", KP(str_buf_[current_buf_index_]));
  } else if (str_buf_[current_buf_index_]->used() > next_free_mem_point_) {
    const int32_t next_index = current_buf_index_ == 0 ? 1 : 0;
    ObStringBuf &next = *str_buf_[next_index];
    SessionValueAllocator owner(next);
    struct PendingCopy {
      ObString *name_;
      ObSessionVariable *value_;
      ObString copied_name_;
      ObSessionVariable copied_value_;
    };
    ObSEArray<PendingCopy, 8> copies;
    // No entry is changed until every name and payload has been cloned. A
    // failed allocation leaves all live entries backed by the current buffer.
    for (VarNameValMap::iterator iter = map_.begin(); OB_SUCC(ret) && iter != map_.end(); ++iter) {
      PendingCopy copy;
      copy.name_ = &iter->first;
      copy.value_ = &iter->second;
      copy.copied_value_.meta_ = iter->second.meta_;
      if (OB_FAIL(next.write_string(iter->first, &copy.copied_name_))) {
      } else if (OB_FAIL(clone_typed_value(iter->second.value_, copy.copied_value_.value_, owner))) {
      } else if (OB_FAIL(copies.push_back(copy))) {
      }
    }
    ObString copied_pending_name;
    ObSessionVariable copied_pending_value;
    if (OB_SUCC(ret) && pending_name != nullptr && pending_value != nullptr) {
      copied_pending_value.meta_ = pending_value->meta_;
      if (OB_FAIL(next.write_string(*pending_name, &copied_pending_name))) {
      } else if (OB_FAIL(clone_typed_value(pending_value->value_, copied_pending_value.value_, owner))) {
      }
    }
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; i < copies.count(); ++i) {
        *copies.at(i).name_ = copies.at(i).copied_name_;
        *copies.at(i).value_ = copies.at(i).copied_value_;
      }
      if (pending_name != nullptr && pending_value != nullptr) {
        *pending_name = copied_pending_name;
        *pending_value = copied_pending_value;
        if (pending_owned != nullptr) { *pending_owned = true; }
      }
      str_buf_[current_buf_index_]->reuse();
      current_buf_index_ = next_index;
      next_free_mem_point_ = std::max(2 * next.used(), str_buf_free_threshold_);
    } else {
      next.reuse();
    }
  }
  return ret;
}

DEFINE_SERIALIZE(ObSessionValMap)
{
  int ret = OB_SUCCESS;
  int64_t num_of_entries = size();
  OB_UNIS_ENCODE(num_of_entries);
  if (OB_SUCC(ret) && num_of_entries > 0) {
    for (VarNameValMap::const_iterator iter = map_.begin();
        (OB_SUCC(ret)) && iter != map_.end(); iter++) {
      const ObString &key = iter->first;
      const ObSessionVariable &value = iter->second;
      OB_UNIS_ENCODE(key);
      if (OB_SUCC(ret)) { ret = value.serialize(buf, buf_len, pos); }
    }
  }
  return ret;
}

DEFINE_DESERIALIZE(ObSessionValMap)
{
  int ret = OB_SUCCESS;
  int64_t num_of_entries = 0;
  reuse();
  OB_UNIS_DECODE(num_of_entries);
  ObString key;
  ObSessionVariable value;
  ObArenaAllocator decode_owner(ObModIds::OB_SQL_SESSION_VAR_MAP);
  if (num_of_entries > 0 && !map_.created()) {
    if (OB_FAIL(init(256 * 1024, 64, NULL))) {
    }
  }
  for (int64_t i = 0; (OB_SUCC(ret)) && (i < num_of_entries); ++i) {
    key.reset();
    value.reset();
    OB_UNIS_DECODE(key);
    if (OB_SUCC(ret) && pos < data_len) { ret = value.deserialize(buf, data_len, pos, decode_owner); }
    if (OB_SUCC(ret)) { ret = this->set_refactored(key, value); }
    decode_owner.reuse();
  }
  return ret;
}

DEFINE_GET_SERIALIZE_SIZE(ObSessionValMap)
{
  int64_t len = 0;
  int64_t num_of_entries = size();
  OB_UNIS_ADD_LEN(num_of_entries);
  if (num_of_entries > 0) {
    for (VarNameValMap::const_iterator iter = map_.begin();
        (iter != map_.end()); iter++) {
      const ObString &key = iter->first;
      const ObSessionVariable &value = iter->second;
      OB_UNIS_ADD_LEN(key);
      len += value.get_serialize_size();
    }
  }
  return len;
}
