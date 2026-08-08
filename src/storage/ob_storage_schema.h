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

#ifndef OCEANBASE_STORAGE_STORAGE_SCHEMA_
#define OCEANBASE_STORAGE_STORAGE_SCHEMA_

#include "common/ob_version_def.h"
#include "lib/container/ob_fixed_array.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{

namespace blocksstable
{
struct ObSSTableColumnMeta;
class ObStorageDatum;
}

namespace storage
{
struct ObStorageRowkeyColumnSchema
{
  OB_UNIS_VERSION(1);
public:
  ObStorageRowkeyColumnSchema();
  ~ObStorageRowkeyColumnSchema();
  void reset();
  bool is_valid() const;
  TO_STRING_KV(K_(column_idx), K_(meta_type), K_(order));

private:
  static const int32_t SRCS_ONE_BIT = 1;
  static const int32_t SRCS_RESERVED_BITS = 31;

public:
  union {
    uint32_t info_;
    struct {
      uint32_t order_   :SRCS_ONE_BIT;
      uint32_t reserved_:SRCS_RESERVED_BITS;
    };
  };
  uint32_t column_idx_;
  ObObjMeta meta_type_;
};

struct ObStorageColumnSchema
{
  OB_UNIS_VERSION(1);
public:
  ObStorageColumnSchema();
  ~ObStorageColumnSchema();

  void reset();
  void destroy(ObIAllocator &allocator);
  bool is_valid() const;
  inline common::ColumnType get_data_type() const { return meta_type_.get_type(); }
  inline bool is_generated_column() const { return is_generated_column_; }
  inline bool is_column_stored_in_sstable() const { return is_column_stored_in_sstable_;}
  inline bool is_rowkey_column() const { return is_rowkey_column_; }
  inline const common::ObObj &get_orig_default_value()  const { return orig_default_value_; }
  int deep_copy_default_val(ObIAllocator &allocator, const ObObj &default_val);

  TO_STRING_KV(K_(meta_type), K_(is_column_stored_in_sstable), K_(is_rowkey_column),
      K_(is_generated_column), K_(orig_default_value));

private:
  static const int32_t SCS_ONE_BIT = 1;
  static const int32_t SCS_RESERVED_BITS = 29;

public:
  union {
    uint32_t info_;
    struct {
      uint32_t is_column_stored_in_sstable_     :SCS_ONE_BIT;
      uint32_t is_rowkey_column_                :SCS_ONE_BIT;
      uint32_t is_generated_column_             :SCS_ONE_BIT;
      uint32_t reserved_                        :SCS_RESERVED_BITS;
    };
  };
  int64_t default_checksum_;
  ObObjMeta meta_type_;
  ObObj orig_default_value_;
};

class ObStorageSchema : public share::schema::ObMergeSchema
{
public:
  ObStorageSchema();
  virtual ~ObStorageSchema();
  bool is_inited() const { return is_inited_; }
  int init(
      common::ObIAllocator &allocator,
      const share::schema::ObTableSchema &input_schema,
      const bool skip_column_info = false);
  int init(
      common::ObIAllocator &allocator,
      const ObStorageSchema &old_schema,
      const bool skip_column_info = false,
      const int64_t stored_column_count = -1);
  int deep_copy_column_array(
      common::ObIAllocator &allocator,
      const ObStorageSchema &src_schema,
      const int64_t copy_array_cnt);
  void reset();
  virtual bool is_valid() const override;
  // serialize & deserialize
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(
      common::ObIAllocator &allocator,
      const char *buf,
      const int64_t data_len,
      int64_t &pos);
  int64_t get_serialize_size() const;
  void update_column_cnt(const int64_t input_col_cnt);

  // for new mds
  int assign(common::ObIAllocator &allocator, const ObStorageSchema &other);

  /* merge related function*/
  virtual inline int64_t get_tablet_size() const override { return tablet_size_; }
  virtual inline int64_t get_rowkey_column_num() const override { return rowkey_array_.count(); }
  virtual inline int64_t get_schema_version() const override { return schema_version_; }
  virtual inline int64_t get_column_count() const override { return column_cnt_; }
  virtual inline int64_t get_pctfree() const override { return pctfree_; }
  virtual inline int64_t get_progressive_merge_round() const override { return progressive_merge_round_; }
  virtual inline int64_t get_progressive_merge_num() const override { return progressive_merge_num_; }
  virtual inline bool is_index_table() const override { return share::schema::is_index_table(table_type_); }
  virtual inline bool is_storage_index_table() const override
  {
    return share::schema::is_index_table(table_type_);
  }
  inline bool is_fts_index() const { return share::schema::is_fts_index(index_type_); }
  inline bool is_vec_index() const { return share::schema::is_vec_index(index_type_); }
  inline bool is_user_data_table() const { return share::schema::ObTableSchema::is_user_data_table(table_type_); }
  virtual inline bool is_global_index_table() const override { return share::schema::ObSimpleTableSchemaV2::is_global_index_table(index_type_); }
  virtual inline int64_t get_block_size() const override { return block_size_; }

  virtual int get_store_column_count(int64_t &column_count, const bool full_col) const override;
  int get_stored_column_count_in_sstable(int64_t &column_count) const;
  virtual int get_multi_version_column_descs(common::ObIArray<share::schema::ObColDesc> &column_descs) const override;
  virtual int get_rowkey_column_ids(common::ObIArray<share::schema::ObColDesc> &column_ids) const override;
  virtual int get_skip_index_col_attr(common::ObIArray<share::schema::ObSkipIndexColumnAttr> &skip_idx_metas) const override;
  virtual inline share::schema::ObTableModeFlag get_table_mode_flag() const override
  { return (share::schema::ObTableModeFlag)table_mode_.mode_flag_; }
  virtual inline share::schema::ObTableMode get_table_mode_struct() const override { return table_mode_; }
  const share::schema::ObSemiStructEncodingType& get_semistruct_encoding_type() const { return semistruct_encoding_type_; }
  virtual int get_semistruct_encoding_type(share::schema::ObSemiStructEncodingType& type) const
  {
    type = semistruct_encoding_type_;
    return OB_SUCCESS;
  }
  virtual inline share::schema::ObTableType get_table_type() const override { return table_type_; }
  virtual inline share::schema::ObIndexType get_index_type() const override { return index_type_; }
  const common::ObIArray<ObStorageColumnSchema> &get_store_column_schemas() const { return column_array_; }
  virtual inline common::ObRowStoreType get_row_store_type() const override { return row_store_type_; }
  virtual inline const char *get_compress_func_name() const override {  return all_compressor_name[compressor_type_]; }
  virtual inline common::ObCompressorType get_compressor_type() const override { return compressor_type_; }
  virtual inline bool is_column_info_simplified() const override { return column_info_simplified_; }

  virtual int get_column_default_checksums(
      common::ObIArray<share::schema::ObColumnDefaultChecksum> &checksums) const override;
  int get_orig_default_row(const common::ObIArray<share::schema::ObColDesc> &column_ids,
                           bool need_trim,
                           blocksstable::ObDatumRow &default_row) const;
  const ObStorageColumnSchema *get_column_schema(const int64_t column_id) const;
  // Use this comparison function to determine which schema has been updated later
  // true: input_schema is newer
  // false: current schema is newer
  bool compare_schema_newer(const ObStorageSchema &input_schema) const
  {
    return store_column_cnt_ < input_schema.store_column_cnt_;
  }

  inline bool is_aux_lob_meta_table() const { return share::schema::is_aux_lob_meta_table(table_type_); }
  inline bool is_aux_lob_piece_table() const { return share::schema::is_aux_lob_piece_table(table_type_); }
  OB_INLINE bool is_user_hidden_table() const { return share::schema::TABLE_STATE_IS_HIDDEN_MASK & table_mode_.state_flag_; }
  VIRTUAL_TO_STRING_KV(KP(this), K_(format_version),
      K_(column_info_simplified), K_(table_type), K_(index_type),
      K_(row_store_type), K_(schema_version),
      K_(column_cnt), K_(store_column_cnt), K_(tablet_size), K_(pctfree), K_(block_size), K_(progressive_merge_round),
      K_(compressor_type),
      "rowkey_cnt", rowkey_array_.count(), K_(rowkey_array), "column_cnt", column_array_.count(), K_(column_array),
      "skip_index_cnt", skip_idx_attr_array_.count(), K_(skip_idx_attr_array));
public:
  static int trim(const ObCollationType type, blocksstable::ObStorageDatum &storage_datum);
private:
  int copy_from(const share::schema::ObMergeSchema &input_schema);
  int truncate_column_array(const int64_t stored_column_count);
  inline bool is_view_table() const { return share::schema::ObTableType::USER_VIEW == table_type_ || share::schema::ObTableType::SYSTEM_VIEW == table_type_; }

  int generate_column_array(const share::schema::ObTableSchema &input_schema);
  int get_column_ids_without_rowkey(
      common::ObIArray<share::schema::ObColDesc> &column_ids,
      bool no_virtual) const;
  /* serialize related function */
  template <typename T>
  int serialize_schema_array(
      char *buf, const int64_t data_len, int64_t &pos, const common::ObIArray<T> &array) const;
  int serialize_column_array(char *buf, const int64_t data_len, int64_t &pos) const;
  int deserialize_rowkey_column_array(const char *buf, const int64_t data_len, int64_t &pos);
  int deserialize_column_array(ObIAllocator &allocator, const char *buf, const int64_t data_len, int64_t &pos);
  int64_t get_column_array_serialize_length(const common::ObIArray<ObStorageColumnSchema> &array) const;
  int deserialize_skip_idx_attr_array(const char *buf, const int64_t data_len, int64_t &pos);
  template <typename T>
  int64_t get_array_serialize_length(const common::ObIArray<T> &array) const;
  template <typename T>
  bool check_column_array_valid(const common::ObIArray<T> &array) const;

public:
  static const int32_t SS_ONE_BIT = 1;
  static const int32_t SS_RESERVED1_BITS = 8;
  static const int32_t SS_RESERVED2_BITS = 23;

  // Storage schema uses a custom serializer because deserialization needs an allocator.
  static const int64_t STORAGE_SCHEMA_FORMAT_VERSION = 1;
  common::ObIAllocator *allocator_;
  int64_t format_version_;

  union {
    uint32_t info_;
    struct
    {
      uint32_t reserved1_                        : SS_RESERVED1_BITS;
      uint32_t column_info_simplified_           : SS_ONE_BIT;
      uint32_t reserved2_                        : SS_RESERVED2_BITS;
    };
  };
  share::schema::ObTableType table_type_;
  share::schema::ObTableMode table_mode_;
  share::schema::ObIndexType index_type_;
  ObRowStoreType row_store_type_;
  int64_t schema_version_;
  int64_t column_cnt_; // include virtual generated column
  int64_t tablet_size_;
  int64_t pctfree_;
  int64_t block_size_; //KB
  int64_t progressive_merge_round_;
  int64_t progressive_merge_num_;
  ObCompressorType compressor_type_;
  common::ObFixedArray<ObStorageRowkeyColumnSchema, common::ObIAllocator> rowkey_array_; // rowkey column
  common::ObFixedArray<ObStorageColumnSchema, common::ObIAllocator> column_array_; // column schema, including virtual column
  common::ObFixedArray<share::schema::ObSkipIndexAttrWithId, common::ObIAllocator> skip_idx_attr_array_;
  int64_t store_column_cnt_; // NOT include virtual generated column
  share::schema::ObSemiStructEncodingType semistruct_encoding_type_;
  bool is_inited_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObStorageSchema);
};

class ObCreateTabletSchema : public ObStorageSchema
{
public:
  ObCreateTabletSchema()
    : ObStorageSchema(),
      table_id_(common::OB_INVALID_ID),
      index_status_(share::schema::ObIndexStatus::INDEX_STATUS_UNAVAILABLE),
      truncate_version_(OB_INVALID_VERSION)
      {}

  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(common::ObIAllocator &allocator, const char *buf, const int64_t data_len, int64_t &pos);
  int64_t get_serialize_size() const;

  inline bool can_read_index() const 
  { return share::schema::INDEX_STATUS_AVAILABLE == index_status_; }
  uint64_t get_table_id () const
  { return table_id_; }
  int64_t get_truncate_version() const
  { return truncate_version_; }
  bool is_valid() const
  {
    return ObStorageSchema::is_valid() && common::OB_INVALID_ID != table_id_;
  }
  int init(common::ObIAllocator &allocator,
      const share::schema::ObTableSchema &input_schema,
      const bool skip_column_info);
  int init(common::ObIAllocator &allocator,
      const ObCreateTabletSchema &old_schema);
  INHERIT_TO_STRING_KV("ObStorageSchema", ObStorageSchema, K_(table_id), K_(index_status), K_(truncate_version));
private:
  // Persisted table identity for tablet creation.
  uint64_t table_id_;
  // for create index
  share::schema::ObIndexStatus index_status_;
  // for tablet throttling 
  int64_t truncate_version_;
};

template <typename T>
int ObStorageSchema::serialize_schema_array(
    char *buf, const int64_t data_len, int64_t &pos, const common::ObIArray<T> &array) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(serialization::encode_vi64(buf, data_len, pos, array.count()))) {
    STORAGE_LOG(WARN, "Fail to encode column count", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < array.count(); ++i) {
    if (OB_FAIL(array.at(i).serialize(buf, data_len, pos))) {
      STORAGE_LOG(WARN, "Fail to serialize column", K(ret));
    }
  }
  return ret;
}

template <typename T>
int64_t ObStorageSchema::get_array_serialize_length(const common::ObIArray<T> &array) const
{
  int64_t len = 0;
  len += serialization::encoded_length_vi64(array.count());
  for (int64_t i = 0; i < array.count(); ++i) {
    len += array.at(i).get_serialize_size();
  }
  return len;
}

template <typename T>
bool ObStorageSchema::check_column_array_valid(const common::ObIArray<T> &array) const
{
  bool valid_ret = true;
  for (int64_t i = 0; valid_ret && i < array.count(); ++i) {
    if (!array.at(i).is_valid()) {
      valid_ret = false;
      STORAGE_LOG_RET(WARN, OB_INVALID_ERROR, "column item is invalid", K(i), K(array.at(i)));
    }
  }
  return valid_ret;
}

} // namespace storage
} // namespace oceanbase

#endif /* OCEANBASE_STORAGE_STORAGE_SCHEMA_ */
