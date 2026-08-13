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

#define USING_LOG_PREFIX STORAGE
#include "ob_medium_compaction_info.h"

namespace oceanbase
{
using namespace storage;
using namespace blocksstable;

namespace compaction
{

/*
 * ObParallelMergeInfo
 * */
ObParallelMergeInfo::~ObParallelMergeInfo()
{
  if (list_size_ > 0 || nullptr != parallel_datum_rowkey_list_) {
    LOG_ERROR_RET(OB_ERR_SYS, "exist unfree buf", K_(list_size), KP_(parallel_datum_rowkey_list));
  }
}

template<typename T>
void ObParallelMergeInfo::destroy(ObIAllocator &allocator, T *&array)
{
  if (nullptr != array) {
    for (int i = 0; i < list_size_; ++i) {
      array[i].destroy(allocator);
    }
    allocator.free(array);
    array = nullptr;
  }
}

void ObParallelMergeInfo::destroy(ObIAllocator &allocator)
{
  if (list_size_ > 0) {
    destroy(allocator, parallel_datum_rowkey_list_);
  }
  parallel_info_ = 0;
  format_version_ = PARALLEL_INFO_VERSION;
}

// parallel_info_ contains list_size_, so the rowkey array count is not encoded separately.
int ObParallelMergeInfo::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == buf || buf_len <= 0 || pos < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(buf_len), K(pos));
  } else if (!is_valid() || 0 == list_size_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parallel merge info is invalid", K(ret), KPC(this));
  } else {
    LST_DO_CODE(OB_UNIS_ENCODE, parallel_info_);
    for (int i = 0; OB_SUCC(ret) && i < list_size_; ++i) {
      if (OB_FAIL(parallel_datum_rowkey_list_[i].serialize(buf, buf_len, pos))) {
      }
    }
  }
  return ret;
}

#define ALLOC_ROWKEY_ARRAY(array_ptr, T) \
  void *alloc_buf = nullptr; \
  if (OB_ISNULL(alloc_buf = allocator.alloc(sizeof(T) * list_size_))) { \
    ret = OB_ALLOCATE_MEMORY_FAILED; \
    LOG_WARN("failed to alloc rowkey array", K(ret), K(list_size_)); \
  } else { \
    array_ptr = new(alloc_buf) T[list_size_]; \
  }

int ObParallelMergeInfo::deserialize(
    common::ObIAllocator &allocator,
    const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == buf || data_len <= 0 || pos < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(data_len), K(pos));
  } else {
    LST_DO_CODE(OB_UNIS_DECODE, parallel_info_);
    if (OB_FAIL(ret)) {
    } else if (PARALLEL_INFO_VERSION != format_version_ || 0 != reserved_) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("parallel merge info format mismatch", K(ret), K_(format_version), K_(reserved));
    } else if (0 == list_size_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("list size is invalid", K(ret), K(list_size_));
    } else {
      ALLOC_ROWKEY_ARRAY(parallel_datum_rowkey_list_, ObDatumRowkey);
      if (OB_SUCC(ret)) {
        ObStorageDatum tmp_storage_datum[OB_INNER_MAX_ROWKEY_COLUMN_NUMBER];
        ObDatumRowkey tmp_datum_rowkey;
        tmp_datum_rowkey.assign(tmp_storage_datum, OB_INNER_MAX_ROWKEY_COLUMN_NUMBER);
        for (int i = 0; OB_SUCC(ret) && i < list_size_; ++i) {
          if (OB_FAIL(tmp_datum_rowkey.deserialize(buf, data_len, pos))) {
          } else if (OB_FAIL(tmp_datum_rowkey.deep_copy(parallel_datum_rowkey_list_[i] /*dst*/, allocator))) {
          }
        } // end of for
      }
      if (OB_FAIL(ret)) {
        destroy(allocator); // free parallel_end_key_list_ in destroy
      }
    }
  }
  return ret;
}

int64_t ObParallelMergeInfo::get_serialize_size() const
{
  int64_t len = 0;
  if (list_size_ > 0) {
    len += serialization::encoded_length_vi32(parallel_info_);
    for (int i = 0; i < list_size_; ++i) {
      len += parallel_datum_rowkey_list_[i].get_serialize_size();
    }
  }
  return len;
}

int ObParallelMergeInfo::generate_from_range_array(
    ObIAllocator &allocator,
    ObArrayArray<ObStoreRange> &paral_range)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(0 != list_size_
      || nullptr != parallel_datum_rowkey_list_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parallel merge info is not empty", K(ret), KPC(this));
  } else {
    int64_t sum_range_cnt = 0;
    for (int64_t i = 0; i < paral_range.count(); ++i) {
      sum_range_cnt += paral_range.at(i).count();
    }
    if (sum_range_cnt <= VALID_CONCURRENT_CNT || sum_range_cnt > UINT8_MAX) {
      // do nothing
    } else {
      list_size_ = sum_range_cnt - 1;
      ret = generate_datum_rowkey_list(allocator, paral_range);
    }
  }
  LOG_DEBUG("parallel range info", K(ret), KPC(this), K(paral_range), K(paral_range.count()), K(paral_range.at(0)));
  if (OB_FAIL(ret)) {
    destroy(allocator);
  }
  return ret;
}

int ObParallelMergeInfo::generate_datum_rowkey_list(
    ObIAllocator &allocator,
    ObArrayArray<ObStoreRange> &paral_range)
{
  int ret = OB_SUCCESS;
  format_version_ = PARALLEL_INFO_VERSION;
  ALLOC_ROWKEY_ARRAY(parallel_datum_rowkey_list_, ObDatumRowkey);
  int64_t cnt = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < paral_range.count() && cnt < list_size_; ++i) {
    const ObIArray<ObStoreRange> &range_array = paral_range.at(i);
    for (int64_t j = 0; OB_SUCC(ret) && j < range_array.count() && cnt < list_size_; ++j) {
      if (OB_FAIL(parallel_datum_rowkey_list_[cnt++].from_rowkey(range_array.at(j).get_end_key().get_rowkey(), allocator))) {
      }
    }
  } // end of loop array
  return ret;
}

template<typename T>
int ObParallelMergeInfo::deep_copy_list(common::ObIAllocator &allocator, const T *src, T *&dst)
{
  int ret = OB_SUCCESS;
  ALLOC_ROWKEY_ARRAY(dst, T);
  for (int i = 0; OB_SUCC(ret) && i < list_size_; ++i) {
    if (OB_FAIL(src[i].deep_copy(dst[i], allocator))) {
    }
  }
  return ret;
}

int ObParallelMergeInfo::init(
    common::ObIAllocator &allocator,
    const ObParallelMergeInfo &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("other parallel info is invalid", K(ret), K(other));
  } else {
    format_version_ = other.format_version_;
    list_size_ = other.list_size_;
    reserved_ = other.reserved_;
    if (list_size_ > 0) {
      ret = deep_copy_list(allocator, other.parallel_datum_rowkey_list_, parallel_datum_rowkey_list_);
      if (OB_FAIL(ret)) {
        destroy(allocator);
      }
    }
  }
  return ret;
}

int ObParallelMergeInfo::deep_copy_datum_rowkey(
    const int64_t idx,
    ObIAllocator &input_allocator,
    blocksstable::ObDatumRowkey &rowkey) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(idx < 0 || idx >= list_size_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid idx", KR(ret), K(idx), K_(list_size));
  } else {
    if (OB_FAIL(parallel_datum_rowkey_list_[idx].deep_copy(rowkey/*dst*/, input_allocator))) {
    }
  }
  return ret;
}

int64_t ObParallelMergeInfo::to_string(char* buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
  } else {
    J_OBJ_START();
    J_KV(K_(list_size), K_(format_version));
    if (list_size_ > 0) {
      J_COMMA();
    }

    for (int i = 0; i < list_size_; ++i) {
      if (i > 0) {
        J_COMMA();
      }
      J_KV(K(i), "key", parallel_datum_rowkey_list_[i]);
    }
    J_OBJ_END();
  }
  return pos;
}

OB_SERIALIZE_MEMBER_SIMPLE(
    ObMediumCompactionInfoKey,
    medium_snapshot_);

/*
 * ObMediumCompactionInfo
 * */
const char *ObMediumCompactionInfo::ObCompactionTypeStr[] = {
    "MEDIUM_COMPACTION",
    "MAJOR_COMPACTION",
};

const char *ObMediumCompactionInfo::get_compaction_type_str(enum ObCompactionType type)
{
  const char *str = "";
  if (is_valid_compaction_type(type)) {
    str = ObCompactionTypeStr[type];
  } else {
    str = "invalid_type";
  }
  return str;
}

ObMediumCompactionInfo::ObMediumCompactionInfo()
  : allocator_(nullptr)
{
  reset();
  STATIC_ASSERT(static_cast<int64_t>(COMPACTION_TYPE_MAX) == ARRAYSIZEOF(ObCompactionTypeStr), "compaction type str len is mismatch");
}

ObMediumCompactionInfo::ObMediumCompactionInfo(ObIAllocator &allocator)
  : allocator_(nullptr)
{
  reset();
  allocator_ = &allocator;
}

ObMediumCompactionInfo::~ObMediumCompactionInfo()
{
  reset();
}

int ObMediumCompactionInfo::assign(ObIAllocator &allocator,
                                   const ObMediumCompactionInfo &medium_info)
{
  return init(allocator, medium_info);
}

int ObMediumCompactionInfo::init(
    ObIAllocator &allocator,
    const ObMediumCompactionInfo &medium_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!medium_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(medium_info));
  } else if (FALSE_IT(allocator_ = &allocator)) {
  } else if (medium_info.contain_storage_schema()
          && OB_FAIL(storage_schema_.init(allocator, medium_info.storage_schema_))) {
    LOG_WARN("failed to init storage schema", K(ret), K(medium_info));
  } else if (OB_FAIL(parallel_merge_info_.init(allocator, medium_info.parallel_merge_info_))) {
  } else if (medium_info.contain_mds_filter_info_
      && OB_FAIL(mds_filter_info_.assign(allocator, medium_info.mds_filter_info_))) {
    LOG_WARN("failed to init mds filter info", K(ret), K(medium_info));
  } else {
    info_ = medium_info.info_;
    medium_snapshot_ = medium_info.medium_snapshot_;
    last_medium_snapshot_ = medium_info.last_medium_snapshot_;
    data_version_ = medium_info.data_version_;
    encoding_granularity_ = medium_info.encoding_granularity_;
  }
  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

bool ObMediumCompactionInfo::is_valid() const
{
  return MEDIUM_INFO_VERSION == format_version_
      && 0 == reserved1_
      && 0 == reserved2_
      && is_valid_compaction_type(static_cast<ObCompactionType>(compaction_type_))
      && medium_snapshot_ > 0
      && DATA_CURRENT_VERSION == data_version_
      && last_medium_snapshot_ > 0
      && (!contain_storage_schema() || storage_schema_.is_valid())
      && (!contain_parallel_range_ || (parallel_merge_info_.is_valid() && nullptr != allocator_))
      && (!contain_mds_filter_info_ || (mds_filter_info_.is_valid() && nullptr != allocator_));
}

void ObMediumCompactionInfo::reset()
{
  info_ = 0;
  format_version_ = MEDIUM_INFO_VERSION;
  compaction_type_ = COMPACTION_TYPE_MAX;
  contain_parallel_range_ = false;
  medium_merge_reason_ = ObAdaptiveMergePolicy::NONE;
  is_schema_changed_ = false;
  reserved1_ = 0;
  is_skip_database_major_ = false;
  contain_mds_filter_info_ = false;
  reserved2_ = 0;
  data_version_ = DATA_CURRENT_VERSION;
  medium_snapshot_ = 0;
  last_medium_snapshot_ = 0;
  encoding_granularity_ = 0;
  storage_schema_.reset();
  if (OB_NOT_NULL(allocator_)) {
    parallel_merge_info_.destroy(*allocator_);
    mds_filter_info_.destroy(*allocator_);
    allocator_ = nullptr;
  }
}

int ObMediumCompactionInfo::gene_parallel_info(
    ObArrayArray<ObStoreRange> &paral_range)
{
  int ret = OB_SUCCESS;
  contain_parallel_range_ = false;
  if (OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("allocator is not init", KR(ret), KP_(allocator));
  } else if (OB_FAIL(parallel_merge_info_.generate_from_range_array(*allocator_, paral_range))) {
    if (OB_UNLIKELY(OB_SIZE_OVERFLOW != ret)) {
      LOG_WARN("failed to generate parallel merge info", K(ret), K(paral_range));
    }
  } else if (parallel_merge_info_.get_size() > 0) {
    contain_parallel_range_ = true;
    LOG_INFO("success to gene parallel info", K(ret), K(contain_parallel_range_), K(parallel_merge_info_));
  }
  return ret;
}

bool ObMediumCompactionInfo::contain_storage_schema() const
{
  return true;
}

int ObMediumCompactionInfo::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == buf || buf_len <= 0 || pos < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(buf_len), K(pos));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("medium compaction info is invalid", K(ret), KPC(this));
  } else {
    LST_DO_CODE(
        OB_UNIS_ENCODE,
        info_,
        medium_snapshot_,
        data_version_);

    if (OB_SUCC(ret) && contain_storage_schema()) {
      LST_DO_CODE(OB_UNIS_ENCODE, storage_schema_);
    }

    if (OB_SUCC(ret) && contain_parallel_range_) {
      LST_DO_CODE(
          OB_UNIS_ENCODE,
          parallel_merge_info_);
    }
    LST_DO_CODE(
      OB_UNIS_ENCODE,
      last_medium_snapshot_,
      encoding_granularity_);
    if (OB_SUCC(ret) && contain_mds_filter_info_) {
      LST_DO_CODE(
        OB_UNIS_ENCODE,
        mds_filter_info_);
    }
  }
  return ret;
}

int ObMediumCompactionInfo::deserialize(
    common::ObIAllocator &allocator,
    const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == buf || data_len <= 0 || pos < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(data_len), K(pos));
  } else {
    allocator_ = &allocator;
    LST_DO_CODE(OB_UNIS_DECODE,
        info_,
        medium_snapshot_,
        data_version_);
    if (OB_FAIL(ret)) {
    } else if (OB_UNLIKELY(MEDIUM_INFO_VERSION != format_version_
                           || 0 != reserved1_
                           || 0 != reserved2_
                           || DATA_CURRENT_VERSION != data_version_)) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("medium compaction info format mismatch", K(ret), K_(format_version),
          K_(reserved1), K_(reserved2), K_(data_version));
    } else if (contain_storage_schema() && OB_FAIL(storage_schema_.deserialize(allocator, buf, data_len, pos))) {
      LOG_WARN("failed to deserialize storage schema", K(ret), K(buf), K(data_len), K(pos));
    } else if (contain_parallel_range_) {
      if (OB_FAIL(parallel_merge_info_.deserialize(allocator, buf, data_len, pos))) {
      }
    } else {
      clear_parallel_range();
    }
    LST_DO_CODE(
      OB_UNIS_DECODE,
      last_medium_snapshot_,
      encoding_granularity_);
    if (OB_FAIL(ret) || !contain_mds_filter_info_) {
    } else if (OB_FAIL(mds_filter_info_.deserialize(allocator, buf, data_len, pos))) {
    }
    if (OB_SUCC(ret) && OB_UNLIKELY(!is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("deserialized medium compaction info is invalid", K(ret), KPC(this));
    }
    if (OB_FAIL(ret)) {
      reset();
    }
  }
  return ret;
}

int64_t ObMediumCompactionInfo::get_serialize_size() const
{
  int64_t len = 0;
  LST_DO_CODE(
      OB_UNIS_ADD_LEN,
      info_,
      medium_snapshot_,
      data_version_);
  if (contain_storage_schema()) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, storage_schema_);
  }
  if (contain_parallel_range_) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, parallel_merge_info_);
  }
  LST_DO_CODE(
    OB_UNIS_ADD_LEN,
    last_medium_snapshot_,
    encoding_granularity_);
  if (contain_mds_filter_info_) {
    LST_DO_CODE(
      OB_UNIS_ADD_LEN,
      mds_filter_info_);
  }
  return len;
}

void ObMediumCompactionInfo::gene_info(
    char* buf, const int64_t buf_len, int64_t &pos) const
{
  if (OB_ISNULL(buf) || buf_len <= 0) {
    // do nothing
  } else {
    J_KV("compaction_type", ObMediumCompactionInfo::get_compaction_type_str((ObCompactionType)compaction_type_),
       "merge_reason", ObAdaptiveMergePolicy::merge_reason_to_str(medium_merge_reason_),
       K(medium_snapshot_), K_(last_medium_snapshot), K_(parallel_merge_info), K_(encoding_granularity), K_(contain_mds_filter_info));
    if (contain_mds_filter_info_) {
      J_COMMA();
      J_KV(K_(mds_filter_info));
    }
  }
}

int64_t ObMediumCompactionInfo::to_string(char* buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
  } else {
    J_OBJ_START();
    J_KV("compaction_type", ObMediumCompactionInfo::get_compaction_type_str((ObCompactionType)compaction_type_),
      "merge_reason", ObAdaptiveMergePolicy::merge_reason_to_str(medium_merge_reason_),
      K_(medium_snapshot), K_(last_medium_snapshot),
      K_(format_version), K_(data_version), K_(is_schema_changed), K_(storage_schema),
      K_(is_skip_database_major), K_(contain_parallel_range), K_(parallel_merge_info), K_(encoding_granularity));
    if (contain_mds_filter_info_) {
      J_COMMA();
      J_KV(K_(mds_filter_info));
    }
    J_OBJ_END();
  }
  return pos;
}

} //namespace compaction
} // namespace oceanbase
