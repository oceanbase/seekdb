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

#include "ob_storage_schema.h"
#include "data_plane/lob/ob_lob_value.h"

namespace oceanbase
{

using namespace common;
using namespace share::schema;

namespace storage
{
ERRSIM_POINT_DEF(EN_NOT_TRIM_FOR_DEFALUT_CHECKSUM);

/*
 * ObStorageRowkeyColumnSchema
 * */
ObStorageRowkeyColumnSchema::ObStorageRowkeyColumnSchema()
{
  reset();
}

ObStorageRowkeyColumnSchema::~ObStorageRowkeyColumnSchema()
{
}

void ObStorageRowkeyColumnSchema::reset()
{
  info_ = 0;
  column_idx_ = 0;
  meta_type_.reset();
}

bool ObStorageRowkeyColumnSchema::is_valid() const
{
  return 0 == reserved_
      && 0 != column_idx_
      && common::ob_is_valid_obj_type(static_cast<ObObjType>(meta_type_.get_type()));
}

OB_SERIALIZE_MEMBER_SIMPLE(
    ObStorageRowkeyColumnSchema,
    info_,
    column_idx_,
    meta_type_);

/*
 * ObStorageColumnSchema
 * */

ObStorageColumnSchema::ObStorageColumnSchema()
{
  reset();
}

ObStorageColumnSchema::~ObStorageColumnSchema()
{
}

void ObStorageColumnSchema::reset()
{
  info_ = 0;
  default_checksum_ = 0;
  meta_type_.reset();
  orig_default_value_.reset();
}

void ObStorageColumnSchema::destroy(ObIAllocator &allocator)
{
  if (orig_default_value_.get_deep_copy_size() > 0) {
    void *ptr = orig_default_value_.get_deep_copy_obj_ptr();
    if (ptr != nullptr) {
      orig_default_value_.reset();
      allocator.free(ptr);
    }
  }
  reset();
}

bool ObStorageColumnSchema::is_valid() const
{
  return 0 == reserved_
      && common::ob_is_valid_obj_type(static_cast<ObObjType>(meta_type_.get_type()));
}

int ObStorageColumnSchema::deep_copy_default_val(ObIAllocator &allocator, const ObObj &default_val)
{
  int ret = OB_SUCCESS;
  if (default_val.get_deep_copy_size() > 0) {
    char *buf = nullptr;
    int64_t pos = 0;
    const int64_t alloc_size = default_val.get_deep_copy_size();
    if (OB_ISNULL(buf = (char *)allocator.alloc(alloc_size))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "failed to alloc memory", K(ret), K(alloc_size));
    } else if (OB_FAIL(orig_default_value_.deep_copy(default_val, buf, alloc_size, pos))) {
      orig_default_value_.reset();
      allocator.free(buf);
      buf = nullptr;
      STORAGE_LOG(WARN, "failed to deep copy", K(ret), K(default_val), K(pos));
    }
  } else {
    orig_default_value_ = default_val;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(
    ObStorageColumnSchema,
    info_,
    default_checksum_,
    meta_type_,
    orig_default_value_);

/*
 * ObStorageSchema
 */

ObStorageSchema::ObStorageSchema()
  : allocator_(nullptr),
    format_version_(STORAGE_SCHEMA_FORMAT_VERSION),
    info_(0),
    table_type_(ObTableType::MAX_TABLE_TYPE),
    table_mode_(),
    index_type_(ObIndexType::INDEX_TYPE_IS_NOT),
    row_store_type_(ObStoreFormat::get_default_row_store_type()),
    schema_version_(OB_INVALID_VERSION),
    column_cnt_(0),
    tablet_size_(OB_DEFAULT_TABLET_SIZE),
    pctfree_(OB_DEFAULT_PCTFREE),
    block_size_(0),
    progressive_merge_round_(0),
    progressive_merge_num_(0),
    compressor_type_(ObCompressorType::NONE_COMPRESSOR),
    rowkey_array_(),
    column_array_(),
    skip_idx_attr_array_(),
    store_column_cnt_(0),
    semistruct_encoding_type_(),
    is_inited_(false)
{
}

ObStorageSchema::~ObStorageSchema()
{
  reset();
}

int ObStorageSchema::init(
    common::ObIAllocator &allocator,
    const ObTableSchema &input_schema,
    const bool skip_column_info/* = false*/)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "init twice", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!input_schema.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid args", K(ret), K(input_schema), K(skip_column_info));
  } else if (OB_FAIL(copy_from(input_schema))) {
  } else if (FALSE_IT(column_info_simplified_ = skip_column_info)) {
  } else {
    allocator_ = &allocator;
    rowkey_array_.set_allocator(&allocator);
    column_array_.set_allocator(&allocator);
    skip_idx_attr_array_.set_allocator(&allocator);
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(generate_column_array(input_schema))) {
  }

  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(!ObStorageSchema::is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(ERROR, "storage schema is invalid", K(ret));
  } else {
    is_inited_ = true;
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }
  return ret;
}

int ObStorageSchema::init(
    common::ObIAllocator &allocator,
    const ObStorageSchema &old_schema,
    const bool skip_column_info/* = false*/,
    const int64_t stored_column_count/* = -1*/)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "init twice", K(ret), K_(is_inited));
  } else if (OB_UNLIKELY(!old_schema.is_valid()
                         || 0 == stored_column_count
                         || stored_column_count > old_schema.store_column_cnt_)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid args", K(ret), K(old_schema), K(skip_column_info), K(stored_column_count));
  } else if (OB_FAIL(copy_from(old_schema))) {
  } else if (FALSE_IT(column_info_simplified_ = (skip_column_info || old_schema.column_info_simplified_))) { 
  } else if (OB_UNLIKELY(stored_column_count > 0 && column_info_simplified_)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "cannot truncate a simplified schema", K(ret), K(old_schema), K(stored_column_count));
  }
  
  if (OB_FAIL(ret)) {
  } else {
    allocator_ = &allocator;
    rowkey_array_.set_allocator(&allocator);
    column_array_.set_allocator(&allocator);
    skip_idx_attr_array_.set_allocator(&allocator);

    format_version_ = STORAGE_SCHEMA_FORMAT_VERSION;
    compressor_type_ = old_schema.compressor_type_;
    column_cnt_ = old_schema.column_cnt_;
    store_column_cnt_ = old_schema.store_column_cnt_;

    if (OB_FAIL(rowkey_array_.reserve(old_schema.rowkey_array_.count()))) {
    } else if (OB_FAIL(rowkey_array_.assign(old_schema.rowkey_array_))) {
    } else if (OB_FAIL(skip_idx_attr_array_.reserve(old_schema.skip_idx_attr_array_.count()))) {
    } else if (OB_FAIL(skip_idx_attr_array_.assign(old_schema.skip_idx_attr_array_))) {
    } else if (!column_info_simplified_ && OB_FAIL(deep_copy_column_array(allocator, old_schema, old_schema.column_array_.count()))) {
      STORAGE_LOG(WARN, "failed to deep copy column array", K(ret), K(old_schema));
    }

    if (OB_FAIL(ret)) {
    } else if (stored_column_count > 0 && OB_FAIL(truncate_column_array(stored_column_count))) {
      STORAGE_LOG(WARN, "failed to truncate column array", K(ret), K(old_schema), K(stored_column_count));
    } else if (OB_UNLIKELY(!is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(ERROR, "storage schema is invalid", K(ret));
    } else {
      is_inited_ = true;
    }
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }

  return ret;
}

int ObStorageSchema::deep_copy_column_array(
    common::ObIAllocator &allocator,
    const ObStorageSchema &src_schema,
    const int64_t copy_array_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(copy_array_cnt <= 0 || copy_array_cnt > src_schema.column_array_.count())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), K(copy_array_cnt), K(src_schema.column_array_));
  } else if (OB_FAIL(column_array_.reserve(copy_array_cnt))) {
  }
  for (int i = 0; OB_SUCC(ret) && i < copy_array_cnt; ++i) {
    ObStorageColumnSchema col_schema;
    const ObStorageColumnSchema &src_col_schema = src_schema.column_array_.at(i);
    col_schema.info_ = src_col_schema.info_;
    col_schema.default_checksum_ = src_col_schema.default_checksum_;
    col_schema.meta_type_ = src_col_schema.meta_type_;
    if (OB_FAIL(col_schema.deep_copy_default_val(allocator, src_col_schema.orig_default_value_))) {
    } else if (OB_FAIL(column_array_.push_back(col_schema))) {
      STORAGE_LOG(WARN, "failed to push back col schema", K(ret), K(i), K(copy_array_cnt),
          K(src_schema.column_array_.count()), K(col_schema));
      col_schema.destroy(allocator);
    }
  }
  return ret;
}

int ObStorageSchema::truncate_column_array(const int64_t stored_column_count)
{
  int ret = OB_SUCCESS;
  int64_t array_count = -1;
  int64_t current_stored_count = 0;
  if (OB_UNLIKELY(stored_column_count <= 0 || stored_column_count > store_column_cnt_)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid stored column count", K(ret), K(stored_column_count), K_(store_column_cnt));
  } else {
    for (int64_t i = 0; i < column_array_.count(); ++i) {
      if (column_array_.at(i).is_column_stored_in_sstable()
          && ++current_stored_count == stored_column_count) {
        array_count = i + 1;
        break;
      }
    }
    if (array_count < 0) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "failed to locate stored column boundary", K(ret), K(stored_column_count), K_(column_array));
    } else {
      while (column_array_.count() > array_count) {
        column_array_.pop_back();
      }
      while (!skip_idx_attr_array_.empty()
             && skip_idx_attr_array_.at(skip_idx_attr_array_.count() - 1).col_idx_ >= array_count) {
        skip_idx_attr_array_.pop_back();
      }
      column_cnt_ = array_count;
      store_column_cnt_ = stored_column_count;
    }
  }
  return ret;
}

void ObStorageSchema::reset()
{
  format_version_ = STORAGE_SCHEMA_FORMAT_VERSION;
  info_ = 0;
  table_type_ = MAX_TABLE_TYPE;
  table_mode_.reset();
  index_type_ = INDEX_TYPE_IS_NOT;
  row_store_type_ = ObStoreFormat::get_default_row_store_type();
  schema_version_ = OB_INVALID_VERSION;
  column_cnt_ = 0;
  store_column_cnt_ = 0;
  tablet_size_ = OB_DEFAULT_TABLET_SIZE;
  pctfree_ = OB_DEFAULT_PCTFREE;
  block_size_ = 0;
  progressive_merge_round_ = 0;
  progressive_merge_num_ = 0;
  compressor_type_ = ObCompressorType::NONE_COMPRESSOR;
  if (nullptr != allocator_) {
    rowkey_array_.reset();
    for (int i = 0; i < column_array_.count(); ++i) {
      column_array_.at(i).destroy(*allocator_);
    }
    column_array_.reset();
    skip_idx_attr_array_.reset();
    allocator_ = nullptr;
  }
  semistruct_encoding_type_.reset();
  is_inited_ = false;
}

bool ObStorageSchema::is_valid() const
{
  bool valid_ret = true;
  if (STORAGE_SCHEMA_FORMAT_VERSION != format_version_
      || 0 != reserved1_
      || 0 != reserved2_
      || nullptr == allocator_
      || schema_version_ < 0
      || column_cnt_ <= 0
      || tablet_size_ < 0
      || pctfree_ < 0
      || table_type_ >= MAX_TABLE_TYPE
      || !table_mode_.is_valid()
      || index_type_ >= INDEX_TYPE_MAX
      || !check_column_array_valid(rowkey_array_)
      || !check_column_array_valid(column_array_)
      || !check_column_array_valid(skip_idx_attr_array_)) {
    valid_ret = false;
    STORAGE_LOG_RET(WARN, OB_INVALID_ERROR, "invalid", K_(is_inited), K_(format_version), KP_(allocator), K_(schema_version), K_(column_cnt),
        K_(tablet_size), K_(pctfree), K_(table_type), K_(table_mode), K_(index_type));
  } else if (!column_info_simplified_ && column_cnt_ != column_array_.count()) {
    valid_ret = false;
    STORAGE_LOG_RET(WARN, OB_INVALID_ERROR, "invalid column count", K(valid_ret), K_(column_info_simplified), K_(column_cnt), K_(column_array));
  } else if (is_view_table()) {
    // no need checking other options for view
  }
  return valid_ret;
}

int ObStorageSchema::assign(common::ObIAllocator &allocator, const ObStorageSchema &other)
{
  int ret = OB_SUCCESS;
  reset();

  if (OB_FAIL(init(allocator, other))) {
  }

  return ret;
}

int ObStorageSchema::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", K(ret), K_(is_inited));
  } else if (OB_ISNULL(buf)
      || OB_UNLIKELY(buf_len <= 0)
      || OB_UNLIKELY(pos < 0)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid args", K(ret), K(buf), K(buf_len), K(pos));
  } else if (OB_UNLIKELY(!is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "invalid storage schema", K(ret), KPC(this));
  } else {
    LST_DO_CODE(OB_UNIS_ENCODE,
        format_version_,
        info_,
        table_type_,
        table_mode_,
        index_type_,
        row_store_type_,
        schema_version_,
        column_cnt_,
        tablet_size_,
        pctfree_,
        block_size_,
        progressive_merge_round_,
        progressive_merge_num_,
        compressor_type_);
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(serialize_schema_array(buf, buf_len, pos, rowkey_array_))){
    } else if (!column_info_simplified_ && OB_FAIL(serialize_column_array(buf, buf_len, pos))){
      STORAGE_LOG(WARN, "failed to serialize columns", K_(column_array));
    } else if (OB_FAIL(serialization::encode_i64(buf, buf_len, pos, store_column_cnt_))) {
    } else if (OB_FAIL(serialize_schema_array(buf, buf_len, pos, skip_idx_attr_array_))){
    } else {
      OB_UNIS_ENCODE(semistruct_encoding_type_);
    }
  }

  return ret;
}

int ObStorageSchema::serialize_column_array(char *buf, const int64_t data_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(serialization::encode_vi64(buf, data_len, pos, column_array_.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < column_array_.count(); ++i) {
    if (OB_FAIL(column_array_.at(i).serialize(buf, data_len, pos))) {
    }
  }
  return ret;
}


int ObStorageSchema::deserialize(
    common::ObIAllocator &allocator,
    const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "cannot deserialize inited storage schema", K(ret), K_(is_inited));
  } else if (OB_ISNULL(buf)
      || OB_UNLIKELY(data_len <= 0)
      || OB_UNLIKELY(pos < 0)
      || OB_UNLIKELY(data_len <= pos)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid args", K(ret), K(buf), K(data_len), K(pos));
  } else {
    allocator_ = &allocator;
    rowkey_array_.set_allocator(&allocator);
    column_array_.set_allocator(&allocator);
    skip_idx_attr_array_.set_allocator(&allocator);
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(serialization::decode(buf, data_len, pos, format_version_))) {
  } else if (STORAGE_SCHEMA_FORMAT_VERSION == format_version_) {
    LST_DO_CODE(OB_UNIS_DECODE,
        info_,
        table_type_,
        table_mode_,
        index_type_,
        row_store_type_,
        schema_version_,
        column_cnt_,
        tablet_size_,
        pctfree_,
        block_size_,
        progressive_merge_round_,
        progressive_merge_num_,
        compressor_type_);
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(deserialize_rowkey_column_array(buf, data_len, pos))){
    } else if (!column_info_simplified_ && OB_FAIL(deserialize_column_array(allocator, buf, data_len, pos))){
      STORAGE_LOG(WARN, "failed to deserialize columns", K(ret), K_(column_array));
    } else if (OB_FAIL(serialization::decode_i64(buf, data_len, pos, &store_column_cnt_))) {
    } else if (OB_FAIL(deserialize_skip_idx_attr_array(buf, data_len, pos))) {
    } else {
      OB_UNIS_DECODE(semistruct_encoding_type_);
    }

    if (OB_SUCC(ret) && OB_UNLIKELY(!ObStorageSchema::is_valid())) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "deserialized storage schema is invalid", K(ret), KPC(this));
    } else if (OB_SUCC(ret)) {
      is_inited_ = true;
    }
  } else {
    ret = OB_NOT_SUPPORTED;
    STORAGE_LOG(WARN, "storage schema format mismatch", K(ret), K_(format_version));
  }

  if (OB_UNLIKELY(!is_inited_)) {
    reset();
  }

  return ret;
}

int ObStorageSchema::deserialize_rowkey_column_array(
    const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  if (OB_ISNULL(buf) || OB_UNLIKELY(data_len <= 0) || OB_UNLIKELY(pos > data_len)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(buf), K(data_len), K(pos), K(ret));
  } else if (pos == data_len) {
    //do nothing
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &count))) {
  } else if (OB_FAIL(rowkey_array_.reserve(count))) {
  } else {
    ObStorageRowkeyColumnSchema column;
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      column.reset();
      if (OB_FAIL(column.deserialize(buf, data_len, pos))) {
      } else if (OB_FAIL(rowkey_array_.push_back(column))) {
      }
    }
  }
  return ret;
}

int64_t ObStorageSchema::get_column_array_serialize_length(
  const common::ObIArray<ObStorageColumnSchema> &array) const
{
  int64_t len = 0;
  len += serialization::encoded_length_vi64(array.count());
  for (int64_t i = 0; i < array.count(); ++i) {
    len += array.at(i).get_serialize_size();
  }
  return len;
}

int ObStorageSchema::deserialize_column_array(
    ObIAllocator &allocator,
    const char *buf,
    const int64_t data_len,
    int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  if (OB_ISNULL(buf) || OB_UNLIKELY(data_len <= 0) || OB_UNLIKELY(pos > data_len)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(buf), K(data_len), K(pos), K(ret));
  } else if (pos == data_len) {
    //do nothing
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &count))) {
  } else if (OB_FAIL(column_array_.reserve(count))) {
  } else {
    ObStorageColumnSchema column;
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      column.reset();
      if (OB_FAIL(column.deserialize(buf, data_len, pos))) {
      }

      if (OB_SUCC(ret) && column.orig_default_value_.get_deep_copy_size() > 0) {
        ObStorageColumnSchema deep_copy_column;
        if (OB_FAIL(deep_copy_column.deep_copy_default_val(allocator, column.get_orig_default_value()))) {
        } else {
          column.orig_default_value_ = deep_copy_column.orig_default_value_;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(column_array_.push_back(column))) {
        STORAGE_LOG(WARN, "Fail to add column", K(ret), K(column));
        column.destroy(allocator);
      }
    }
  }
  return ret;
}

int ObStorageSchema::deserialize_skip_idx_attr_array(const char *buf,
                                                     const int64_t data_len,
                                                     int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t count = 0;
  if (OB_ISNULL(buf) || OB_UNLIKELY(data_len <= 0) || OB_UNLIKELY(pos > data_len)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(buf), K(data_len), K(pos), K(ret));
  } else if (pos == data_len) {
    //do nothing
  } else if (OB_FAIL(serialization::decode_vi64(buf, data_len, pos, &count))) {
  } else if (OB_FAIL(skip_idx_attr_array_.reserve(count))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      ObSkipIndexAttrWithId skip_attr_with_id;
      if (OB_FAIL(skip_attr_with_id.deserialize(buf, data_len, pos))) {
      } else if (OB_FAIL(skip_idx_attr_array_.push_back(skip_attr_with_id))) {
      }
    }
  }
  return ret;
}

int64_t ObStorageSchema::get_serialize_size() const
{
  int64_t len = 0;

  LST_DO_CODE(OB_UNIS_ADD_LEN,
      format_version_,
      info_,
      table_type_,
      table_mode_,
      index_type_,
      row_store_type_,
      schema_version_,
      column_cnt_,
      tablet_size_,
      pctfree_,
      block_size_,
      progressive_merge_round_,
      progressive_merge_num_,
      compressor_type_);
  len += get_array_serialize_length(rowkey_array_);
  //get columms size
  if (!column_info_simplified_) {
    len += get_column_array_serialize_length(column_array_);
  }
  len += serialization::encoded_length_i64(store_column_cnt_);
  len += get_array_serialize_length(skip_idx_attr_array_);
  OB_UNIS_ADD_LEN(semistruct_encoding_type_);
  return len;
}

int ObStorageSchema::generate_column_array(const ObTableSchema &input_schema)
{
  int ret = OB_SUCCESS;
  // build column schema map
  common::hash::ObHashMap<uint64_t, uint64_t> tmp_map; // column_id -> index

  if (OB_FAIL(tmp_map.create(input_schema.get_column_count(), "StorageSchema"))) {
  } else if (OB_FAIL(input_schema.check_column_array_sorted_by_column_id(true/*skip_rowkey*/))) {
  }

  ObTableSchema::const_column_iterator iter = input_schema.column_begin();
  ObColumnSchemaV2 *col = NULL;
  ObStorageColumnSchema col_schema;
  if (FAILEDx(column_array_.reserve(input_schema.get_column_count()))) {
    STORAGE_LOG(WARN, "Fail to reserve column array", K(ret));
  }
  int64_t col_idx = 0;
  int64_t col_cnt_in_sstable = 0;
  int64_t has_skip_index_cnt = 0;
  blocksstable::ObStorageDatum datum;
  ObSEArray<share::schema::ObSkipIndexAttrWithId, 16> tmp_skip_array;
  for ( ; OB_SUCC(ret) && iter != input_schema.column_end(); iter++) {
    if (OB_ISNULL(col = *iter)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "The column is NULL", K(col));
    } else if (FALSE_IT(col_cnt_in_sstable += col->is_column_stored_in_sstable())) {
      // only record stored column count here
    } else if (!column_info_simplified_) {
      col_schema.reset();
      col_schema.is_rowkey_column_ = col->is_rowkey_column();
      col_schema.is_column_stored_in_sstable_ = col->is_column_stored_in_sstable();
      col_schema.is_generated_column_ = col->is_generated_column();
      ObObjMeta meta_type = col->get_meta_type();
      if (meta_type.is_decimal_int()) {
        meta_type.set_stored_precision(col->get_accuracy().get_precision());
        meta_type.set_scale(col->get_accuracy().get_scale());
      } else if (ob_is_real_type(meta_type.get_type())) {
        meta_type.set_scale(col->get_accuracy().get_scale());
      }
      col_schema.meta_type_ = meta_type;
      const ObObj &orig_default_val = col->get_orig_default_value();
      if (OB_FAIL(datum.from_obj_enhance(orig_default_val))) {
      } else if (is_lob_storage(col->get_data_type()) && !datum.has_lob_header()
              && OB_FAIL(data_plane::fill_lob_header(*allocator_, datum))) {
        STORAGE_LOG(WARN, "failed to fill lob header", K(ret), K(datum));
      } else if (orig_default_val.is_fixed_len_char_type()
              && OB_FAIL(trim(orig_default_val.get_collation_type(), datum))) {
        STORAGE_LOG(WARN, "failed to trim default value", K(ret), K(orig_default_val), K(datum));
      } else {
        col_schema.default_checksum_ = datum.checksum(0);
#ifdef ERRSIM
        if (orig_default_val.is_fixed_len_char_type()) {
          const int64_t original_checksum = col_schema.default_checksum_;
          const int64_t errsim_code = EN_NOT_TRIM_FOR_DEFALUT_CHECKSUM;
          if (OB_SUCCESS != errsim_code) {
            blocksstable::ObStorageDatum errsim_datum;
            if (OB_FAIL(errsim_datum.from_obj_enhance(orig_default_val))) {
              STORAGE_LOG(WARN, "Failed to transfer obj to errsim_datum", K(ret));
            } else {
              col_schema.default_checksum_ = errsim_datum.checksum(0);
            }
          }
          STORAGE_LOG(INFO, "ERRSIM: whether to trim space for default checksum", K(ret), K(errsim_code), K(need_trim_default_val), 
                            K(original_checksum), "current_checksum", col_schema.default_checksum_);
        }
#endif
      }
      if (FAILEDx(col_schema.deep_copy_default_val(*allocator_, orig_default_val))) {
        STORAGE_LOG(WARN, "failed to deep copy", K(ret), K(orig_default_val));
      } else if (OB_FAIL(column_array_.push_back(col_schema))) {
        STORAGE_LOG(WARN, "Fail to push into column array", K(ret), K(col_schema));
        col_schema.destroy(*allocator_);
      }
    }
    const share::schema::ObSkipIndexColumnAttr &skip_idx_attr = col->get_skip_index_attr();
    ObSkipIndexAttrWithId skip_attr_with_id;
    skip_attr_with_id.col_idx_ = col_idx;
    skip_attr_with_id.skip_idx_attr_ = skip_idx_attr;
    if (FAILEDx(tmp_map.set_refactored(col->get_column_id(), col_idx))) {
      STORAGE_LOG(WARN, "failed to set column map", K(ret), "col_id", col->get_column_id(), K(col_idx));
    } else if (skip_idx_attr.has_skip_index() && col->is_column_stored_in_sstable() &&
        OB_FAIL(tmp_skip_array.push_back(skip_attr_with_id))) {
      STORAGE_LOG(WARN, "fail to push into skip idx attr with col id array",
          K(ret), K(col_idx), K(skip_attr_with_id));
    } else {
      col_idx++;
    }
  } // end of for

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(skip_idx_attr_array_.assign(tmp_skip_array))){
  } else {
    store_column_cnt_ = is_storage_index_table() ? input_schema.get_column_count() : col_cnt_in_sstable;
  }
  // add rowkey columns
  ObStorageRowkeyColumnSchema rowkey_schema;
  const ObColumnSchemaV2 *rowkey_col_schema = nullptr;
  const common::ObRowkeyInfo &rowkey_info = input_schema.get_rowkey_info();
  const ObRowkeyColumn *rowkey_column = NULL;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(rowkey_array_.reserve(rowkey_info.get_size()))) {
  }
  uint64_t find_idx = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_info.get_size(); ++i) {
    if (NULL == (rowkey_column = rowkey_info.get_column(i))) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "The rowkey column is NULL", K(i));
    } else if (OB_FAIL(tmp_map.get_refactored(rowkey_column->column_id_, find_idx))) {
    } else if (OB_ISNULL(rowkey_col_schema =
                           input_schema.get_column_schema(rowkey_column->column_id_))) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "failed to get rowkey column schema", K(ret));
    } else {
      rowkey_schema.reset();
      rowkey_schema.column_idx_ = common::OB_APP_MIN_COLUMN_ID + find_idx;
      ObObjMeta meta_type = rowkey_column->type_;
      if (meta_type.is_decimal_int()) {
        meta_type.set_stored_precision(rowkey_col_schema->get_accuracy().get_precision());
        meta_type.set_scale(rowkey_col_schema->get_accuracy().get_scale());
      } else if (ob_is_real_type(meta_type.get_type())) {
        meta_type.set_scale(rowkey_col_schema->get_accuracy().get_scale());
      }
      rowkey_schema.meta_type_ = meta_type;
      rowkey_schema.order_ = rowkey_column->order_;
      if (OB_FAIL(rowkey_array_.push_back(rowkey_schema))) {
      }
    }
  }

  if (tmp_map.created()) {
    tmp_map.destroy();
  }
  return ret;
}

int ObStorageSchema::get_column_ids_without_rowkey(
    common::ObIArray<share::schema::ObColDesc> &column_ids,
    const bool no_virtual) const
{
  int ret = OB_SUCCESS;
  ObColDesc col_desc;
  if (column_info_simplified_) {
    // fake column ids
    for (int64_t i = rowkey_array_.count(); OB_SUCC(ret) && i < store_column_cnt_; i++) {
      col_desc.col_id_ = common::OB_APP_MIN_COLUMN_ID + i;
      //for non-rowkey, col_desc.col_order_ is not meaningful
      if (OB_FAIL(column_ids.push_back(col_desc))) {
      }
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < column_cnt_; i++) {
      const ObStorageColumnSchema &column = column_array_[i];
      if (!column.is_rowkey_column_                    // column is not a rowkey column(rowkey column is already added in step1 get_rowkey_column_ids)
         && (column.is_column_stored_in_sstable_       // current column is not virtual
              || !no_virtual)) {                       // could have virtual column
        col_desc.col_id_ = common::OB_APP_MIN_COLUMN_ID + i;
        col_desc.col_type_ = column.meta_type_;
        //for non-rowkey, col_desc.col_order_ is not meaningful
        if (OB_FAIL(column_ids.push_back(col_desc))) {
        }
      }
    } // end of for
  }
  return ret;
}

int ObStorageSchema::get_rowkey_column_ids(common::ObIArray<ObColDesc> &column_ids) const
{
  int ret = OB_SUCCESS;
  ObColDesc col_desc;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", K(ret), K_(is_inited));
  } else {
    //add rowkey columns
    for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_array_.count(); ++i) {
      col_desc.col_id_ = rowkey_array_[i].column_idx_;
      col_desc.col_type_ = rowkey_array_[i].meta_type_;
      col_desc.col_order_ = (ObOrderType)rowkey_array_[i].order_;
      if (OB_FAIL(column_ids.push_back(col_desc))) {
      }
    }
  }
  return ret;
}

int ObStorageSchema::get_skip_index_col_attr(
    ObIArray<share::schema::ObSkipIndexColumnAttr> &skip_idx_attrs) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", K(ret), K_(is_inited));
  } else {
    skip_idx_attrs.reset();
    // add rowkey columns
    share::schema::ObSkipIndexColumnAttr rowkey_skip_idx_attr;
    for (int64_t i = 0; OB_SUCC(ret) && i < rowkey_array_.count(); ++i) {
      rowkey_skip_idx_attr.reset();
      int64_t rowkey_col_idx = rowkey_array_[i].column_idx_ - common::OB_APP_MIN_COLUMN_ID;
      for (int64_t skip_col_id = 0; OB_SUCC(ret) && skip_col_id < skip_idx_attr_array_.count(); ++skip_col_id) {
        if (rowkey_col_idx == skip_idx_attr_array_.at(skip_col_id).col_idx_) {
          rowkey_skip_idx_attr = skip_idx_attr_array_.at(skip_col_id).skip_idx_attr_;
          break;
        }
      }
      if (FAILEDx(skip_idx_attrs.push_back(rowkey_skip_idx_attr))) {
        STORAGE_LOG(WARN, "fail to append rowkey skip index attr to array",
            K(ret), K(i), K(rowkey_skip_idx_attr));
      }
    }
    // add dummy idx for stored multi-version columns
    if (OB_SUCC(ret)) {
      ObSkipIndexColumnAttr dummy_multi_version_col_attr;
      if (OB_FAIL(skip_idx_attrs.push_back(dummy_multi_version_col_attr))) {
      } else if (OB_FAIL(skip_idx_attrs.push_back(dummy_multi_version_col_attr))) {
      }
    }
    // add non-rowkey columns

    share::schema::ObSkipIndexColumnAttr no_rowkey_skip_idx_attr;
    for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < column_cnt_; ++col_idx) {
      no_rowkey_skip_idx_attr.reset();
      bool is_rowkey = false;
      for (int64_t j = 0; OB_SUCC(ret) && j < rowkey_array_.count(); ++j) {
        int64_t rowkey_col_idx = rowkey_array_[j].column_idx_ - common::OB_APP_MIN_COLUMN_ID;
        if (rowkey_col_idx == col_idx) {
          is_rowkey = true;
          break;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (is_rowkey) {
        //skip
      } else {
        for (int64_t skip_col_id = 0; OB_SUCC(ret) && skip_col_id < skip_idx_attr_array_.count(); ++skip_col_id) {
          if (col_idx == skip_idx_attr_array_.at(skip_col_id).col_idx_) {
            no_rowkey_skip_idx_attr = skip_idx_attr_array_.at(skip_col_id).skip_idx_attr_;
            break;
          }
        }
        if (FAILEDx(skip_idx_attrs.push_back(no_rowkey_skip_idx_attr))) {
          STORAGE_LOG(WARN, "fail to append no rowkey skip index attr to array",
              K(ret), K(col_idx), K(rowkey_skip_idx_attr));
        }
      }
    }
  }
  return ret;
}

int ObStorageSchema::get_stored_column_count_in_sstable(int64_t &column_count) const
{
  int ret = OB_SUCCESS;
  column_count = 0;
  if (OB_FAIL(get_store_column_count(column_count, true/*full_col*/))) {
  } else {
    column_count += storage::ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
  }
  return ret;
}

int ObStorageSchema::get_store_column_count(int64_t &column_count, const bool full_col) const
{
  UNUSED(full_col);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", K(ret), K_(is_inited));
  } else if (is_storage_index_table()) {
    column_count = column_cnt_;
  } else {
    column_count = store_column_cnt_;
  }
  return ret;
}

// will call in deserialize for compat to init store_column_cnt_

int ObStorageSchema::get_column_default_checksums(
    common::ObIArray<share::schema::ObColumnDefaultChecksum> &checksums) const
{
  int ret = OB_SUCCESS;
  ObArray<ObColDesc> columns;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", K(ret), K_(is_inited));
  } else if (column_info_simplified_) {
    ret = OB_NOT_SUPPORTED;
    STORAGE_LOG(WARN, "not support get multi version column desc array when column simplified", K(ret), KPC(this));
  } else if (OB_FAIL(get_multi_version_column_descs(columns))) {
  } else {
    // build column schema map
    common::hash::ObHashMap<uint64_t, uint64_t> tmp_map; // column_id -> index
    if (OB_FAIL(tmp_map.create(column_array_.count(), "StorageSchema"))) {
    }
    for (int i = 0; OB_SUCC(ret) && i < column_array_.count(); ++i) {
      if (OB_FAIL(tmp_map.set_refactored(common::OB_APP_MIN_COLUMN_ID + i, i))) {
      }
    }
    uint64_t idx = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
      const uint64_t column_idx = columns.at(i).col_id_;
      int64_t default_checksum = 0;
      if (OB_FAIL(tmp_map.get_refactored(column_idx, idx))) {
        // if it's multi version extra rowkey, no problem
        if (column_idx == OB_HIDDEN_TRANS_VERSION_COLUMN_ID ||
            column_idx == OB_HIDDEN_SQL_SEQUENCE_COLUMN_ID) {
          ret = OB_SUCCESS;
        } else {
          STORAGE_LOG(WARN, "failed to get column schema", K(ret), K(i), K(columns.at(i)));
        }
      } else if (idx >= column_array_.count()) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "idx is invalid", K(ret), K(idx), K(*this));
      } else {
        const ObStorageColumnSchema &col_schema = column_array_.at(idx);
        if (!col_schema.is_column_stored_in_sstable_ && !is_storage_index_table()) {
          ret = OB_ERR_UNEXPECTED;
          STORAGE_LOG(WARN, "virtual generated column should be filtered already", K(ret), K(col_schema));
        } else {
          default_checksum = col_schema.default_checksum_;
        }
      }
      if (OB_SUCC(ret)
          && OB_FAIL(checksums.push_back(
              share::schema::ObColumnDefaultChecksum(column_idx, default_checksum)))) {
        STORAGE_LOG(WARN, "Fail to push column default checksum", K(ret));
      }
    } // end for
    if (tmp_map.created()) {
      tmp_map.destroy();
    }
  }

  return ret;
}

int ObStorageSchema::get_orig_default_row(
    const common::ObIArray<ObColDesc> &column_ids,
    bool need_trim,
    blocksstable::ObDatumRow &default_row) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!default_row.is_valid() || default_row.count_ != column_ids.count()
      || column_ids.count() > column_cnt_ + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt())) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "Invalid argument", K(ret), K(column_cnt_), K(default_row), K(column_ids.count()));
  }

  for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); ++i) {
    if (column_ids.at(i).col_id_ == OB_HIDDEN_TRANS_VERSION_COLUMN_ID ||
        column_ids.at(i).col_id_ == OB_HIDDEN_SQL_SEQUENCE_COLUMN_ID) {
      default_row.storage_datums_[i].set_int(0);
    } else {
      const ObStorageColumnSchema *col_schema = nullptr;
      if (OB_ISNULL(col_schema = get_column_schema(column_ids.at(i).col_id_))) {
        ret = OB_ERR_SYS;
        STORAGE_LOG(WARN, "column id not found", K(ret), K(column_ids.at(i)));
      } else if (OB_FAIL(default_row.storage_datums_[i].from_obj_enhance(col_schema->get_orig_default_value()))) {
      } else if (need_trim && col_schema->get_orig_default_value().is_fixed_len_char_type()) {
        if (OB_FAIL(trim(col_schema->get_orig_default_value().get_collation_type(), default_row.storage_datums_[i]))) {
        }
      }
    }
  }
  return ret;
}

int ObStorageSchema::trim(const ObCollationType type, blocksstable::ObStorageDatum &storage_datum)
{
  int ret = OB_SUCCESS;
  ObString space_pattern = ObCharsetUtils::get_const_str(type, ' ');
  if (OB_UNLIKELY(!ObCharset::is_valid_collation(type) || (0 == space_pattern.length()))) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid collation type", K(ret), K(type), K(space_pattern));
  } else {
    const char *str = storage_datum.ptr_;
    int32_t len = storage_datum.len_;
    for (; len >= space_pattern.length(); len -= space_pattern.length()) {
      if (0 != MEMCMP(str + len - space_pattern.length(),
            space_pattern.ptr(),
            space_pattern.length())) {
        break;
      }
    }
    storage_datum.len_ = len;
  }
  return ret;
}

const ObStorageColumnSchema *ObStorageSchema::get_column_schema(const int64_t column_idx) const
{
  const ObStorageColumnSchema *found_col = nullptr;
  for (int64_t j = 0; j < column_cnt_; ++j) {
    const ObStorageColumnSchema &column = column_array_[j];
    if (common::OB_APP_MIN_COLUMN_ID + j == column_idx) {
      found_col = &column;
      break;
    }
  }
  return found_col;
}

int ObStorageSchema::get_multi_version_column_descs(common::ObIArray<ObColDesc> &column_descs) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "not inited", K(ret), K_(is_inited));
  } else if (column_info_simplified_) {
    ret = OB_NOT_SUPPORTED;
    STORAGE_LOG(WARN, "not support get multi version column desc array when column simplified", K(ret), KPC(this));
  } else if (OB_FAIL(get_mulit_version_rowkey_column_ids(column_descs))) {
  } else if (OB_FAIL(get_column_ids_without_rowkey(column_descs, !is_storage_index_table()))) {
  }
  return ret;
}

int ObStorageSchema::copy_from(const share::schema::ObMergeSchema &input_schema)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(input_schema.get_semistruct_encoding_type(semistruct_encoding_type_))) {
  } else {
    table_type_ = input_schema.get_table_type();
    table_mode_ = input_schema.get_table_mode_struct();
    index_type_ = input_schema.get_index_type();
    row_store_type_ = input_schema.get_row_store_type();
    schema_version_ = input_schema.get_schema_version();
    column_cnt_ = input_schema.get_column_count();
    tablet_size_ = input_schema.get_tablet_size();
    pctfree_ = input_schema.get_pctfree();
    block_size_ = input_schema.get_block_size();
    progressive_merge_round_ = input_schema.get_progressive_merge_round();
    progressive_merge_num_ = input_schema.get_progressive_merge_num();
    compressor_type_ = input_schema.get_compressor_type();
  }

  return ret;
}

void ObStorageSchema::update_column_cnt(const int64_t input_col_cnt)
{
  column_cnt_ = MAX(column_cnt_, input_col_cnt);
  store_column_cnt_ = MAX(store_column_cnt_, input_col_cnt);
  if (column_cnt_ != column_array_.count()) {
    column_info_simplified_ = true;
    STORAGE_LOG(INFO, "update column cnt", K(column_cnt_), K(store_column_cnt_), K(column_cnt_), K(column_array_.count()));
  }
}

int ObCreateTabletSchema::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  BASE_SER((, ObStorageSchema));
  LST_DO_CODE(OB_UNIS_ENCODE,
              table_id_,
              index_status_,
              truncate_version_);
  return ret;
}

int ObCreateTabletSchema::deserialize(common::ObIAllocator &allocator, const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObStorageSchema::deserialize(allocator, buf, data_len, pos))) {
  } else {
    LST_DO_CODE(OB_UNIS_DECODE,
                table_id_,
                index_status_,
                truncate_version_);
  }
  return ret;
}

int64_t ObCreateTabletSchema::get_serialize_size() const
{
  int64_t len = ObStorageSchema::get_serialize_size();
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              table_id_,
              index_status_,
              truncate_version_);
  return len;
}

int ObCreateTabletSchema::init(
    common::ObIAllocator &allocator,
    const share::schema::ObTableSchema &input_schema,
    const bool skip_column_info)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObStorageSchema::init(allocator, input_schema, skip_column_info))) {
  } else {
    table_id_ = input_schema.get_table_id();
    index_status_ = input_schema.get_index_status();
    truncate_version_ = input_schema.get_truncate_version();
  }
  return ret;
}

int ObCreateTabletSchema::init(
    common::ObIAllocator &allocator,
    const ObCreateTabletSchema &old_schema)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObStorageSchema::init(allocator, old_schema))) {
  } else {
    table_id_ = old_schema.get_table_id();
    index_status_ = old_schema.get_index_status();
    truncate_version_ = old_schema.get_truncate_version();
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
