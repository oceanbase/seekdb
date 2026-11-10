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

#pragma once

#include "lib/ob_define.h"
#include "lib/string/ob_string.h"
#include "lib/net/ob_addr.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace common
{

struct ObLobId
{
  ObLobId()
    : tablet_id_(0 /* ObTabletID::INVALID_TABLET_ID */), lob_id_(0)
  {}
  bool operator ==(const ObLobId &other) const;
  bool operator !=(const ObLobId &other) const;
  TO_STRING_KV(K_(tablet_id), K_(lob_id));
  void reset();
  inline bool is_valid() const {return tablet_id_ != 0 && lob_id_ != 0;}
  uint64_t tablet_id_;
  uint64_t lob_id_;
};

struct ObLobDataOutRowCtx
{
  static const int64_t OUTROW_LOB_CHUNK_SIZE_UNIT = 1024; // 1KB
  enum OpType
  {
    SQL = 0, // all sql op
    APPEND,
    INSERT,
    WRITE,
    ERASE,
    EMPTY_SQL, // lob col not change in full sql update, out row ctx is empty
    DIFF,
    EXT_INFO_LOG,  // only used in new row value
    VALID_OLD_VALUE_EXT_INFO_LOG,
    VALID_OLD_VALUE,
    DIFF_V2, // add lob id in ext info log, so deprecated old, use new diff type
  };
  ObLobDataOutRowCtx()
    : is_full_(0), op_(0), offset_(0), check_sum_(0), seq_no_st_(0), seq_no_cnt_(0),
      del_seq_no_cnt_(0), modified_len_(0), first_meta_offset_(0), chunk_size_(0), reserved_(0)
  {}
  TO_STRING_KV(K_(is_full), K_(op), K_(offset), K_(check_sum), K_(seq_no_st), K_(seq_no_cnt),
    K_(del_seq_no_cnt), K_(modified_len), K_(first_meta_offset), K_(chunk_size), K_(reserved));
  uint64_t is_full_ : 1;
  uint64_t op_ : 8;
  uint64_t offset_ : 55;
  uint64_t check_sum_;
  int64_t seq_no_st_;
  uint32_t seq_no_cnt_;
  uint32_t del_seq_no_cnt_; // for sql update
  uint64_t modified_len_;
  uint32_t first_meta_offset_ : 24;
  uint32_t chunk_size_ : 8;   // unit is kb
  // this is 8 bytes aligned, so left 4 byte
  // and this field is added later when bug is found, and may be a random value
  uint32_t reserved_;

  bool is_empty_sql() const { return OpType::EMPTY_SQL == op_; }
  bool is_diff_v1() const { return OpType::DIFF == op_;}
  bool is_diff() const { return OpType::DIFF == op_ || OpType::DIFF_V2 == op_; }
  bool is_ext_info_log() const { return OpType::EXT_INFO_LOG == op_; }
  bool is_valid_old_value_ext_info_log() const { return OpType::VALID_OLD_VALUE_EXT_INFO_LOG == op_; }
  bool is_valid_old_value() const { return OpType::VALID_OLD_VALUE == op_; }

  int64_t get_real_chunk_size() const;
};

struct ObLobData
{
  ObLobData()
    : id_(), byte_size_(0)
  {}
  bool operator ==(const ObLobData &other) const;
  TO_STRING_KV(K_(id), K_(byte_size));
  ObLobId id_;
  uint64_t byte_size_;
  char buffer_[0];
};

struct ObLobCommon
{
  static const int64_t LOB_DATA_VERSION = 1;
  ObLobCommon()
    : version_(LOB_DATA_VERSION), is_init_(0), is_empty_(0), in_row_(1),
      reserved_opt_encrypt_(0), opt_compress_(0), opt_deduplicate_(0), has_content_type_(0),
      use_big_endian_(1), is_mem_loc_(0), reserve_(0)
  {}
  TO_STRING_KV(K_(version), K_(is_init), K_(is_empty), K_(in_row),
      K_(opt_compress), K_(opt_deduplicate), K_(has_content_type), K_(use_big_endian), K_(is_mem_loc),
      K_(reserve));
  int64_t get_handle_size(int64_t byte_size) const
  {
    int64_t s = static_cast<int64_t>(offsetof(ObLobCommon, buffer_));
    if (is_init_) {
      s += sizeof(ObLobData);
    }
    if (in_row_) {
      s += byte_size;
    } else {
      s += sizeof(ObLobDataOutRowCtx) + sizeof(uint64_t);
    }
    return s;
  }
  int64_t get_byte_size(int64_t handle_size) const
  {
    int64_t s = handle_size;
    if (is_init_) {
      const ObLobData *lob_data = reinterpret_cast<const ObLobData*>(buffer_);
      s = lob_data->byte_size_;
    } else {
      s -= static_cast<int64_t>(offsetof(ObLobCommon, buffer_));
    }
    return s;
  }
  const char* get_inrow_data_ptr() const
  {
    const char* ptr = buffer_;
    if (is_init_) {
      const ObLobData *lob_data = reinterpret_cast<const ObLobData*>(buffer_);
      ptr = lob_data->buffer_;
    }
    return ptr;
  }

  // Get full size of outrow lobs if it is built inrow, only used in reading of systables
  static int64_t calc_inrow_handle_size(bool is_init, int64_t byte_size)
  {
    int64_t s = static_cast<int64_t>(offsetof(ObLobCommon, buffer_));
    if (is_init) {
      s += sizeof(ObLobData);
    }
    s += byte_size;
    return s;
  }
  OB_INLINE bool is_valid() const
  {
    bool bret = (version_ == LOB_DATA_VERSION) && (reserve_ == 0);
    if (!in_row_) {
      bret = (bret && is_init_);
    }
    return bret;
  }
  uint32_t version_ : 8;
  /*flag start*/
  uint32_t is_init_ : 1;
  uint32_t is_empty_ : 1;
  uint32_t in_row_ : 1;
  // Keep the removed encryption flag's bit reserved for on-disk compatibility.
  uint32_t reserved_opt_encrypt_ : 1;
  uint32_t opt_compress_ : 1;
  uint32_t opt_deduplicate_ : 1;
  uint32_t has_content_type_ : 1;
  uint32_t use_big_endian_ : 1;
  uint32_t is_mem_loc_ : 1;
  uint32_t reserve_ : 15;
  /*flag end*/
  char buffer_[0];
};

//We always get/convert/build locator from existing buffer
#define LOB_DEFAULT_FLAGS UINT16_C(0)
#define LOB_COMPAT_MODE_FLAG (INT64_C(1) << 0) // 1 means old heap table without rowid, fake locator
#define LOB_OPEN_MODE_FLAG (INT64_C(1) << 1) // 0 means lob_readwrite, 1 means lob_readonly
#define LOB_IS_OPEN_FLAG (INT64_C(1) << 2) // 0 means is close, 1 means is open

// New In-Memory LobLocator
enum ObMemLobType
{
  INVALID_LOB = 0,
  PERSISTENT_LOB = 1,
  TEMP_FULL_LOB = 2,
  TEMP_DELTA_LOB = 3,
  MAX_LOB_TYPE
};

// Memory Locator V2, Common Header, sz/mo6g8w
// Notice: Do not add or remove fields from this sturct!
// 8 bytes
struct ObMemLobCommon
{
  // locator version 2, the old was 1
  static const uint8_t MEM_LOB_LOCATOR_VERSION = 2;
  static const uint32_t MAGIC_CODE2 = 0x7F7FABCD; // LOB magic for debug

  ObMemLobCommon(ObMemLobType type, bool is_simple) :
    lob_common_(), version_(MEM_LOB_LOCATOR_VERSION), type_(type), read_only_(0),
    has_inrow_data_(1), is_open_(0), is_simple_(is_simple), has_extern_(0), is_freed_(0), reserved_(0)
  { lob_common_.is_mem_loc_ = 1; }

  OB_INLINE void set_extern(bool has_extern) { has_extern_ = has_extern ? 1 : 0; };
  OB_INLINE void set_read_only(bool is_read_only) { read_only_ = is_read_only ? 1 : 0; }
  OB_INLINE void set_has_inrow_data(bool has_inrow_data) { has_inrow_data_ =  has_inrow_data ? 1 : 0 ; }
  OB_INLINE void set_open(bool is_open) { is_open_ = is_open ? 1 : 0; }
  OB_INLINE void set_simple(bool is_simple) { is_simple_ = is_simple ? 1 : 0; }
  OB_INLINE void set_freed(bool freed) { is_freed_ = freed ? 1 : 0; };

  OB_INLINE bool is_read_only() { return read_only_ == 1; }
  OB_INLINE bool has_inrow_data() { return has_inrow_data_ == 1; }
  OB_INLINE bool is_open() { return is_open_ == 1; }
  OB_INLINE bool is_simple() { return is_simple_ == 1; }
  OB_INLINE bool has_extern() {return has_extern_ == 1; }
  OB_INLINE bool is_freed() {return is_freed_ == 1; }
  OB_INLINE bool is_persist() { return type_ == PERSISTENT_LOB; }
  OB_INLINE bool is_temporary_full() { return type_ == TEMP_FULL_LOB; }
  OB_INLINE bool is_temporary_delta() { return type_ == TEMP_DELTA_LOB; }

  OB_INLINE bool has_rowkey_addr() { return has_extern(); } // Notice: rowkey maybe empty string

  OB_INLINE bool is_valid() const
  {
    bool bret = (lob_common_.is_valid() && lob_common_.is_mem_loc_ == 1)
                && (version_ == MEM_LOB_LOCATOR_VERSION)
                && (type_ > INVALID_LOB && type_ <= TEMP_DELTA_LOB)
                && ((is_simple_ & has_extern_) != 1);
    if (!bret) {
      COMMON_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "Invalid lob locator v2!",
        K(lob_common_), K(version_), K(type_), K(is_simple_), K(has_extern_), K(lbt()));
    }
    return bret;
  }

  TO_STRING_KV(K_(lob_common), K_(type), K_(read_only), K_(has_inrow_data), K_(is_open), K_(is_simple),
               K_(has_extern), K_(is_freed), K_(reserved), K_(version));

  ObLobCommon lob_common_;

  // version, type, flags total 4 bytes, correspoinding to the old locator version
  uint32_t version_ : 8;
  uint32_t type_ : 4;     // ObMemLobType (Persistent/TmpFull/TmpDelta)

  // flags, 20 bits
  uint32_t read_only_ : 1;
  uint32_t has_inrow_data_ : 1;  // Indicate whether the lob is in-Memory inrow
                                 // However, it may not be the same with the flag in disk locator.
                                 // compatibility flag for client
  uint32_t is_open_ : 1;   // Indicate whether the persist lob is open
  uint32_t is_simple_ : 1; // Indicate whether the lob has this common part only. Used for inrow lobs
                           // which do not need rowkey (Only used in mysql modes now)
  uint32_t has_extern_ : 1; // Indicate whether the lob locator has extern segment
  uint32_t is_freed_ : 1; // Indicate whether the temp lob locator has been freed
  uint32_t reserved_  : 14;


  char data_[0];
};

struct ObMemLobExternFlags
{
  ObMemLobExternFlags() :
    has_tx_info_(0), has_location_info_(1), has_read_snapshot_(1), read_latest_(0), reserved_(0)
  {}

  ObMemLobExternFlags(bool enable) :
    has_tx_info_(0), has_location_info_(enable), has_read_snapshot_(enable), read_latest_(0), reserved_(0)
  {}

  ObMemLobExternFlags(const ObMemLobExternFlags &flags) { *this = flags; }

  OB_INLINE bool is_empty() const
  {
    return (*(reinterpret_cast<const uint16_t *>(this)) == 0);
  }

  OB_INLINE bool set_empty()
  {
    return (*(reinterpret_cast<uint16_t *>(this)) = 0);
  }

  TO_STRING_KV(K_(has_tx_info), K_(has_location_info), K_(has_read_snapshot), K_(read_latest), K_(reserved));

  uint16_t has_tx_info_ : 1; // Indicate whether tx info exists
  uint16_t has_location_info_ : 1; // Indicate whether has cid exists (reserved)
  uint16_t has_read_snapshot_ : 1; // Indicate whether has ObTxReadSnapshot
  uint16_t read_latest_ : 1; // Preserve the scan's read-latest mode for out-of-row LOB reads.
  uint16_t reserved_ : 12;
};

// Memory Locator V2, Extern Header:
// Notice: Do not add or remove fields from this sturct!
// 32 bytes
struct ObMemLobExternHeader
{
  ObMemLobExternHeader(const ObMemLobExternFlags &flags, uint16_t rowkey_size) :
    reserved_(0), table_id_(0), column_idx_(0), flags_(flags),
    rowkey_size_(rowkey_size), payload_offset_(0), payload_size_(0)
  {}

  ObMemLobExternHeader(uint64_t table_id, uint32_t column_idx, uint16_t rowkey_size,
                       uint32_t payload_offset, uint32_t payload_size) :
    reserved_(0), table_id_(table_id), column_idx_(column_idx), flags_(),
    rowkey_size_(rowkey_size),
    payload_offset_(payload_offset), payload_size_(payload_size)
  {}

  TO_STRING_KV(K_(reserved), K_(table_id), K_(column_idx), K_(flags), K_(rowkey_size),
               K_(payload_offset), K_(payload_size));

  int64_t reserved_; // 8 bytes snpahot version is not enough for read, just reserved
  uint64_t table_id_;
  uint32_t column_idx_;

  ObMemLobExternFlags flags_;

  uint16_t rowkey_size_; // Max rowkey size is 16KB

  uint32_t payload_offset_;  // total length of extern fields, rowkey, and disk locator
  uint32_t payload_size_;    // total inrow data size, <= 4GB

  char data_[0];
};

// Memory Locator V2, Extern Body Structs
struct ObMemLobTxInfo
{
  ObMemLobTxInfo(){}
  ObMemLobTxInfo(int64_t snapshot_ver, int64_t tx_id, int64_t scn) :
    snapshot_version_(snapshot_ver), snapshot_tx_id_(tx_id), snapshot_seq_(scn)
  {}
  TO_STRING_KV(K_(snapshot_version), K_(snapshot_tx_id), K_(snapshot_seq));

  int64_t snapshot_version_;
  int64_t snapshot_tx_id_;
  int64_t snapshot_seq_;
  char data_[0];
};

struct ObMemLobReadSnapshot
{
  ObMemLobReadSnapshot(): version_(0), size_(0) {}

  TO_STRING_KV(K_(version), K_(size));
  uint32_t version_ : 4;
  uint32_t size_ : 28;
  char data_[0];
};

struct ObMemLobLocationInfo
{
  ObMemLobLocationInfo(){}
  ObMemLobLocationInfo(int64_t table_id, ObCollationType collation_id) :
    tablet_id_(table_id), cid_(collation_id)
  {}
  TO_STRING_KV(K_(tablet_id), K_(cid));

  int64_t tablet_id_;
  ObCollationType cid_; // charset for dbmslob
  char data_[0];
};


OB_INLINE void validate_has_lob_header(const bool &has_header)
{
#ifdef VALIDATE_LOB_HEADER
  // Debug only, disabled by default
  OB_ASSERT(has_header == true);
#endif
};

// In-Memory LobLocator Content:
// ObMemLobCommon       (all lobs)|
// ObMemLobExternHeader (has extern flag) |
// externs length       (uint16_t, extern flags) |
// ObMemLobExterns      (extern flags) |
// Rowkey & Addr        (has extern flag) |
// Disk locator         (all non-simple lobs)|
// inline buffer
struct ObLobLocatorV2
{
OB_UNIS_VERSION(1);
public:
  static const uint32_t MEM_LOB_COMMON_HEADER_LEN = sizeof(ObMemLobCommon);
  static const uint32_t MEM_LOB_EXTERN_HEADER_LEN = sizeof(ObMemLobExternHeader);
  static const uint32_t MEM_LOB_EXTERN_TXINFO_LEN = sizeof(ObMemLobTxInfo);
  static const uint32_t MEM_LOB_EXTERN_LOCATIONINFO_LEN = sizeof(ObMemLobLocationInfo);
  static const uint16_t MEM_LOB_EXTERN_SIZE_LEN = sizeof(uint16_t);
  static const int64_t DISK_LOB_OUTROW_FULL_SIZE = sizeof(ObLobCommon) + sizeof(ObLobData) + sizeof(ObLobDataOutRowCtx) + sizeof(uint64_t);

  ObLobLocatorV2() : ptr_(NULL), size_(0), has_lob_header_(true) {}
  ObLobLocatorV2(char *loc_ptr, uint32_t loc_size, bool has_lob_header = true) :
    ptr_(loc_ptr), size_(loc_size), has_lob_header_(has_lob_header)
  {
    if (loc_ptr == NULL || loc_size == 0) {
    } else {
      validate_has_lob_header(has_lob_header_);
    }
  }
  ObLobLocatorV2(ObString &lob_str, bool has_lob_header = true) :
    ptr_(lob_str.ptr()), size_(lob_str.length()), has_lob_header_(has_lob_header)
  {
    if (lob_str.empty()) {
    } else {
      validate_has_lob_header(has_lob_header_);
    }
  }
  ObLobLocatorV2(const ObString &lob_str, bool has_lob_header = true) :
    ptr_(const_cast<char *>(lob_str.ptr())), size_(lob_str.length()), has_lob_header_(has_lob_header)
  {
    if (lob_str.empty()) {
    } else {
      validate_has_lob_header(has_lob_header_);
    }
  }

  ~ObLobLocatorV2() {}

  DECLARE_TO_STRING;

  OB_INLINE bool is_lob_disk_locator() const
  {
    // Notice: should be called only when ptr_ is not null
    ObLobCommon *loc = reinterpret_cast<ObLobCommon *>(ptr_);
    return has_lob_header_ && (loc != nullptr) && (size_ >= sizeof(ObLobCommon)) &&
           (loc->is_valid()) && (loc->is_mem_loc_ == 0);
  }

  OB_INLINE bool is_inrow_disk_lob_locator() const
  {
    // Notice: should be called only when ptr_ is not null
    ObLobCommon *loc = reinterpret_cast<ObLobCommon *>(ptr_);
    return has_lob_header_ && (loc != nullptr) && (size_ >= sizeof(ObLobCommon)) &&
           (loc->is_valid()) && (loc->is_mem_loc_ == 0) && (loc->in_row_);
  }

  OB_INLINE bool is_valid(bool is_assert = true) const
  {
    bool bret = true;
    if (is_assert) {
      validate_has_lob_header(has_lob_header_); // remove later
    }
    if (OB_UNLIKELY(!has_lob_header_ || size_ == 0)) {
      // cannot validate without header or len is zero
    } else if (OB_NOT_NULL(ptr_) && is_lob_disk_locator()) {
      // cannot validate with disk
    } else {
      ObMemLobCommon *loc = reinterpret_cast<ObMemLobCommon *>(ptr_);
      bret = (OB_NOT_NULL(loc) && size_ >= MEM_LOB_COMMON_HEADER_LEN) ? loc->is_valid() : false;
      if (bret && loc->has_extern() && size_ < MEM_LOB_COMMON_HEADER_LEN + MEM_LOB_EXTERN_HEADER_LEN) {
        bret = false;
      }
      if (!bret) {
        COMMON_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "Invalid lob locator!", KP(ptr_), K(size_));
        if (OB_NOT_NULL(loc) && is_assert) {
          OB_ASSERT(0);
        }
      }
    }
    return bret;
  }

  // remove if not used
  void assign_ptr(const ObMemLobCommon *lobv2, uint32 len, bool has_lob_header = true)
  {
    ptr_ = reinterpret_cast<char *>(const_cast<ObMemLobCommon *>(lobv2));
    size_ = len;
    has_lob_header_ = has_lob_header;
  }

  void assign_buffer(char *loc_buff, uint32 len, bool has_lob_header = true)
  {
    ptr_ = loc_buff;
    size_ = len;
    has_lob_header_ = has_lob_header;
  }

  // Notice: disk_lob_full_size = (disk locator header size if any) + inline buffer
  static uint32_t calc_locator_full_len(const ObMemLobExternFlags &flags,
                                        uint32_t rowkey_size,
                                        uint32_t disk_lob_full_size,
                                        uint32_t read_snapshot_size,
                                        bool is_simple);

  // interfaces for write
  // fill empty lob locator
  int fill(ObMemLobType type,
           const ObMemLobExternFlags &flags,
           const ObString &rowkey_str,
           const ObLobCommon *disk_loc,
           uint32_t disk_lob_full_size,
           uint32_t disk_lob_header_size,
           uint32_t read_snapshot_size,
           bool is_simple);

  int set_table_info(const uint64_t &table_id, const uint32_t &column_idx);
  int set_payload_data(const ObString& payload);
  int set_payload_data(const ObLobCommon *lob_comm, const ObString& payload);
  int set_tx_info(const ObMemLobTxInfo &tx_info);
  int set_location_info(const ObMemLobLocationInfo &location_info);

  // interfaces for read
  // Notice: all the following functions should be called after is_valid() or fill()
  int get_mem_locator(ObMemLobCommon *&mem_loc) const;
  int get_extern_header(ObMemLobExternHeader *&extern_header) const;
  int get_rowkey(ObString &rowkey_str) const;
  int get_disk_locator(ObLobCommon *&disk_loc) const;
  int get_disk_locator(ObString &disc_loc_buff) const;

  int get_inrow_data(ObString &inrow_data) const;
  int get_lob_data_byte_len(int64_t &len) const;
  int get_tx_info(ObMemLobTxInfo *&tx_info) const;
  int get_location_info(ObMemLobLocationInfo *&location_info) const;
  int get_read_snapshot_data(ObString &data) const;
  int get_real_locator_len(int64_t &real_len) const;
  int get_chunk_size(int64_t &chunk_size) const;

  bool is_empty_lob() const;
  bool is_inrow() const;
  bool is_null() const { return ptr_ == NULL || size_ == 0; }

  OB_INLINE bool is_readonly() const
  {
    validate_has_lob_header(has_lob_header_);
    return has_lob_header_ && OB_NOT_NULL(ptr_) &&
           !is_lob_disk_locator() && size_ >= MEM_LOB_COMMON_HEADER_LEN &&
           (reinterpret_cast<ObMemLobCommon *>(ptr_))->is_read_only();
  }

  OB_INLINE bool is_freed() const
  {
    validate_has_lob_header(has_lob_header_);
    return has_lob_header_ && OB_NOT_NULL(ptr_) &&
           !is_lob_disk_locator() && size_ >= MEM_LOB_COMMON_HEADER_LEN &&
           (reinterpret_cast<ObMemLobCommon *>(ptr_))->is_freed();
  }

  OB_INLINE bool has_inrow_data() const
  {
    bool bret = false;
    if (!has_lob_header_ || size_ == 0) {
      bret = true;
    } else if (OB_NOT_NULL(ptr_)) {
      if (is_lob_disk_locator()) {
        bret = (reinterpret_cast<ObLobCommon *>(ptr_))->in_row_;
      } else if (size_ >= MEM_LOB_COMMON_HEADER_LEN) {
        bret = (reinterpret_cast<ObMemLobCommon *>(ptr_))->has_inrow_data();
      }
    }
    return bret;
  }

  OB_INLINE bool is_open() const
  {
    validate_has_lob_header(has_lob_header_);
    return has_lob_header_ && OB_NOT_NULL(ptr_) &&
           !is_lob_disk_locator() && size_ >= MEM_LOB_COMMON_HEADER_LEN &&
           (reinterpret_cast<ObMemLobCommon *>(ptr_))->is_open();
  }

  OB_INLINE bool is_simple() const
  {
    validate_has_lob_header(has_lob_header_);
    return has_lob_header_ && OB_NOT_NULL(ptr_) &&
           !is_lob_disk_locator() && size_ >= MEM_LOB_COMMON_HEADER_LEN &&
           (reinterpret_cast<ObMemLobCommon *>(ptr_))->is_simple();
  }

  OB_INLINE bool has_extern() const
  {
    validate_has_lob_header(has_lob_header_);
    return has_lob_header_ && OB_NOT_NULL(ptr_) &&
           !is_lob_disk_locator() && size_ >= MEM_LOB_COMMON_HEADER_LEN &&
           (reinterpret_cast<ObMemLobCommon *>(ptr_))->has_extern();
  }

  // all lob types without header are treated as full temp lob
  OB_INLINE bool is_persist_lob() const
  {
    validate_has_lob_header(has_lob_header_);
    return has_lob_header_ && OB_NOT_NULL(ptr_) &&
           !is_lob_disk_locator() && size_ >= MEM_LOB_COMMON_HEADER_LEN &&
           (reinterpret_cast<ObMemLobCommon *>(ptr_))->is_persist();
  }

  OB_INLINE bool is_full_temp_lob() const
  {
    validate_has_lob_header(has_lob_header_);
    return (!has_lob_header_)
           || (has_lob_header_ && OB_NOT_NULL(ptr_) &&
              !is_lob_disk_locator() && size_ >= MEM_LOB_COMMON_HEADER_LEN &&
              (reinterpret_cast<ObMemLobCommon *>(ptr_))->is_temporary_full());
  }

  OB_INLINE bool is_delta_temp_lob() const
  {
    validate_has_lob_header(has_lob_header_);
    return has_lob_header_ && OB_NOT_NULL(ptr_) &&
           !is_lob_disk_locator() && size_ >= MEM_LOB_COMMON_HEADER_LEN &&
           (reinterpret_cast<ObMemLobCommon *>(ptr_))->is_temporary_delta();
  }

  OB_INLINE bool has_lob_header() const { return has_lob_header_; }

  char *ptr_; // if has_lob_header ptr_ is ObMemLobCommon, else it is data content
  uint32_t size_; // full MemLobLocator size;
  bool has_lob_header_; // for observer 4.0 compatibility
};

} // namespace common
} // namespace oceanbase
