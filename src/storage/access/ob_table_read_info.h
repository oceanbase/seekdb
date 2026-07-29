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

#ifndef OB_STORAGE_ACCESS_TABLE_READ_INFO_H_
#define OB_STORAGE_ACCESS_TABLE_READ_INFO_H_

#include "storage/meta_mem/ob_fixed_meta_obj_array.h"
#include "storage/meta_mem/ob_meta_obj_struct.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/ob_storage_schema.h"

namespace oceanbase {
namespace share {
namespace schema {
class ObColumnParam;
class ObColDesc;
class ObColExtend;
}
}
using namespace share::schema;
namespace storage {
class ObStorageSchema;

typedef ObFixedMetaObjArray<ObColumnParam *> Columns;
typedef ObFixedMetaObjArray<int32_t> ColumnsIndex;
typedef ObFixedMetaObjArray<ObColDesc> ColDescArray;
typedef ObFixedMetaObjArray<ObColExtend> ColExtendArray;
struct ObColumnIndexArray
{
public:
  ObColumnIndexArray(const bool rowkey_mode = false, const bool for_memtable = false);
  ~ObColumnIndexArray() { reset(); }
  void reset()
  {
    schema_rowkey_cnt_ = 0;
    column_cnt_ = 0;
    array_.reset();
  }
  bool is_valid() const
  {
    return (rowkey_mode_ && column_cnt_ > schema_rowkey_cnt_ && schema_rowkey_cnt_ > 0)
      || (!rowkey_mode_ && array_.count() > 0);
  }
  int init(
    const int64_t count,
    const int64_t schema_rowkey_cnt,
    ObIAllocator &allocator);
  int init_and_assign(const ObIArray<int32_t> &other, ObIAllocator &allocator);
  int32_t at(int64_t idx) const
  {
    return (*at_func_)(schema_rowkey_cnt_, column_cnt_, idx, array_);
  }

  OB_INLINE int64_t count() const
  {
    return (*count_func_)(column_cnt_, array_);
  }
  int64_t get_deep_copy_size() const;
  int deep_copy(
    char *dst_buf,
    const int64_t buf_size,
    int64_t &pos,
    ObColumnIndexArray &dst_array) const;
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t data_len, int64_t &pos, common::ObIAllocator &allocator);
  int64_t get_serialize_size() const;
  DECLARE_TO_STRING;
private:
  typedef int64_t (*COUNT_FUNC)(uint32_t, const ObFixedMetaObjArray<int32_t> &);
  typedef int32_t (*AT_FUNC)(uint32_t, uint32_t, int64_t, const ObFixedMetaObjArray<int32_t> &);
public:
  const static uint8_t COLUMN_INDEX_ARRAY_VERSION = 1;
  uint8_t version_;
  bool rowkey_mode_;
  bool for_memtable_;
  uint8_t reserved_;
  uint32_t schema_rowkey_cnt_;
  uint32_t column_cnt_;
  ObFixedMetaObjArray<int32_t> array_;
private:
  COUNT_FUNC count_func_;
  AT_FUNC at_func_;
};
class ObITableReadInfo
{
public:
  ObITableReadInfo() = default;
  virtual ~ObITableReadInfo() = default;
  virtual int64_t get_schema_column_count() const = 0;
  virtual int64_t get_seq_read_column_count() const = 0;
  virtual int64_t get_request_count() const = 0;
  virtual int64_t get_schema_rowkey_count() const = 0;
  virtual int64_t get_rowkey_count() const = 0;
  virtual int64_t get_group_idx_col_index() const = 0;
  virtual int64_t get_trans_col_index() const = 0;
  virtual const common::ObIArray<ObColDesc> &get_columns_desc() const = 0;
  virtual const ObColumnIndexArray &get_columns_index() const = 0;
  virtual const ObColumnIndexArray &get_memtable_columns_index() const = 0;
  virtual const blocksstable::ObStorageDatumUtils &get_datum_utils() const = 0;
  virtual const common::ObIArray<ObColumnParam *> *get_columns() const = 0;
  virtual const common::ObIArray<ObColExtend> *get_columns_extend() const = 0;
  virtual bool is_access_rowkey_only() const = 0;
  virtual bool need_truncate_filter() const = 0;
  virtual bool is_valid() const = 0;
  virtual void reset() = 0;
  DECLARE_PURE_VIRTUAL_TO_STRING;
};

class ObReadInfoStruct : public ObITableReadInfo
{
public:
  ObReadInfoStruct(const bool rowkey_mode = false)
    : ObITableReadInfo(),
      is_inited_(false),
      allocator_(nullptr),
      schema_column_count_(0),
      format_version_(READ_INFO_FORMAT_VERSION),
      reserved_(0),
      schema_rowkey_cnt_(0),
      rowkey_cnt_(0),
      cols_desc_(),
      cols_index_(rowkey_mode, false/*for_memtable*/),
      memtable_cols_index_(rowkey_mode, true/*for_memtable*/),
      datum_utils_()
  {}
  virtual ~ObReadInfoStruct() { reset(); }
  virtual bool is_valid() const override
  {
    return is_inited_
        && READ_INFO_FORMAT_VERSION == format_version_
        && schema_rowkey_cnt_ <= cols_desc_.count()
        && 0 < cols_desc_.count()
        && 0 < cols_index_.count()
        && schema_rowkey_cnt_ <= schema_column_count_
        && datum_utils_.is_valid();
  }
  virtual void reset() override;

  OB_INLINE virtual int64_t get_schema_column_count() const override { return schema_column_count_; }
  OB_INLINE virtual int64_t get_schema_rowkey_count() const override { return schema_rowkey_cnt_; }
  OB_INLINE virtual int64_t get_rowkey_count() const override { return rowkey_cnt_; }
  OB_INLINE virtual const common::ObIArray<ObColDesc> &get_columns_desc() const override
  { return cols_desc_; }
  OB_INLINE virtual int64_t get_request_count() const override
  { return cols_desc_.count(); }
  OB_INLINE virtual const ObColumnIndexArray &get_columns_index() const override
  { return cols_index_; }
  OB_INLINE virtual const ObColumnIndexArray &get_memtable_columns_index() const override
  { return memtable_cols_index_; }
  OB_INLINE virtual const blocksstable::ObStorageDatumUtils &get_datum_utils() const override { return datum_utils_; }
  OB_INLINE virtual int64_t get_group_idx_col_index() const override
  {
    return OB_INVALID_INDEX;
  }
  OB_INLINE virtual int64_t get_trans_col_index() const override
  {
    OB_ASSERT_MSG(false, "ObReadInfoStruct dose not promise trans col index");
    return OB_INVALID_INDEX;
  }
  OB_INLINE virtual int64_t get_seq_read_column_count() const override
  {
    OB_ASSERT_MSG(false, "ObReadInfoStruct dose not promise seq read column count");
    return OB_INVALID_INDEX;
  }
  OB_INLINE virtual const common::ObIArray<ObColumnParam *> *get_columns() const override
  {
    OB_ASSERT_MSG(false, "ObReadInfoStruct dose not promise columns array");
    return nullptr;
  }
  OB_INLINE virtual bool is_access_rowkey_only() const override
  {
    OB_ASSERT_MSG(false, "ObReadInfoStruct dose not promise rowkey info");
    return false;
  }
  OB_INLINE virtual const common::ObIArray<ObColExtend> *get_columns_extend() const override
  {
    OB_ASSERT_MSG(false, "ObReadInfoStruct dose not promise columns extend array");
    return nullptr;
  }
  virtual bool need_truncate_filter() const override
  {
    OB_ASSERT_MSG(false, "ObReadInfoStruct dose not promise need truncate filter");
    return false;
  }
  DECLARE_VIRTUAL_TO_STRING;
  void init_basic_info(const int64_t schema_column_count,
                       const int64_t schema_rowkey_cnt,
                       const bool is_global_index_table);
  int prepare_arrays(common::ObIAllocator &allocator,
                     const common::ObIArray<ObColDesc> &cols_desc,
                     const int64_t col_cnt);
protected:
  static constexpr int64_t READ_INFO_FORMAT_VERSION = 5;
  static const int32_t READ_INFO_ONE_BIT = 1;
  static const int32_t READ_INFO_RESERVED_BITS = 15;

  bool is_inited_;
  ObIAllocator *allocator_;
  // distinguish schema changed by schema column count
  union {
    uint64_t info_;
    struct {
      uint32_t schema_column_count_;
      uint16_t format_version_;
      uint16_t is_global_index_table_  : READ_INFO_ONE_BIT; // only used for rowkey_read_info in ObTablet
      uint16_t reserved_               : READ_INFO_RESERVED_BITS;
    };
  };
  int64_t schema_rowkey_cnt_;
  int64_t rowkey_cnt_;
  ColDescArray cols_desc_;
  ObColumnIndexArray cols_index_; // col index in sstable
  ObColumnIndexArray memtable_cols_index_; // there is no multi verison rowkey col in memtable
  blocksstable::ObStorageDatumUtils datum_utils_;
};

class ObTableReadInfo : public ObReadInfoStruct
{
public:
  ObTableReadInfo();
  virtual ~ObTableReadInfo();
  virtual void reset() override;
  /*
   * schema_rowkey_cnt: schema row key count
   * cols_desc: access col descs
   * storage_cols_index: access-column index in the stored row
   * cols_param: access column params
   */
  // could used for query memtable/sstable
  int init(
      common::ObIAllocator &allocator,
      const int64_t schema_column_count,
      const int64_t schema_rowkey_cnt,
      const common::ObIArray<ObColDesc> &cols_desc,
      const common::ObIArray<int32_t> *storage_cols_index,
      const common::ObIArray<ObColumnParam *> *cols_param = nullptr,
      const common::ObIArray<ObColExtend> *cols_extend = nullptr,
      const bool need_truncate_filter = false);
  virtual OB_INLINE bool is_valid() const override
  {
    return ObReadInfoStruct::is_valid()
        && cols_desc_.count() == cols_index_.count()
        && schema_rowkey_cnt_ <= seq_read_column_count_
        && seq_read_column_count_ <= cols_desc_.count();
  }
  OB_INLINE virtual int64_t get_trans_col_index() const override
  { return trans_col_index_; }
  OB_INLINE int64_t get_group_idx_col_index() const
  { return group_idx_col_index_; }
  OB_INLINE int64_t get_seq_read_column_count() const
  { return seq_read_column_count_; }
  virtual const common::ObIArray<ObColumnParam *> *get_columns() const
  { return &cols_param_; }
  OB_INLINE bool is_access_rowkey_only() const override
  { return max_col_index_ < rowkey_cnt_; }
  OB_INLINE virtual const ObColumnIndexArray &get_memtable_columns_index() const override
  {
    OB_ASSERT_MSG(!mock_sstable_query_, "ObTableReadInfo dose not promise memtable columns index");
    return memtable_cols_index_;
  }
  OB_INLINE virtual const common::ObIArray<ObColExtend> *get_columns_extend() const override
  {
    return &cols_extend_;
  }
  // this func only called in query
  OB_INLINE virtual int64_t get_request_count() const override
  { return cols_desc_.count(); }
  virtual bool need_truncate_filter() const override
  { return need_truncate_filter_; }
  int deserialize(
      common::ObIAllocator &allocator,
      const char *buf,
      const int64_t data_len,
      int64_t &pos);
  int serialize(
      char *buf,
      const int64_t buf_len,
      int64_t &pos) const;
  int64_t get_serialize_size() const;
  DECLARE_VIRTUAL_TO_STRING;

private:
  DISALLOW_COPY_AND_ASSIGN(ObTableReadInfo);
  int init_datum_utils(common::ObIAllocator &allocator);
  int init_pre_check(
      const int64_t schema_column_count,
      const int64_t schema_rowkey_cnt,
      const common::ObIArray<ObColDesc> &cols_desc,
      const common::ObIArray<int32_t> *storage_cols_index,
      const common::ObIArray<ObColumnParam *> *cols_param = nullptr,
      const common::ObIArray<ObColExtend> *cols_extend = nullptr);
  void inner_gene_cols_index_by_col_descs(
    const int64_t schema_rowkey_cnt,
    const common::ObIArray<ObColDesc> &cols_desc,
    const common::ObIArray<int32_t> *storage_cols_index);
private:
  // distinguish schema changed by schema column count
  int64_t trans_col_index_;
  int64_t group_idx_col_index_;
  // the count of common prefix between request columns and store columns
  int64_t seq_read_column_count_;
  int64_t max_col_index_;
  Columns cols_param_;
  ColExtendArray cols_extend_;
  bool mock_sstable_query_;
  bool need_truncate_filter_;
};

class ObRowkeyReadInfo final : public ObReadInfoStruct
{
public:
  ObRowkeyReadInfo();
  virtual ~ObRowkeyReadInfo() {}

  int init(
      common::ObIAllocator &allocator,
      const int64_t schema_column_count,
      const int64_t schema_rowkey_cnt,
      const common::ObIArray<ObColDesc> &rowkey_col_descs,
      const bool is_global_index = false);
  OB_INLINE virtual int64_t get_seq_read_column_count() const override
  { return get_request_count(); }
  OB_INLINE virtual int64_t get_trans_col_index() const override
  { return schema_rowkey_cnt_; }
  virtual int64_t get_request_count() const override;
  OB_INLINE bool is_access_rowkey_only() const override
  { return false; }
  OB_INLINE bool is_global_index_table() const { return is_global_index_table_; }
  int deep_copy(char *buf, const int64_t buf_len, ObRowkeyReadInfo *&value) const;
  int64_t get_deep_copy_size() const;
  int deserialize(
      common::ObIAllocator &allocator,
      const char *buf,
      const int64_t data_len,
      int64_t &pos);
  int serialize(
      char *buf,
      const int64_t buf_len,
      int64_t &pos) const;
  int64_t get_serialize_size() const;
  DISALLOW_COPY_AND_ASSIGN(ObRowkeyReadInfo);
};


}
}
#endif //OB_STORAGE_ACCESS_TABLE_READ_INFO_H_
