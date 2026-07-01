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

#ifndef OCEANBASE_COMMON_OB_CELL_WRITER_H_
#define OCEANBASE_COMMON_OB_CELL_WRITER_H_
#include "common/object/ob_obj_type.h"
#include "lib/number/ob_number_v2.h"
#include "lib/ob_define.h"

namespace oceanbase
{
namespace common
{
class ObObj;

/**
 * Used to distinguish four types of storage formats
 * DENSE_SPARSE and DENSE_DENSE are actually implemented with rowkey and regular columns belonging to two different rows
 */
enum ObCompactStoreType
{
  SPARSE, //Sparse format type, with column_id
  DENSE, //Dense format type, without column_id
  DENSE_SPARSE, //Each row consists of two parts, the first part is rowkey, dense, and the latter part is sparse.
  DENSE_DENSE, //Each row consists of two parts, the first part is rowkey, which is dense, and the latter part of ordinary columns is also sparse
  INVALID_COMPACT_STORE_TYPE
};

const uint8_t TEXT_CELL_META_VERSION = 1;

class ObCellWriter
{
public:
  struct CellMeta
  {
    static const uint64_t SF_BIT_TYPE = 6;
    static const uint64_t SF_BIT_ATTR = 2;
    static const uint64_t SF_MASK_TYPE = (0x1UL << SF_BIT_TYPE) - 1;
    static const uint64_t SF_MASK_ATTR = (0x1UL << SF_BIT_ATTR) - 1;
    uint8_t type_: 6;
    uint8_t attr_: 2;
    CellMeta() : type_(0), attr_(0) {}
    OB_INLINE bool need_collation() const
    {
      return ObCellWriter::TEXT_VARCHAR_COLL == attr_ || ObCellWriter::TEXT_SCALE_COLL == attr_;
    }
    OB_INLINE bool is_varchar_text() const
    {
      return ObCellWriter::TEXT_VARCHAR_NO_COLL == attr_ || ObCellWriter::TEXT_VARCHAR_COLL == attr_;
    }
  };
  enum TextAttr
  {
    TEXT_VARCHAR_NO_COLL = 0,
    TEXT_VARCHAR_COLL = 1,
    TEXT_SCALE_NO_COLL = 2,
    TEXT_SCALE_COLL = 3
  };
  enum ExtendAttr
  {
    EA_END_FLAG = 0,
    EA_OTHER = 1
  };
  enum ExtendValue
  {
    EV_NOP_ROW = 0,
    EV_DEL_ROW = 1,
    EV_NOT_EXIST_ROW = 2,
    EV_MIN_CELL = 3,
    EV_MAX_CELL = 4,
    EV_LOCK_ROW = 5
  };
public:
  ObCellWriter();
  virtual ~ObCellWriter() {}
  int init(char *buf, int64_t size, const ObCompactStoreType store_type, const bool old_text_format = false);
  int append(uint64_t column_id, const ObObj &obj, ObObj *clone_obj = NULL);
  int append(const ObObj &obj);
  int extend_buf(char *buf, int64_t size);
  int revert_buf(char *buf, int64_t size);
  void reuse() { pos_ = 0; last_append_pos_ = 0; cell_cnt_ = 0; }
  void reset_text_format(const bool old_text_format) { old_text_format_ = old_text_format;  }
  void set_store_type(const ObCompactStoreType store_type) { store_type_ = store_type; }
  int row_finish();
  inline int64_t get_cell_cnt() { return cell_cnt_; }
  inline int64_t size() const { return pos_; }
  inline char *get_buf() const { return buf_; }
  virtual bool allow_lob_locator() { return true; }
private:
  template<class T>
  int append(const T &value);
  inline int write_int(const ObObj &obj, const enum common::ObObjType meta_type, const int64_t value);
  inline int write_number(const enum common::ObObjType meta_type, const common::number::ObNumber &number, ObObj *clone_obj);
  inline int write_char(const ObObj &obj, const enum common::ObObjType meta_type, const ObString &str, ObObj *clone_obj);
  inline int write_text(const ObObj &obj, const enum common::ObObjType meta_type, const ObString &str, ObObj *clone_obj);
  inline int write_binary(const enum common::ObObjType meta_type, const ObString &str, ObObj *clone_obj);
  inline int write_ext(const int64_t ext_value);
  inline int write_decimal_int(const ObObj &obj, ObObj *clone_obj);
  inline int get_int_byte(int64_t int_value);
  inline bool old_text_format () const { return old_text_format_; }
private:
  char *buf_;
  int64_t buf_size_;
  int64_t pos_;
  //In the case of big row, column append may fail due to insufficient buffer,
  //To solve this problem, the current solution is: temporarily allocate a larger buf, and continue to append the failed column;
  //Retry append of column, need to roll back pos_ before append, otherwise the data will increase, causing consistency problems;
  int64_t last_append_pos_;
  int64_t cell_cnt_;
  ObCompactStoreType store_type_;
  bool old_text_format_;
  bool is_inited_;
};
}//end namespace common
}//end namespace oceanbase
#endif
