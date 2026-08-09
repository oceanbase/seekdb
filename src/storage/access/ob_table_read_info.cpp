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
#include "ob_table_read_info.h"
#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"
#include "share/truncate_info/ob_truncate_info_util.h"
namespace oceanbase
{
using namespace common;
namespace storage
{
/*
 * ------------------------------- ObColumnIndexArray -------------------------------
 */
int64_t return_array_cnt(uint32_t schema_rowkey_cnt, const ObFixedMetaObjArray<int32_t> & array)
{
  return array.count();
}
int64_t return_schema_rowkey_cnt(uint32_t schema_rowkey_cnt, const ObFixedMetaObjArray<int32_t> & array)
{
  return schema_rowkey_cnt;
}
int32_t return_array_idx_for_memtable(uint32_t schema_rowkey_cnt, uint32_t column_cnt,
                         int64_t idx,
                         const ObFixedMetaObjArray<int32_t> &array)
{
  int32_t ret_val = 0;
  const int32_t extra_rowkey_cnt = storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  OB_ASSERT(idx >= 0 && idx < column_cnt);
  if (idx < schema_rowkey_cnt) {
    ret_val = idx;
  } else if (idx < schema_rowkey_cnt + extra_rowkey_cnt) {
    ret_val = OB_INVALID_INDEX;
  } else {
    ret_val = idx - extra_rowkey_cnt;
  }
  return ret_val;
}
int32_t return_idx(uint32_t schema_rowkey_cnt, uint32_t column_cnt,
                         int64_t idx,
                         const ObFixedMetaObjArray<int32_t> &array)
{
  OB_ASSERT(idx >= 0 && idx < column_cnt);
  return (int32_t)idx;
}
int32_t return_array_idx(uint32_t schema_rowkey_cnt, uint32_t column_cnt,
                         int64_t idx,
                         const ObFixedMetaObjArray<int32_t> &array)
{
  OB_ASSERT(idx >= 0 && idx < array.count());
  return array[idx];
}
ObColumnIndexArray::ObColumnIndexArray(const bool rowkey_mode /* = false*/,
                                       const bool for_memtable /* = false*/)
    : version_(COLUMN_INDEX_ARRAY_VERSION),
      rowkey_mode_(rowkey_mode),
      for_memtable_(for_memtable),
      reserved_(0),
      schema_rowkey_cnt_(0),
      column_cnt_(0),
      array_()
{
  if (rowkey_mode) {
    if (for_memtable) { // no multi_version rowkey in memtable
      at_func_ = return_array_idx_for_memtable;
    } else {
      at_func_ = return_idx;
    }
    count_func_ = return_schema_rowkey_cnt;
  } else {
    at_func_ = return_array_idx;
    count_func_ = return_array_cnt;
  }
}

int64_t ObColumnIndexArray::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
  } else {
    J_OBJ_START();
    J_KV(K_(rowkey_mode), K_(for_memtable));
    if (rowkey_mode_) {
      J_COMMA();
      J_KV(K_(schema_rowkey_cnt), K_(column_cnt));
    } else {
      J_COMMA();
      J_KV(K_(array));
    }
    J_OBJ_END();
  }
  return pos;
}

int ObColumnIndexArray::init(const int64_t count, const int64_t schema_rowkey_cnt, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (rowkey_mode_) {
    column_cnt_ = count;
    schema_rowkey_cnt_ = schema_rowkey_cnt;
  } else {
    if (OB_FAIL(array_.init(count, allocator))) {
    } else if (OB_FAIL(array_.prepare_allocate(count))) {
    }
  }
  return ret;
}

int ObColumnIndexArray::init_and_assign(const ObIArray<int32_t> &other, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(array_.init_and_assign(other, allocator))) {
  }
  return ret;
}

int64_t ObColumnIndexArray::get_deep_copy_size() const
{
  return rowkey_mode_ ? 0 : array_.get_deep_copy_size();
}

int ObColumnIndexArray::deep_copy(
    char *dst_buf,
    const int64_t buf_size,
    int64_t &pos,
    ObColumnIndexArray &dst_array) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cur column index array is invalid", K(ret), KPC(this));
  } else {
    dst_array.version_ = version_;
    dst_array.rowkey_mode_ = rowkey_mode_;
    dst_array.for_memtable_ = for_memtable_;
    dst_array.schema_rowkey_cnt_ = schema_rowkey_cnt_;
    dst_array.column_cnt_ = column_cnt_;
    if (!rowkey_mode_ && OB_FAIL(array_.deep_copy(dst_buf, buf_size, pos, dst_array.array_))) {
      LOG_WARN("failed to deep copy", K(ret));
    }
  }
  return ret;
}

int ObColumnIndexArray::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
    version_,
    rowkey_mode_,
    for_memtable_);
  if (OB_FAIL(ret)) {
  } else if (rowkey_mode_) {
    LST_DO_CODE(OB_UNIS_ENCODE, schema_rowkey_cnt_, column_cnt_);
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("for non-rowkey-mode, should not use serialize func", K(ret), K(rowkey_mode_));
  }
  return ret;
}

int ObColumnIndexArray::deserialize(const char *buf, const int64_t data_len, int64_t &pos, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  reset();
  bool tmp_rowkey_mode = false;
  bool tmp_for_memtable = false;
  LST_DO_CODE(OB_UNIS_DECODE,
    version_,
    tmp_rowkey_mode,
    tmp_for_memtable);

  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(rowkey_mode_ != tmp_rowkey_mode || for_memtable_ != tmp_for_memtable)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("deserialize info is different from cur array", KR(ret), K(rowkey_mode_), K(tmp_rowkey_mode),
      K(for_memtable_), K(tmp_for_memtable));
  } else if (rowkey_mode_) {
    LST_DO_CODE(OB_UNIS_DECODE, schema_rowkey_cnt_, column_cnt_);
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("for non-rowkey-mode, should not use deserialize func", K(ret), K(rowkey_mode_));
  }
  return ret;
}

int64_t ObColumnIndexArray::get_serialize_size() const
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
    version_,
    rowkey_mode_,
    for_memtable_);
  if (rowkey_mode_) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, schema_rowkey_cnt_, column_cnt_);
  } else {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "for non-rowkey-mode, should not use serialize func", K(rowkey_mode_));
  }
  return len;
}

/*
 * ------------------------------- ObReadInfoStruct -------------------------------
 */
void ObReadInfoStruct::reset()
{
  is_inited_ = false;
  allocator_ = nullptr;
  schema_column_count_ = 0;
  format_version_ = READ_INFO_FORMAT_VERSION;
  reserved_ = 0;
  schema_rowkey_cnt_ = 0;
  rowkey_cnt_ = 0;
  cols_desc_.reset();
  cols_index_.reset();
  memtable_cols_index_.reset();
  datum_utils_.reset();
}

void ObReadInfoStruct::init_basic_info(const int64_t schema_column_count,
                     const int64_t schema_rowkey_cnt,
                     const bool is_global_index_table) {
  schema_column_count_ = schema_column_count;
  schema_rowkey_cnt_ = schema_rowkey_cnt;
  rowkey_cnt_ = schema_rowkey_cnt + storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  is_global_index_table_ = is_global_index_table;
}

int64_t ObReadInfoStruct::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
  } else {
    J_OBJ_START();
    J_KV(K_(is_inited), K_(format_version),
        K_(schema_column_count),
        K_(schema_rowkey_cnt),
        K_(rowkey_cnt),
        K_(cols_index),
        K_(cols_desc),
        K_(datum_utils),
        K_(memtable_cols_index));
    J_OBJ_END();
  }
  return pos;
}

/*
 * ------------------------------- ObTableReadInfo -------------------------------
 */
ObTableReadInfo::ObTableReadInfo()
  : ObReadInfoStruct(false/*rowkey_mode*/),
    trans_col_index_(OB_INVALID_INDEX),
    group_idx_col_index_(OB_INVALID_INDEX),
    seq_read_column_count_(0),
    max_col_index_(-1),
    cols_param_(),
    cols_extend_(),
    mock_sstable_query_(false),
    need_truncate_filter_(false)
{
}

ObTableReadInfo::~ObTableReadInfo()
{
  reset();
}

int ObTableReadInfo::init_pre_check(
    const int64_t schema_column_count,
    const int64_t schema_rowkey_cnt,
    const common::ObIArray<ObColDesc> &cols_desc,
    const common::ObIArray<int32_t> *storage_cols_index,
    const common::ObIArray<ObColumnParam *> *cols_param,
    const common::ObIArray<ObColExtend> *cols_extend)
{
  int ret = OB_SUCCESS;
  const int64_t out_cols_cnt = cols_desc.count();
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), KPC(this));
  } else if (OB_UNLIKELY(schema_rowkey_cnt < 0
      || schema_column_count < 0
      || out_cols_cnt < schema_rowkey_cnt
      || out_cols_cnt > OB_ROW_MAX_COLUMNS_COUNT
      || (nullptr != storage_cols_index && storage_cols_index->count() != cols_desc.count())
      || (nullptr != cols_param && cols_param->count() != cols_desc.count())
      || (nullptr != cols_extend && cols_extend->count() != cols_desc.count()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(schema_rowkey_cnt), K(schema_column_count),
             K(cols_desc.count()), KPC(storage_cols_index), KPC(cols_param),
             KPC(cols_extend));
  }
  return ret;
}

int ObTableReadInfo::init(
    common::ObIAllocator &allocator,
    const int64_t schema_column_count,
    const int64_t schema_rowkey_cnt,
    const common::ObIArray<ObColDesc> &cols_desc,
    const common::ObIArray<int32_t> *storage_cols_index,
    const common::ObIArray<ObColumnParam *> *cols_param,
    const common::ObIArray<ObColExtend> *cols_extend,
    const bool need_truncate_filter)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init_pre_check(schema_column_count, schema_rowkey_cnt, cols_desc,
                             storage_cols_index, cols_param, cols_extend))) {
  } else if (FALSE_IT(init_basic_info(schema_column_count, schema_rowkey_cnt,
      false/*is_global_index_table*/))) { // init basic info
  } else if (OB_FAIL(ObReadInfoStruct::prepare_arrays(allocator, cols_desc, cols_desc.count()))) {
  } else if (nullptr != cols_param && OB_FAIL(cols_param_.init_and_assign(*cols_param, allocator))) {
    LOG_WARN("Fail to assign cols_param", K(ret));
  } else if (nullptr != cols_extend && OB_FAIL(cols_extend_.init_and_assign(*cols_extend, allocator))) {
    LOG_WARN("Fail to assign cols_extend", K(ret));
  } else if (FALSE_IT(inner_gene_cols_index_by_col_descs(schema_rowkey_cnt, cols_desc, storage_cols_index))) {
  } else if (OB_FAIL(init_datum_utils(allocator))) {
  } else {
    need_truncate_filter_ = need_truncate_filter;
    is_inited_ = true;
  }
  if (OB_FAIL(ret) && OB_INIT_TWICE != ret) {
    reset();
  }
  return ret;
}

void ObTableReadInfo::inner_gene_cols_index_by_col_descs(
    const int64_t schema_rowkey_cnt,
    const common::ObIArray<ObColDesc> &cols_desc,
    const common::ObIArray<int32_t> *storage_cols_index)
{
  const int64_t out_cols_cnt = cols_desc.count();
  int32_t col_index = OB_INVALID_INDEX;
  const int64_t trans_version_col_idx = ObMultiVersionRowkeyHelpper::get_trans_version_col_store_index(
        schema_rowkey_cnt, true);
  const int64_t sql_sequence_col_idx = ObMultiVersionRowkeyHelpper::get_sql_sequence_col_store_index(
        schema_rowkey_cnt, true);
  const int64_t extra_rowkey_cnt = storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  for (int64_t i = 0; i < out_cols_cnt; i++) {
    col_index = (nullptr == storage_cols_index) ? i : storage_cols_index->at(i);
    // memtable do not involve the multi version column
    memtable_cols_index_.array_[i] = col_index;
    if (i < schema_rowkey_cnt) {
      // continue
    } else if (OB_INVALID_INDEX == col_index) {
      if (common::OB_HIDDEN_TRANS_VERSION_COLUMN_ID == cols_desc.at(i).col_id_) {
        trans_col_index_ = i;
        col_index = trans_version_col_idx;
      } else if (common::OB_HIDDEN_SQL_SEQUENCE_COLUMN_ID == cols_desc.at(i).col_id_) {
        col_index = sql_sequence_col_idx;
      } else if (common::OB_HIDDEN_GROUP_IDX_COLUMN_ID == cols_desc.at(i).col_id_) {
        group_idx_col_index_ = i;
        col_index = -1;
      } else {
        col_index = -1;
      }
    } else {
      col_index = col_index + extra_rowkey_cnt;
    }
    cols_index_.array_[i] = col_index;
  }
}

int ObReadInfoStruct::prepare_arrays(
  common::ObIAllocator &allocator,
  const common::ObIArray<ObColDesc> &cols_desc,
  const int64_t out_cols_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(cols_desc_.init_and_assign(cols_desc, allocator))) {
  } else if (OB_FAIL(cols_index_.init(out_cols_cnt, schema_rowkey_cnt_, allocator))) {
  } else if (OB_FAIL(memtable_cols_index_.init(out_cols_cnt, schema_rowkey_cnt_, allocator))) {
  }
  return ret;
}

int ObTableReadInfo::init_datum_utils(common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  seq_read_column_count_ = 0;
  while (seq_read_column_count_ < cols_index_.count() &&
         cols_index_.at(seq_read_column_count_) == seq_read_column_count_) {
    seq_read_column_count_++;
  }
  max_col_index_ = -1;
  for (int64_t i = 0; i < cols_index_.count(); i++) {
    if (cols_index_.at(i) > max_col_index_) {
      max_col_index_ = cols_index_.at(i);
    } else if (-1 == cols_index_.at(i)) {
      max_col_index_ = INT64_MAX;
    }
  }
  for (int64_t i = 0; i < cols_param_.count(); i++) {
    if (cols_param_.at(i)->get_meta_type().is_decimal_int()) {
      cols_desc_.at(i).col_type_.set_stored_precision(cols_param_.at(i)->get_accuracy().get_precision());
      cols_desc_.at(i).col_type_.set_scale(cols_param_.at(i)->get_accuracy().get_scale());
    } else if (ob_is_real_type(cols_param_.at(i)->get_meta_type().get_type())) {
      cols_desc_.at(i).col_type_.set_scale(cols_param_.at(i)->get_accuracy().get_scale());
    }
  }
  if (OB_FAIL(datum_utils_.init(cols_desc_, schema_rowkey_cnt_, allocator))) {
  }
  return ret;
}

void ObTableReadInfo::reset()
{
  ObReadInfoStruct::reset();
  trans_col_index_ = OB_INVALID_INDEX;
  group_idx_col_index_ = OB_INVALID_INDEX;
  seq_read_column_count_ = 0;
  max_col_index_ = -1;
  cols_param_.reset();
  cols_extend_.reset();
  memtable_cols_index_.reset();
  mock_sstable_query_ = false;
  need_truncate_filter_ = false;
}

/*
  be careful to deal with cols_index_/memtable_cols_index_ when (de)serialize
  for compat, only serialize arrays
*/
int ObTableReadInfo::serialize(
    char *buf,
    const int64_t buf_len,
    int64_t &pos) const
{
  int ret = OB_SUCCESS;

  LST_DO_CODE(OB_UNIS_ENCODE,
              info_,
              schema_rowkey_cnt_,
              rowkey_cnt_,
              trans_col_index_,
              group_idx_col_index_,
              seq_read_column_count_,
              cols_desc_,
              cols_index_.array_,
              memtable_cols_index_.array_);
  if (OB_SUCC(ret)) {
    if (OB_FAIL(serialization::encode_vi64(buf, buf_len, pos, cols_param_.count()))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < cols_param_.count(); ++i) {
      if (OB_ISNULL(cols_param_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", K(ret), K(i));
      } else if (OB_FAIL(cols_param_.at(i)->serialize(buf, buf_len, pos))) {
      }
    }
  }

  if (OB_SUCC(ret)) {
    LST_DO_CODE(OB_UNIS_ENCODE, cols_extend_);
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(serialization::encode_bool(buf, buf_len, pos, need_truncate_filter_))) {
    }
  }
  return ret;
}

int ObTableReadInfo::deserialize(
    common::ObIAllocator &allocator,
    const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  reset();
  LST_DO_CODE(OB_UNIS_DECODE,
              info_,
              schema_rowkey_cnt_,
              rowkey_cnt_,
              trans_col_index_,
              group_idx_col_index_,
              seq_read_column_count_);
  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(READ_INFO_FORMAT_VERSION != format_version_)) {
    ret = OB_VERSION_NOT_MATCH;
    LOG_WARN("table read info format version mismatch", K(ret), K_(format_version), K(READ_INFO_FORMAT_VERSION));
  } else if (OB_FAIL(cols_desc_.deserialize(buf, data_len, pos, allocator))) {
  } else if (FALSE_IT(cols_index_.rowkey_mode_ = false)) {
  } else if (OB_FAIL(cols_index_.array_.deserialize(buf, data_len, pos, allocator))) {
  } else if (FALSE_IT(memtable_cols_index_.rowkey_mode_ = false)) {
  } else if (OB_FAIL(memtable_cols_index_.array_.deserialize(buf, data_len, pos, allocator))) {
  }
  if (OB_SUCC(ret)) {
    int64_t column_param_cnt = 0;
    if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &column_param_cnt))) {
    } else if (column_param_cnt > 0) {
      ObColumnParam **column = NULL;
      void *tmp_ptr  = NULL;
      if (OB_ISNULL(tmp_ptr = allocator.alloc(column_param_cnt * sizeof(ObColumnParam *)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("Fail to alloc", K(ret), K(column_param_cnt));
      } else if (FALSE_IT(column = static_cast<ObColumnParam **>(tmp_ptr))) {
        // not reach
      } else {
        ObArray<ObColumnParam *> tmp_columns;
        for (int64_t i = 0; OB_SUCC(ret) && i < column_param_cnt; ++i) {
          ObColumnParam *&cur_column = column[i];
          cur_column = nullptr;
          if (OB_ISNULL(tmp_ptr = allocator.alloc(sizeof(ObColumnParam)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("alloc failed", K(ret));
          } else if (FALSE_IT(cur_column = new (tmp_ptr) ObColumnParam(allocator))) {
          } else if (OB_FAIL(cur_column->deserialize(buf, data_len, pos))) {
          } else if (OB_FAIL(tmp_columns.push_back(cur_column))) {
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(cols_param_.init_and_assign(tmp_columns, allocator))) {
          LOG_WARN("Fail to add columns", K(ret));
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(cols_extend_.deserialize(buf, data_len, pos, allocator))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(serialization::decode_bool(buf, data_len, pos, &need_truncate_filter_))) {
    }
  }

  if (OB_SUCC(ret) && cols_desc_.count() > 0) {
    max_col_index_ = -1;
    for (int64_t i = 0; i < cols_index_.count(); i++) {
      if (cols_index_.at(i) > max_col_index_) {
        max_col_index_ = cols_index_.at(i);
      } else if (-1 == cols_index_.at(i)) {
        max_col_index_ = INT64_MAX;
      }
    }
    if (OB_FAIL(datum_utils_.init(cols_desc_, schema_rowkey_cnt_, allocator, false))) {
    } else {
      is_inited_ = true;
    }
  }

  if (OB_FAIL(ret)) {
    reset();
  }

  return ret;
}


int64_t ObTableReadInfo::get_serialize_size() const
{
  int ret = OB_SUCCESS;
  int64_t len = 0;

  LST_DO_CODE(OB_UNIS_ADD_LEN,
              info_,
              schema_rowkey_cnt_,
              rowkey_cnt_,
              trans_col_index_,
              group_idx_col_index_,
              seq_read_column_count_,
              cols_desc_,
              cols_index_.array_,
              memtable_cols_index_.array_);

  if (OB_SUCC(ret)) {
    len += serialization::encoded_length_vi64(cols_param_.count());
    for (int64_t i = 0; OB_SUCC(ret) && i < cols_param_.count(); ++i) {
      if (OB_ISNULL(cols_param_.at(i))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", K(ret), K(i));
      } else {
        len += cols_param_.at(i)->get_serialize_size();
      }
    }
  }
  if (OB_SUCC(ret)) {
    LST_DO_CODE(OB_UNIS_ADD_LEN, cols_extend_);
  }
  if (OB_SUCC(ret)) {
    len += serialization::encoded_length_bool(need_truncate_filter_);
  }
  return len;
}

int64_t ObTableReadInfo::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  if (OB_ISNULL(buf) || buf_len <= 0) {
  } else {
    J_OBJ_START();
    J_KV(K_(schema_column_count),
        K_(schema_rowkey_cnt),
        K_(rowkey_cnt),
        K_(trans_col_index),
        K_(group_idx_col_index),
        K_(seq_read_column_count),
        K_(max_col_index),
        K_(cols_index),
        K_(memtable_cols_index),
        K_(cols_desc),
        K_(cols_extend),
        K_(need_truncate_filter));
        //K_(datum_utils),
        //"cols_param",
        //ObArrayWrap<ObColumnParam *>(0 == cols_param_.count() ? NULL : &cols_param_.at(0),
        //                              cols_param_.count()));
    J_OBJ_END();
  }
  return pos;
}

/*
 * ------------------------------- ObRowkeyReadInfo -------------------------------
 */
ObRowkeyReadInfo::ObRowkeyReadInfo()
  : ObReadInfoStruct(true/*rowkey_mode*/)
{
#if defined(__x86_64__)
  static_assert(sizeof(ObRowkeyReadInfo) <= 480, "The size of ObRowkeyReadInfo will affect the meta memory manager, and the necessity of adding new fields needs to be considered.");
#endif
}

int ObRowkeyReadInfo::init(
    common::ObIAllocator &allocator,
    const int64_t schema_column_count,
    const int64_t schema_rowkey_cnt,
    const common::ObIArray<ObColDesc> &rowkey_col_descs,
    const bool is_global_index_table)
{
  int ret = OB_SUCCESS;
  const int64_t extra_rowkey_cnt = storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  const int64_t out_cols_cnt = schema_column_count + extra_rowkey_cnt;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), KPC(this));
  } else if (OB_UNLIKELY(0 > schema_rowkey_cnt
    || schema_column_count > OB_ROW_MAX_COLUMNS_COUNT
    || schema_rowkey_cnt > schema_column_count)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(schema_rowkey_cnt), K(rowkey_col_descs.count()), K(out_cols_cnt), K(schema_column_count));
  }
  if (OB_SUCC(ret)) {
    init_basic_info(schema_column_count, schema_rowkey_cnt,
                    is_global_index_table); // init basic info
    if (OB_FAIL(prepare_arrays(allocator, rowkey_col_descs, out_cols_cnt))) {
    } else if (OB_FAIL(datum_utils_.init(cols_desc_, schema_rowkey_cnt_,
                                         allocator))) {
    } else {
      is_inited_ = true;
    }
    if (OB_FAIL(ret)) {
      reset();
    }
  }
  return ret;
}

int64_t ObRowkeyReadInfo::get_request_count() const
{
  return schema_column_count_ + storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
}

int64_t ObRowkeyReadInfo::get_deep_copy_size() const
{
  return sizeof(ObRowkeyReadInfo)
      + cols_desc_.get_deep_copy_size()
      + cols_index_.get_deep_copy_size()
      + memtable_cols_index_.get_deep_copy_size()
      + datum_utils_.get_deep_copy_size();
}

int ObRowkeyReadInfo::deep_copy(char *buf, const int64_t buf_len, ObRowkeyReadInfo *&value) const
{
  int ret = OB_SUCCESS;
  const int64_t memory_size = get_deep_copy_size();
  if (OB_ISNULL(buf) || OB_UNLIKELY(buf_len < memory_size)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalue argument", K(ret), KP(buf), K(buf_len), K(memory_size));
  } else {
    ObRowkeyReadInfo *dst_value = new (buf) ObRowkeyReadInfo();
    int64_t pos = sizeof(ObRowkeyReadInfo);
    dst_value->info_ = info_;
    dst_value->schema_rowkey_cnt_ = schema_rowkey_cnt_;
    dst_value->rowkey_cnt_ = rowkey_cnt_;
    // can not deep copy cols param cuz ObColumnParam need an allocator on constructor for default value
    if (OB_FAIL(cols_desc_.deep_copy(buf, buf_len, pos, dst_value->cols_desc_))) {
    } else if (OB_FAIL(cols_index_.deep_copy(buf, buf_len, pos, dst_value->cols_index_))) {
    } else if (OB_FAIL(memtable_cols_index_.deep_copy(buf, buf_len, pos, dst_value->memtable_cols_index_))) {
    } else if (OB_FAIL(dst_value->datum_utils_.init(
        cols_desc_, schema_rowkey_cnt_, buf_len - pos, buf + pos))) {
    } else {
      pos += datum_utils_.get_deep_copy_size();
      dst_value->is_inited_ = is_inited_;
      value = dst_value;
    }
  }
  return ret;
}


int ObRowkeyReadInfo::serialize(
    char *buf,
    const int64_t buf_len,
    int64_t &pos) const
{
  int ret = OB_SUCCESS;

  LST_DO_CODE(OB_UNIS_ENCODE,
              info_,
              schema_rowkey_cnt_,
              rowkey_cnt_,
              cols_desc_,
              cols_index_,
              memtable_cols_index_);
  return ret;
}

int ObRowkeyReadInfo::deserialize(
    common::ObIAllocator &allocator,
    const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  reset();
  LST_DO_CODE(OB_UNIS_DECODE,
              info_,
              schema_rowkey_cnt_,
              rowkey_cnt_);
  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(READ_INFO_FORMAT_VERSION != format_version_)) {
    ret = OB_VERSION_NOT_MATCH;
    LOG_WARN("rowkey read info format version mismatch", K(ret), K_(format_version), K(READ_INFO_FORMAT_VERSION));
  } else if (OB_FAIL(cols_desc_.deserialize(buf, data_len, pos, allocator))) {
  } else if (OB_FAIL(cols_index_.deserialize(buf, data_len, pos, allocator))) {
  } else if (OB_FAIL(memtable_cols_index_.deserialize(buf, data_len, pos, allocator))) {
  }

  if (OB_SUCC(ret) && cols_desc_.count() > 0) {
    if (OB_FAIL(datum_utils_.init(cols_desc_, schema_rowkey_cnt_, allocator, false))) {
    } else {
      is_inited_ = true;
    }
  }
  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

int64_t ObRowkeyReadInfo::get_serialize_size() const
{
  int ret = OB_SUCCESS;
  int64_t len = 0;

  LST_DO_CODE(OB_UNIS_ADD_LEN,
              info_,
              schema_rowkey_cnt_,
              rowkey_cnt_,
              cols_desc_,
              cols_index_,
              memtable_cols_index_);
  return len;
}


}
}
