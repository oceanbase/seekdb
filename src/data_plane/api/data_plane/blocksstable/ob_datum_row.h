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

#ifndef OCEANBASE_DATA_PLANE_BLOCKSSTABLE_OB_DATUM_ROW_H_
#define OCEANBASE_DATA_PLANE_BLOCKSSTABLE_OB_DATUM_ROW_H_

#include <cstdint>

#include "data_plane/blocksstable/ob_storage_datum.h"
#include "share/transaction/ob_tx_id.h"
#include "lib/allocator/page_arena.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace storage
{
struct ObStoreRow;
}
namespace blocksstable
{

enum ObDmlFlag
{
  DF_NOT_EXIST = 0,
  DF_LOCK = 1,
  DF_UPDATE = 2,
  DF_INSERT = 3,
  DF_DELETE = 4,
  DF_MAX = 5,
};

static const char *ObDmlFlagStr[DF_MAX] = {
    "NOT_EXIST",
    "LOCK",
    "UPDATE",
    "INSERT",
    "DELETE"
};

enum ObDmlRowFlagType
{
  DF_TYPE_NORMAL = 0,
  DF_TYPE_INSERT_DELETE = 1,
  DF_TYPE_MAX,
};

static const char *ObDmlTypeStr[DF_TYPE_MAX] = {
    "N",
    "I_D"
};

const char *get_dml_str(ObDmlFlag dml_flag);
void format_dml_str(const int32_t flag, char *str, int len);

struct ObDmlRowFlag
{
  OB_UNIS_VERSION(1);
public:
  ObDmlRowFlag()
    : whole_flag_(0)
  {}
  ObDmlRowFlag(const uint8_t flag)
    : whole_flag_(flag)
  {}
  ObDmlRowFlag(ObDmlFlag flag)
    : whole_flag_(0)
  {
    set_flag(flag);
  }
  ~ObDmlRowFlag() = default;
  OB_INLINE void reset()
  {
    whole_flag_ = 0;
  }
  OB_INLINE void set_flag(ObDmlFlag row_flag, ObDmlRowFlagType flag_type = DF_TYPE_NORMAL)
  {
    reset();
    if (OB_LIKELY(row_flag >= DF_NOT_EXIST && row_flag < DF_MAX)) {
      flag_ = row_flag;
    }
    if (OB_LIKELY(flag_type >= DF_TYPE_NORMAL && flag_type < DF_TYPE_MAX)) {
      flag_type_ = flag_type;
    }
  }
  OB_INLINE bool is_delete() const
  {
    return DF_DELETE == flag_;
  }
  OB_INLINE bool is_lock() const
  {
    return DF_LOCK == flag_;
  }
  OB_INLINE bool is_not_exist() const
  {
    return DF_NOT_EXIST == flag_;
  }
  OB_INLINE bool is_insert() const
  {
    return DF_INSERT == flag_;
  }
  OB_INLINE bool is_update() const
  {
    return DF_UPDATE == flag_;
  }
  OB_INLINE bool is_exist() const
  {
    return is_valid() && !is_not_exist();
  }
  OB_INLINE bool is_exist_without_delete() const
  {
    return is_exist() && !is_delete();
  }
  OB_INLINE bool is_valid() const
  {
    return (DF_TYPE_NORMAL == flag_type_ && DF_DELETE >= flag_)
        || (DF_TYPE_INSERT_DELETE == flag_type_ && (DF_INSERT == flag_ || DF_DELETE == flag_));
  }
  OB_INLINE bool is_extra_delete() const
  {
    return DF_TYPE_INSERT_DELETE != flag_type_ && DF_DELETE == flag_;
  }
  OB_INLINE bool is_insert_delete() const
  {
    return DF_TYPE_INSERT_DELETE == flag_type_ && DF_DELETE == flag_;
  }
  OB_INLINE bool is_upsert() const
  {
    return DF_TYPE_INSERT_DELETE == flag_type_ && DF_INSERT == flag_;
  }
  OB_INLINE void fuse_flag(const ObDmlRowFlag input_flag)
  {
    if (OB_LIKELY(input_flag.is_valid())) {
      if (DF_INSERT == input_flag.flag_) {
        if (DF_DELETE == flag_) {
          flag_type_ = DF_TYPE_INSERT_DELETE;
        } else {
          flag_ = DF_INSERT;
        }
      } else if (DF_DELETE == input_flag.flag_ && DF_DELETE == flag_) {
        if (flag_type_ == DF_TYPE_INSERT_DELETE) {
          flag_type_ = input_flag.flag_type_;
        } else {
        }
      }
    }
  }
  OB_INLINE uint8_t get_serialize_flag() const
  {
    return whole_flag_;
  }
  OB_INLINE ObDmlFlag get_dml_flag() const { return static_cast<ObDmlFlag>(flag_); }
  ObDmlRowFlag &operator=(const ObDmlRowFlag &other)
  {
    if (other.is_valid()) {
      whole_flag_ = other.whole_flag_;
    }
    return *this;
  }

  const char *getFlagStr() const
  {
    const char *ret_str = nullptr;
    if (is_valid()) {
      ret_str = ObDmlFlagStr[flag_];
    } else {
      ret_str = "invalid flag";
    }
    return ret_str;
  }
  OB_INLINE void format_str(char *str, int8_t len) const
  {
    format_dml_str(whole_flag_, str, len);
  }
  OB_INLINE int32_t get_delta() const
  {
    int32_t ret_val = 0;
    if (is_extra_delete()) {
      ret_val = -1;
    } else if (is_insert()) {
      ret_val = 1;
    }
    return ret_val;
  }

  TO_STRING_KV("flag", get_dml_str(static_cast<ObDmlFlag>(flag_)), K_(flag_type));
private:
  bool operator!=(const ObDmlRowFlag &other) const
  {
    return flag_ != other.flag_;
  }

  const static uint8_t OB_FLAG_TYPE_MASK = 0x80;
  const static uint8_t OB_FLAG_MASK = 0x7F;
  union
  {
    uint8_t whole_flag_;
    struct {
      uint8_t flag_      : 7;
      uint8_t flag_type_ : 1;
    };
  };
};

static const int8_t MvccFlagCount = 8;
static const char *ObMvccFlagStr[MvccFlagCount] = {
  "",
  "F",
  "U",
  "S",
  "C",
  "G",
  "L",
  "UNKNOWN"
};

void format_mvcc_str(const int32_t flag, char *str, int len);

struct ObMultiVersionRowFlag
{
  OB_UNIS_VERSION(1);
public:
  union
  {
    uint8_t flag_;
    struct
    {
      uint8_t is_first_        : 1;
      uint8_t is_uncommitted_  : 1;
      uint8_t is_shadow_       : 1;
      uint8_t is_compacted_    : 1;
      uint8_t is_ghost_        : 1;
      uint8_t is_last_         : 1;
      uint8_t reserved_        : 2;
    };
  };

  ObMultiVersionRowFlag() : flag_(0) {}
  ObMultiVersionRowFlag(uint8_t flag) : flag_(flag) {}
  void reset() { flag_ = 0; }
  inline void set_compacted_multi_version_row(const bool value) { is_compacted_ = value; }
  inline void set_last_multi_version_row(const bool value) { is_last_ = value; }
  inline void set_first_multi_version_row(const bool value) { is_first_ = value; }
  inline void set_uncommitted_row(const bool value) { is_uncommitted_ = value; }
  inline void set_ghost_row(const bool value) { is_ghost_ = value; }
  inline void set_shadow_row(const bool value) { is_shadow_ = value; }
  inline bool is_valid() const
  {
    return !is_first_multi_version_row()
        || is_uncommitted_row()
        || is_last_multi_version_row()
        || is_ghost_row()
        || is_shadow_row();
  }
  inline bool is_compacted_multi_version_row() const { return is_compacted_; }
  inline bool is_last_multi_version_row() const { return is_last_; }
  inline bool is_first_multi_version_row() const { return is_first_; }
  inline bool is_uncommitted_row() const { return is_uncommitted_; }
  inline bool is_ghost_row() const { return is_ghost_; }
  inline bool is_shadow_row() const { return is_shadow_; }
  inline void format_str(char *str, int8_t len) const
  {
    format_mvcc_str(flag_, str, len);
  }

  TO_STRING_KV("first", is_first_,
               "uncommitted", is_uncommitted_,
               "shadow", is_shadow_,
               "compact", is_compacted_,
               "ghost", is_ghost_,
               "last", is_last_,
               "reserved", reserved_,
               K_(flag));
};

// Concrete row layout used on the query/storage boundary.  Algorithms that
// operate on the row remain implemented by Storage.
struct ObDatumRow
{
  OB_UNIS_VERSION(1);
public:
  ObDatumRow();
  ~ObDatumRow();
  int init(common::ObIAllocator &allocator, const int64_t capacity, char *trans_info_ptr = nullptr);
  int init(const int64_t capacity);
  void reset();
  void reuse();
  int reserve(const int64_t capacity, const bool keep_data = false);
  int deep_copy(const ObDatumRow &src, common::ObIAllocator &allocator);
  int from_store_row(const storage::ObStoreRow &store_row);
  int shallow_copy(const ObDatumRow &other);
  bool operator==(const ObDatumRow &other) const;

  int is_datums_changed(const ObDatumRow &other, bool &is_changed) const;
  int copy_attributes_except_datums(const ObDatumRow &other);
  OB_INLINE int64_t get_capacity() const { return datum_buffer_.get_capacity(); }
  OB_INLINE int64_t get_column_count() const { return count_; }
  OB_INLINE int64_t get_scan_idx() const { return scan_index_; }
  OB_INLINE bool is_valid() const { return nullptr != storage_datums_ && get_capacity() > 0; }
  OB_INLINE bool check_has_nop_col() const
  {
    for (int64_t i = 0; i < get_column_count(); i++) {
      if (storage_datums_[i].is_nop()) {
        return true;
      }
    }
    return false;
  }
  OB_INLINE transaction::ObTransID get_trans_id() const { return trans_id_; }
  OB_INLINE void set_trans_id(const transaction::ObTransID &trans_id) { trans_id_ = trans_id; }
  OB_INLINE bool is_have_uncommited_row() const { return have_uncommited_row_; }
  OB_INLINE void set_have_uncommited_row(const bool value = true) { have_uncommited_row_ = value; }
  OB_INLINE bool is_ghost_row() const { return mvcc_row_flag_.is_ghost_row(); }
  OB_INLINE bool is_uncommitted_row() const { return mvcc_row_flag_.is_uncommitted_row(); }
  OB_INLINE bool is_compacted_multi_version_row() const { return mvcc_row_flag_.is_compacted_multi_version_row(); }
  OB_INLINE bool is_first_multi_version_row() const { return mvcc_row_flag_.is_first_multi_version_row(); }
  OB_INLINE bool is_last_multi_version_row() const { return mvcc_row_flag_.is_last_multi_version_row(); }
  OB_INLINE bool is_shadow_row() const { return mvcc_row_flag_.is_shadow_row(); }
  OB_INLINE void set_compacted_multi_version_row() { mvcc_row_flag_.set_compacted_multi_version_row(true); }
  OB_INLINE void set_first_multi_version_row() { mvcc_row_flag_.set_first_multi_version_row(true); }
  OB_INLINE void set_last_multi_version_row() { mvcc_row_flag_.set_last_multi_version_row(true); }
  OB_INLINE void set_shadow_row() { mvcc_row_flag_.set_shadow_row(true); }
  OB_INLINE void set_uncommitted_row() { mvcc_row_flag_.set_uncommitted_row(true); }
  OB_INLINE void set_multi_version_flag(const ObMultiVersionRowFlag &flag) { mvcc_row_flag_ = flag; }
  OB_INLINE int32_t get_delta() const { return row_flag_.get_delta(); }

  DECLARE_TO_STRING;

public:
  common::ObArenaAllocator local_allocator_;
  uint16_t count_;
  union {
    struct {
      uint32_t have_uncommited_row_: 1;
      uint32_t fast_filter_skipped_: 1;
      uint32_t reserved_ : 30;
    };
    uint32_t read_flag_;
  };
  ObDmlRowFlag row_flag_;
  ObMultiVersionRowFlag mvcc_row_flag_;
  transaction::ObTransID trans_id_;
  int64_t scan_index_;
  int64_t group_idx_;
  int64_t snapshot_version_;

  ObStorageDatum *storage_datums_;
  ObStorageDatumBuffer datum_buffer_;
  char *trans_info_;
};

} // namespace blocksstable
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_BLOCKSSTABLE_OB_DATUM_ROW_H_
