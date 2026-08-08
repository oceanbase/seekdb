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

#ifndef OCEANBASE_SHARE_OB_LOB_ACCESS_UTILS_
#define OCEANBASE_SHARE_OB_LOB_ACCESS_UTILS_

#include "share/ob_errno.h"
#include "common/object/ob_object.h"
#include "common/object/ob_obj_type.h"
#include "common/datum/ob_datum.h"

namespace oceanbase
{
namespace common
{
class ObILobAccessContext;
class ObILobReadService;
struct ObLobDiffHeader;
struct ObLobTextIterCtx;

// Share-owned read request. Query translates its session into a deadline and
// may attach an opaque data-plane cache context; neither upper-layer type
// crosses this Interface.
struct ObLobReadOptions
{
  ObLobReadOptions(ObILobReadService &read_service,
                   int64_t timeout_ts = 0,
                   ObILobAccessContext *access_context = nullptr)
    : read_service_(&read_service), timeout_ts_(timeout_ts), access_context_(access_context)
  {}
  ObILobReadService *read_service_;
  int64_t timeout_ts_;
  ObILobAccessContext *access_context_;
};

// Notice: cannot support obobj funcs/compare (in lib dir)
// fixed underlying type (: int): so the ObILobReadService port header can forward-declare this enum(dependency inversion)。
enum ObTextStringIterState : int
{
  TEXTSTRING_ITER_INVALID = 0,
  TEXTSTRING_ITER_INIT = 1,
  TEXTSTRING_ITER_NEXT = 2,
  TEXTSTRING_ITER_END = 3
};

// wrapper class to handle string/text type input
class ObTextStringIter
{
public:
  static const uint32_t DEAFULT_LOB_PREFIX_CHAR_LEN = 1000;
  static const uint32_t MAX_CHAR_MULTIPLIER = 4;
  ObTextStringIter(ObObjType type, ObCollationType cs_type, const ObString &datum_str, 
                   bool has_lob_header) :
    type_(type), cs_type_(cs_type), is_init_(false), is_lob_(false), is_outrow_(false),
    has_lob_header_(has_lob_header), state_(TEXTSTRING_ITER_INVALID), datum_str_(datum_str),
    ctx_(nullptr), err_ret_(OB_SUCCESS), tmp_alloc_(nullptr)
  {
    if (is_lob_storage(type)) {
      validate_has_lob_header(has_lob_header_);
    }
    cs_type_ = ob_is_json(type) ? CS_TYPE_BINARY : cs_type_;
  }

  ObTextStringIter(const ObObj &obj) :
    type_(obj.get_type()), cs_type_(obj.get_collation_type()), is_init_(false), is_lob_(false),
    is_outrow_(false), has_lob_header_(obj.has_lob_header()), state_(TEXTSTRING_ITER_INVALID),
    datum_str_(obj.get_string()), ctx_(nullptr), err_ret_(OB_SUCCESS), tmp_alloc_(nullptr)
  {
    if (is_lob_storage(obj.get_type())) {
      validate_has_lob_header(has_lob_header_);
    }
    cs_type_ = ob_is_json(obj.get_type()) ? CS_TYPE_BINARY : cs_type_;
  }
  ~ObTextStringIter();

  TO_STRING_KV(K_(type), K_(cs_type), K_(is_init), K_(is_lob), K_(is_outrow),
    K_(state), K(datum_str_), KP_(ctx), K_(err_ret));

  int init(uint32_t buffer_len,
           const ObLobReadOptions *options = NULL,
           ObIAllocator *res_allocator = NULL,
           ObIAllocator *tmp_allocator = NULL);

  ObTextStringIterState get_next_block(ObString &str);


  int get_full_data(ObString &data_str);

  int get_inrow_or_outrow_prefix_data(ObString &data_str,
                                      uint32_t prefix_char_len = DEAFULT_LOB_PREFIX_CHAR_LEN);

  void set_start_offset(uint64_t offset);

  void set_access_len(int64_t char_len); // total read len of outrow lob
  void set_reserved_len(uint32_t reserved_len);
  void set_reserved_byte_len(uint32_t reserved_byte_len);
  void reset_reserve_len();
  void set_backward();

  uint64_t get_start_offset();
  uint32_t get_last_accessed_len();
  uint32_t get_accessed_byte_len();
  int get_inner_ret() { return err_ret_; }
  bool is_outrow_lob() { return is_outrow_; };
  int get_byte_len(int64_t &byte_len);
  int get_char_len(int64_t &char_length);
  uint32_t get_iter_count();
  uint32_t get_reserved_char_len();
  static int convert_outrow_lob_to_inrow_templob(const ObObj &in_obj,
                                                 ObObj &out_obj,
                                                 const ObLobReadOptions *options,
                                                 ObIAllocator *allocator,
                                                 bool allow_persist_inrow = false,
                                                 bool need_deep_copy = false);

private:
  int get_outrow_lob_full_data(ObIAllocator *allocator = nullptr);
  int get_delta_lob_full_data(ObLobLocatorV2& lob_locator, ObIAllocator *allocator, ObString &data);
  int get_first_block(ObString &str);
  int get_next_block_inner(ObString &str);
  int get_outrow_prefix_data(uint32_t prefix_char_len);
  int reserve_data();
  int reserve_byte_data();
  OB_INLINE bool is_valid_for_config(ObTextStringIterState valid_state = TEXTSTRING_ITER_INIT)
  {
    return (is_init_ && is_outrow_ && has_lob_header_
            && state_ == valid_state && OB_NOT_NULL(ctx_));
  }
private:
  ObObjType type_;
  ObCollationType cs_type_;
  uint32_t is_init_ : 1;
  uint32_t is_lob_ : 1;
  uint32_t is_outrow_ : 1;
  uint32_t has_lob_header_ : 1;// 4.0 lob compatibility
  uint32_t reserved : 28;
  ObTextStringIterState state_;
  const ObString datum_str_;
  ObLobTextIterCtx *ctx_;
  int err_ret_;
  ObIAllocator *tmp_alloc_;
};

// wrapper class to handle templob output(including string types)
class ObTextStringResult
{
public:
  ObTextStringResult(const ObObjType type,  bool has_lob_header, ObIAllocator *allocator) :
    type_(type), buffer_(NULL), buff_len_(0), pos_(0), is_outrow_templob_(false),
    has_lob_header_(has_lob_header), is_init_(false), alloc_(allocator)
  {
    if (is_lob_storage(type)) {
      validate_has_lob_header(has_lob_header_);
    } 
  }
  ~ObTextStringResult(){};

  TO_STRING_KV(K_(type), KP_(buffer), K_(buff_len), K_(pos), K_(is_outrow_templob), 
               K_(has_lob_header), K_(is_init), KP_(alloc));

  static const uint32_t MAX_TMP_LOB_HEADER_LEN = 1 * 1024;

  // create resource by expr.datum_.type_ and has_lob_header_
  // inrow lobs: create medmory buffer with expr.get_str_res_mem, or allocator in cast params;
  // outrow lobs: create memory for locator, and tmp file for outrow data (not implemented)
  // support user assigned allocator
  // Notice: 
  // 1. all lobs created by this class should be temp lobs
  // 2. if has_lob_header_ is false, the text result should be 4.0 compatible
  virtual int init(const int64_t res_len, ObIAllocator *allocator = NULL);
  int init(const int64_t res_len, ObString &res_buffer);

  // copy existent loc to result
  int copy(const ObLobLocatorV2 *loc);

  // append (copy) result to buffer(file), change pos_
  int append(const char *buffer, int64_t len);
  OB_INLINE int append(const ObString &str)
  {
    return append(str.ptr(), str.length());
  }

  // overwrite exist result buffer(file), not change pos_

  // overwrite exist result buffer(file), not change pos_
  int fill(int64_t pos, int c, int64_t len);

  // move pos_ to pos_ + offset
  int lseek(int64_t offset, int state);

  // expose buffer for user function, lseek should be called after write
  int get_reserved_buffer(char *&empty_start, int64_t &empty_len);

  bool is_init() { return is_init_; };

  OB_INLINE void get_result_buffer(ObString &buf_str) { buf_str.assign(buffer_, pos_); }
  OB_INLINE void set_has_lob_header(bool has_header) { has_lob_header_ = has_header; }
  OB_INLINE bool has_lob_header() { return (is_lob_storage(type_)) && has_lob_header_; }
  static int ob_convert_obj_temporay_lob(ObObj &obj, ObIAllocator &allocator);
  static int ob_convert_datum_temporay_lob(ObDatum &datum,
                                           const ObObjMeta &in_obj_meta,
                                           const ObObjMeta &out_obj_meta,
                                           ObIAllocator &allocator);
  int calc_buffer_len(const int64_t res_len);
  OB_INLINE int64_t get_buff_len() { return buff_len_; }

protected:
  int fill_temp_lob_header(const int64_t res_len);

protected:
  const ObObjType type_;
  char *buffer_;
  int64_t buff_len_;
  int64_t pos_;
  bool is_outrow_templob_;
  bool has_lob_header_;
  bool is_init_;
  ObIAllocator *alloc_;
};

OB_INLINE bool ob_is_empty_lob(ObObjType type, const ObDatum &datum, bool has_lob_header)
{
  bool bret = false;
  if (common::is_lob_storage(type)) {
    common::ObLobLocatorV2 loc(datum.get_string(), has_lob_header);
    bret = loc.is_empty_lob();
  }
  return bret;
}

template <typename TextVec>
OB_INLINE bool ob_is_empty_lob(ObObjType type, const TextVec &vector, bool has_lob_header,
                               int64_t idx)
{
  bool bret = false;
  if (common::is_lob_storage(type)) {
    common::ObLobLocatorV2 loc(vector->get_string(idx), has_lob_header);
    bret = loc.is_empty_lob();
  }
  return bret;
}

OB_INLINE bool ob_is_empty_lob(const ObObj &obj)
{
  bool bret = false;
  if (common::is_lob_storage(obj.get_type())) {
    common::ObLobLocatorV2 loc(obj.get_string(), obj.has_lob_header());
    bret = loc.is_empty_lob();
  }
  return bret;
}

class ObDeltaLob {
public:
   static int has_diff(const ObLobLocatorV2 &locator, int64_t &res);
   static int has_diff(const ObLobLocatorV2 &locator, bool &res);

public:
  int64_t get_serialize_size() const;
  int64_t get_header_serialize_size() const;
  virtual int64_t get_partial_data_serialize_size() const = 0;
  virtual int64_t get_lob_diff_serialize_size() const = 0;
  virtual uint32_t get_lob_diff_cnt() const = 0;

  int serialize(char* buf, const int64_t buf_len, int64_t& pos) const;
  int serialize_header(char* buf, const int64_t buf_len, int64_t& pos, ObLobDiffHeader *&diff_header) const;
  virtual int serialize_partial_data(char* buf, const int64_t buf_len, int64_t& pos) const = 0;
  virtual int serialize_lob_diffs(char* buf, const int64_t buf_len, ObLobDiffHeader *diff_header) const = 0;

  int deserialize(const ObLobLocatorV2 &delta_lob);
  virtual int deserialize_partial_data(ObLobDiffHeader *diff_header) = 0;
  virtual int deserialize_lob_diffs(char* buf, const int64_t buf_len, ObLobDiffHeader *diff_header) = 0;
};

} // end namespace common
} // end namespace oceanbase

namespace oceanbase
{
namespace common
{
struct ObObjCastParams;
namespace lob_helper
{
int read_real_string_data(ObIAllocator *allocator, const ObObj &obj, ObString &str,
                          const ObLobReadOptions *options);
int read_real_string_data(ObIAllocator *allocator, ObObjType type, ObCollationType cs_type,
                          bool has_lob_header, ObString &str,
                          const ObLobReadOptions *options);
template <typename Allocator>
int pack_to_disk_inrow_lob(Allocator &allocator, const ObString data, ObString &result)
{
  int ret = OB_SUCCESS;
  int64_t total_len = data.length() + sizeof(ObLobCommon);
  char* buf = nullptr;
  if (OB_ISNULL(buf = (char*)allocator.alloc(total_len))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    COMMON_LOG(WARN, "alloc memory for lob fail", K(ret), K(total_len));
  } else {
    // default is inrow
    ObLobCommon *lob_data = new(buf)ObLobCommon();
    MEMCPY(lob_data->buffer_, data.ptr(), data.length());
    result.assign_ptr(buf, total_len);
  }
  return ret;
}

template <typename Allocator>
int pack_to_disk_inrow_lob(Allocator &allocator, const ObString data, const ObObjType type, ObObj &res_obj)
{
  int ret = OB_SUCCESS;
  ObString result;
  if (OB_FAIL(pack_to_disk_inrow_lob(allocator, data, result))) {
  } else {
    res_obj.set_lob_value(type, result.ptr(), result.length());
    res_obj.set_has_lob_header();
  }
  return ret;
}

}  // namespace lob_helper
}  // namespace common

// ObObj-level text/lob result writer owned by the Share runtime.
namespace common
{
class ObTextStringObObjResult : public ObTextStringResult
{
public:
  ObTextStringObObjResult(const ObObjType type, ObObjCastParams *params, ObObj *res_obj, bool has_header) :
    ObTextStringResult(type, has_header, NULL), params_(params), res_obj_(res_obj)
  {}

  ~ObTextStringObObjResult(){};

  TO_STRING_KV(KP_(params), KP_(res_obj));
  int init(int64_t res_len, ObIAllocator *allocator = NULL) override;
  void set_result();

private:
  char * buff_alloc (const int64_t size);

private:
  ObObjCastParams *params_;
  ObObj *res_obj_;
};
}  // namespace common
}  // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_LOB_ACCESS_UTILS_
