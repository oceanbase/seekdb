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

#define USING_LOG_PREFIX COMMON
#include "share/rc/ob_server_runtime.h"

#include "lib/ob_check_macros.h"
#include "ob_lob_access_utils.h"
#include "share/ob_i_lob_read_service.h"
#include "share/lob/ob_lob_diff_struct.h"
#include "share/lob/ob_lob_text_iter_context.h"
#include "share/object/ob_obj_cast.h"
#include "common/datum/datum_value.h"

namespace oceanbase
{
namespace common
{

void ObLobTextIterCtx::init(bool is_clone /* false */)
{
  buff_byte_len_ = MAX(buff_byte_len_, OB_LOB_ITER_DEFAULT_BUFFER_LEN);
  if (is_clone) {
    // not used currently, waititng for temp lob based on temporary file
    is_cloned_temporary_ = is_clone;
  }
}


// ----- implementations of ObTextStringIter -----


int ObTextStringIter::init(uint32_t buffer_len,
                           const ObLobReadOptions *options,
                           ObIAllocator *res_allocator,
                           ObIAllocator *tmp_allocator)
{
  int ret = OB_SUCCESS;
  if (is_init_) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: iter already initiated", K(ret));
  } else if (!(is_lob_storage(type_))) {
    is_lob_ = false;
    is_outrow_ = false;
  } else if (datum_str_.length() == 0) {
  } else {
    ObLobLocatorV2 locator(datum_str_, has_lob_header_);
    is_lob_ = true;
    tmp_alloc_ = tmp_allocator;
    if (!has_lob_header_) {
      is_outrow_ = false;

    } else if (!locator.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(WARN,"Lob: invalid lob", K(ret));
    } else if (FALSE_IT(is_outrow_ = !locator.has_inrow_data())) {
    } else if (!is_outrow_ && !locator.is_delta_temp_lob()) { // inrow lob always get full data, no need ctx_
    } else if (OB_ISNULL(res_allocator)) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "Lob: iter with null allocator", K(ret));
    } else if (OB_ISNULL(options) || OB_ISNULL(options->read_service_)) {
      ret = OB_NOT_INIT;
      COMMON_LOG(WARN, "Lob: outrow iter requires an explicit read service", K(ret));
    } else { // outrow lob
      ObIAllocator *allocator = (tmp_allocator == nullptr) ? res_allocator : tmp_allocator;
      char *ctx_buffer = static_cast<char *>(allocator->alloc(sizeof(ObLobTextIterCtx)));
      if (OB_ISNULL(ctx_buffer)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        COMMON_LOG(WARN,"Lob: failed to alloc output buffer", K(ret), KP(ctx_buffer));
      } else {
        ctx_ = new (ctx_buffer) ObLobTextIterCtx(
            locator, *options->read_service_, options->timeout_ts_, res_allocator, buffer_len);
        ctx_ ->init();
        ctx_->access_context_ = options->access_context_;
      }
    }
  }
  if (OB_SUCC(ret)) {
    state_ = TEXTSTRING_ITER_INIT;
    is_init_ = true;
  }
  return ret;
}





int ObTextStringIter::get_full_data(ObString &data_str)
{
  int ret = OB_SUCCESS;
  ObLobLocatorV2 loc(datum_str_, has_lob_header_);
  if (!is_init_ || state_ != TEXTSTRING_ITER_INIT) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: iter state error", K(ret), K(is_init_), K(state_));
  } else if (datum_str_.length() == 0) { // maybe from datum->is_nop/is_null
    data_str.assign(NULL, 0);
  } else if (!is_lob_ || !has_lob_header_) { // string types or 4.0 compatiable text
    data_str.assign_ptr(datum_str_.ptr(), datum_str_.length());
  } else if (loc.is_delta_temp_lob()) {
    if (OB_FAIL(get_delta_lob_full_data(loc, tmp_alloc_, data_str))) {
    }
  } else if (!is_outrow_) { // inrow lob
    if (OB_FAIL(loc.get_inrow_data(data_str))) {
    }
  } else {
    // outrow lob, read full data into a inrow local tmp lob currently
    if (OB_FAIL(get_outrow_lob_full_data())) {
    } else {
      data_str.assign_ptr(ctx_->buff_, ctx_->content_byte_len_);
    }
  }
  if (OB_SUCC(ret)) {
    state_ = TEXTSTRING_ITER_NEXT;
  }
  COMMON_LOG(DEBUG, "Lob: get_full_data", K(ret), K(data_str), KP(data_str.ptr()), K(data_str.length()));
  return ret;
}

// Notice: always get full data for string tc & inrow lobs
int ObTextStringIter::get_inrow_or_outrow_prefix_data(ObString &data_str, uint32_t prefix_char_len)
{
  int ret = OB_SUCCESS;
  if (!is_init_ || state_ != TEXTSTRING_ITER_INIT) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: iter state error", K(ret), K(is_init_), K(state_));
  } else if (datum_str_.length() == 0) {
    data_str.assign(NULL, 0);
  } else if (!is_lob_ || !has_lob_header_) { // string types
    if (prefix_char_len == DEAFULT_LOB_PREFIX_CHAR_LEN) {
      data_str.assign_ptr(datum_str_.ptr(), datum_str_.length());
    } else {
      int64_t mb_len = ObCharset::strlen_char(cs_type_, datum_str_.ptr(), datum_str_.length());
      if (mb_len <= prefix_char_len) {
        data_str.assign_ptr(datum_str_.ptr(), datum_str_.length());
      } else {
        int64_t truncated_str_len = ObCharset::charpos(cs_type_, datum_str_.ptr(), datum_str_.length(), prefix_char_len);
        data_str.assign_ptr(datum_str_.ptr(), truncated_str_len);
      }
    }
  } else if (!is_outrow_) { // inrow lob
    ObLobLocatorV2 loc(datum_str_, has_lob_header_);
    if (OB_FAIL(loc.get_inrow_data(data_str))) {
    } else {
      uint32_t max_len = ObCharset::strlen_char(cs_type_, data_str.ptr(), data_str.length());
      uint32_t byte_len = (prefix_char_len > max_len) ? max_len : prefix_char_len;
      byte_len = ObCharset::charpos(cs_type_, data_str.ptr(), data_str.length(), byte_len);
      data_str.assign_ptr(data_str.ptr(), byte_len);
    }
  } else {
    // outrow lob, read full data into a inrow local tmp lob currently
    if (OB_FAIL(get_outrow_prefix_data(prefix_char_len))) {
    } else {
      data_str.assign_ptr(ctx_->buff_, ctx_->content_byte_len_);
    }
  }
  if (OB_SUCC(ret)) {
    state_ = TEXTSTRING_ITER_NEXT;
  }
  COMMON_LOG(DEBUG, "Lob: get_inrow_or_outrow_prefix_data",
    K(ret), K(data_str), KP(data_str.ptr()), K(data_str.length()));
  return ret;
}



int ObTextStringIter::reserve_data()
{
  int ret = OB_SUCCESS;
  if (!is_outrow_ || !has_lob_header_ || OB_ISNULL(ctx_) || OB_ISNULL(ctx_->read_cursor_)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: error condition", K(ret), K(is_outrow_), K(has_lob_header_), KP(ctx_));
  } else if (ctx_->reserved_len_ == 0 && ctx_->reserved_byte_len_ == 0) {
    // do nothing
  } else if (ctx_->accessed_byte_len_ == ctx_->total_byte_len_) {
    // 1. already the last block, should get iter end;
    // 2. if not the last block, this function fail if access_len_ < reserved_len_
  } else if (ctx_->reserved_len_ != 0) {
    // reserve_char_data()
    if (cs_type_ == CS_TYPE_BINARY) {
      ctx_->reserved_byte_len_ = ctx_->reserved_len_;
    } else if (ctx_->reserved_len_ > ctx_->content_len_) {
      ret = OB_SIZE_OVERFLOW;
      COMMON_LOG(WARN, "Lob: reserved length oversized", K(ret), K(*ctx_));
    } else if (!ctx_->is_backward_) {
      uint32 reserved_char_start = ctx_->content_len_ - ctx_->reserved_len_;
      uint32 reserved_char_pos =
        static_cast<uint32_t>(ObCharset::charpos(cs_type_, ctx_->buff_, ctx_->content_byte_len_, reserved_char_start));
      ctx_->reserved_byte_len_ = ctx_->content_byte_len_ - reserved_char_pos;
      MEMMOVE(ctx_->buff_, ctx_->buff_ + reserved_char_pos, ctx_->reserved_byte_len_);
    } else if (ctx_->is_backward_) {
      uint32 reserved_char_end = ctx_->reserved_len_ + 1;
      uint32 reserved_char_end_pos =
        static_cast<uint32_t>(ObCharset::charpos(cs_type_, ctx_->buff_, ctx_->content_byte_len_, reserved_char_end));
      ctx_->reserved_byte_len_ = reserved_char_end_pos;
      MEMMOVE(ctx_->buff_ + ctx_->buff_byte_len_ - ctx_->reserved_byte_len_,
              ctx_->buff_,
              ctx_->reserved_byte_len_);
    } else {}
  } else if (ctx_->reserved_byte_len_ != 0) {
    if (OB_FAIL(reserve_byte_data())) {
    }
  }
  return ret;
}

int ObTextStringIter::reserve_byte_data()
{
  int ret = OB_SUCCESS;
  if (ctx_->reserved_byte_len_ > ctx_->content_byte_len_) {
    ret = OB_SIZE_OVERFLOW;
    COMMON_LOG(WARN, "Lob: reserved byte length oversized", K(ret), K(*ctx_));
  } else if (!ctx_->is_backward_) {
    uint32 reserved_byte_start = ctx_->content_byte_len_ - ctx_->reserved_byte_len_;
    MEMMOVE(ctx_->buff_, ctx_->buff_ + reserved_byte_start, ctx_->reserved_byte_len_);
  } else if (ctx_->is_backward_) {
    MEMMOVE(ctx_->buff_ + ctx_->buff_byte_len_ - ctx_->reserved_byte_len_,
            ctx_->buff_,
            ctx_->reserved_byte_len_);
  } else {}
  return ret;
}


ObTextStringIterState ObTextStringIter::get_next_block(ObString &str)
{
  int ret = OB_SUCCESS;
  str.reset();
  if (!is_init_ || state_ < TEXTSTRING_ITER_INIT) {
    state_ = TEXTSTRING_ITER_INVALID;
    COMMON_LOG(WARN, "Lob: iter not initiated", K(ret), K(*this));
  } else if (!is_lob_ || !is_outrow_ || !has_lob_header_) { // if not outrow lob, get full data
    switch (state_) {
      case TEXTSTRING_ITER_INIT: {
        OZ(get_full_data(str));
        break;
      }
      case TEXTSTRING_ITER_NEXT: {
        state_ = TEXTSTRING_ITER_END;
        break;
      }
      default: {
        COMMON_LOG(WARN, "Lob: error state for common string or inrow lob", K(state_));
        break;
      }
    }
  } else if (is_outrow_) {
    switch (state_) {
      case TEXTSTRING_ITER_INIT: {
        OZ(get_first_block(str));
        break;
      }
      case TEXTSTRING_ITER_NEXT: {
        OZ(get_next_block_inner(str));
        break;
      }
      default: {
        COMMON_LOG(WARN, "Lob: error state for common string or inrow lob", K(state_));
        break;
      }
    }
  } else {
    COMMON_LOG(WARN, "Lob: error case in of iter", K(ret), K(*this));
    state_ = TEXTSTRING_ITER_INVALID;
  }
  if (OB_FAIL(ret)) {
    COMMON_LOG(WARN, "Lob: iter get_next_block failed", K(ret), K(*this));
    state_ = TEXTSTRING_ITER_INVALID;
    err_ret_ = ret;
  }
  return state_;
}

void ObTextStringIter::set_start_offset(uint64_t offset)
{
  if (is_valid_for_config()) {
    ctx_->start_offset_ = offset;
  }
}

void ObTextStringIter::set_access_len(int64_t char_len)
{
  if (is_valid_for_config()) {
    ctx_->total_access_len_ = char_len;
  }
}

void ObTextStringIter::set_reserved_len(uint32_t reserved_len)
{
  if (is_valid_for_config()) {
    ctx_->reserved_len_ = reserved_len;
    ctx_->reserved_byte_len_ = 0;
  }
}

void ObTextStringIter::set_reserved_byte_len(uint32_t reserved_byte_len)
{
  if (is_valid_for_config()) {
    ctx_->reserved_byte_len_ = reserved_byte_len;
    ctx_->reserved_len_ = 0;
  }
}

void ObTextStringIter::reset_reserve_len()
{
  if (is_valid_for_config(TEXTSTRING_ITER_NEXT)) {
    ctx_->reserved_byte_len_ = 0;
    ctx_->reserved_len_ = 0;
  }
}

void ObTextStringIter::set_backward()
{
  if (is_valid_for_config()) {
    ctx_->is_backward_ = true;
  }
}


uint64_t ObTextStringIter::get_start_offset()
{
  uint64_t start_offset = 0;
  if (!is_init_ || !is_outrow_ || !has_lob_header_
      || state_ <= TEXTSTRING_ITER_INIT || OB_ISNULL(ctx_)) {
  } else {
    start_offset = ctx_->start_offset_;
  }
  return start_offset;
}

uint32_t ObTextStringIter::get_last_accessed_len()
{
  uint32_t last_accessed_len = 0;
  if (!is_init_ || !is_outrow_ || !has_lob_header_
      || state_ <= TEXTSTRING_ITER_INIT || OB_ISNULL(ctx_)) {
  } else {
    last_accessed_len = ctx_->last_accessed_len_;
  }
  return last_accessed_len;
}

uint32_t ObTextStringIter::get_iter_count()
{
  uint32_t iter_count = 0;
  if (!is_init_ || !is_outrow_ || !has_lob_header_
      || state_ <= TEXTSTRING_ITER_INIT || OB_ISNULL(ctx_)) {
  } else {
    iter_count = ctx_->iter_count_;
  }
  return iter_count;
}

uint32_t ObTextStringIter::get_reserved_char_len()
{
  uint32_t reserved_char_len = 0;
  if (!is_init_ || !is_outrow_ || !has_lob_header_
      || state_ <= TEXTSTRING_ITER_INIT || OB_ISNULL(ctx_)) {
  } else {
    reserved_char_len = ctx_->reserved_len_;
  }
  return reserved_char_len;
}





uint32_t ObTextStringIter::get_accessed_byte_len()
{
  int32 ret = OB_SUCCESS;
  uint32_t accessed_byte_len = 0;
  if (!is_init_ || state_ <= TEXTSTRING_ITER_INIT) {
  } else if (!is_lob_ || !is_outrow_ || !has_lob_header_) { // string types
    int64_t total_byte_len = 0;
    if (OB_FAIL(get_byte_len(total_byte_len))) {
    } else {
      accessed_byte_len = static_cast<uint32_t>(total_byte_len);
    }
  } else if (is_outrow_) {
    accessed_byte_len = ctx_->accessed_byte_len_;
  } else { // should not come here.
    ret = OB_UNEXPECT_INTERNAL_ERROR;
    COMMON_LOG(WARN, "Lob: get access len failed", K(ret), K(*this), K(lbt()));
  }
  return accessed_byte_len;
}

int ObTextStringIter::get_byte_len(int64_t &byte_len)
{
  int ret = OB_SUCCESS;
  if (!is_init_) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: iter state error", K(ret), K(is_init_), K(state_));
  } else if (!is_lob_ || !is_outrow_ || !has_lob_header_) {
    ObString data_str = datum_str_;
    if (is_lob_ && has_lob_header_) {
      ObLobLocatorV2 loc(data_str, has_lob_header_);
      if (OB_FAIL(loc.get_inrow_data(data_str))) {
      }
    }
    byte_len = data_str.length();
  } else { // outrow lob
    if (OB_ISNULL(ctx_)) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "Lob: iter state error", K(ret), K(is_init_), K(state_), K(ctx_));
    } else if (!ctx_->locator_.is_persist_lob()) {
      ret = OB_NOT_IMPLEMENT;
      COMMON_LOG(WARN, "Lob: outrow temp lob is not implement", K(ret), K(ctx_->locator_));
    } else if (OB_FAIL(ctx_->locator_.get_lob_data_byte_len(byte_len))) {
    }
  }
  return ret;
}


int ObTextStringIter::convert_outrow_lob_to_inrow_templob(const DatumValue &input,
                                                          DatumValue &output,
                                                          const ObLobReadOptions *options,
                                                          ObIAllocator &owner,
                                                          bool allow_persist_inrow,
                                                          bool need_deep_copy)
{
  int ret = OB_SUCCESS;
  const SqlValueType type = input.get_type();
  // Match the existing caller contract: these inputs leave output untouched.
  if (!input.has_lob_header() || !is_lob_storage(type)
      || input.is_null() || input.is_nop_value()) {
  } else {
    ObLobLocatorV2 locator(input.get_string(), true);
    int64_t byte_length = 0;
    if (!locator.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else if ((!locator.is_persist_lob() || allow_persist_inrow)
               && (locator.is_inrow() || locator.is_simple())) {
      if (need_deep_copy) {
        // The locator is already validated. Copy its bytes without changing
        // the independent metadata (including unspecified raw collation).
        DatumValue candidate = input;
        if (OB_SUCC(ret = candidate.datum_.deep_copy(input.datum_, owner))) {
          output = candidate;
        }
      } else {
        output = input;
      }
    } else if (locator.is_delta_temp_lob()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(locator.get_lob_data_byte_len(byte_length))) {
    } else if (byte_length < 0 || byte_length > UINT32_MAX) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ObTextStringResult result(type, true, &owner);
      ObTextStringIter iterator(type, input.get_collation_type(), input.get_string(), true);
      if (OB_FAIL(result.init(byte_length))) {
      } else if (OB_FAIL(iterator.init(0, options, &owner))) {
      } else {
        int64_t copied = 0;
        ObString block;
        while (OB_SUCC(ret) && copied < byte_length) {
          const ObTextStringIterState state = iterator.get_next_block(block);
          if (state != TEXTSTRING_ITER_NEXT) {
            ret = iterator.get_inner_ret();
            if (ret == OB_SUCCESS) { ret = OB_ERR_UNEXPECTED; }
          } else if (block.empty() || block.length() > byte_length - copied) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(result.append(block))) {
          } else {
            copied += block.length();
          }
        }
        if (OB_SUCC(ret)) {
          ObString bytes;
          result.get_result_buffer(bytes);
          DatumValue candidate = input;
          candidate.datum_.set_string(bytes);
          candidate.meta_.set_type(type);
          candidate.meta_.set_inrow();
          if (type != ObCollectionSQLType) {
            candidate.meta_.set_collation_level(CS_LEVEL_IMPLICIT);
          }
          candidate.meta_.set_has_lob_header();
          output = candidate;
        }
      }
    }
  }
  return ret;
}

// ----- implementations of ObTextStringResult -----

int ObTextStringResult::calc_buffer_len(int64_t res_len)
{
  int ret = OB_SUCCESS;
  if (!(is_lob_storage(type_))) { // tinytext no has lob header
    buff_len_ = res_len;
  } else {
    if (!has_lob_header_) {
      buff_len_ = res_len;
    } else if (res_len < OB_MAX_LONGTEXT_LENGTH - MAX_TMP_LOB_HEADER_LEN) {
      // inrow lob with lob header
      bool has_extern = false; // temp data does not need extern flags
      ObMemLobExternFlags extern_flags(has_extern);
      res_len += sizeof(ObLobCommon);
      if (has_extern) {
        buff_len_ = ObLobLocatorV2::calc_locator_full_len(extern_flags, 0, static_cast<uint32_t>(res_len), 0, false);
      } else {
        buff_len_ = res_len; // temp lob can be mocked as disk inrow lob
      }
    } else {
      ret = OB_NOT_SUPPORTED;
    }
  }
  return ret;
}




int ObTextStringResult::fill_temp_lob_header(const int64_t res_len)
{
  int ret = OB_SUCCESS;
  if (!has_lob_header_) { // do nothing
  } else if (OB_ISNULL(buffer_)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (!(is_lob_storage(type_))) { // do nothing
  } else if (res_len <= OB_MAX_LONGTEXT_LENGTH - MAX_TMP_LOB_HEADER_LEN) {
    ObLobCommon lob_common;
    // temp lob can be mocked as disk inrow lob
    MEMCPY(buffer_, &lob_common, sizeof(ObLobCommon));
    pos_ = buff_len_ - res_len; // only res_len could be used later
  } else { // outrow
    ret = OB_NOT_IMPLEMENT;
  }
  return ret;
}

int ObTextStringResult::init(int64_t res_len, ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(buffer_) || is_init_) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!(ob_is_string_or_lob_type(type_) || is_lob_storage(type_))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(calc_buffer_len(res_len))) {
  } else if (buff_len_ == 0) {
    OB_ASSERT(has_lob_header_ == false); // empty result without header
  } else {
    buffer_ = OB_ISNULL(allocator)
              ? (char *)alloc_->alloc(buff_len_) : (char *)allocator->alloc(buff_len_);
    if (OB_ISNULL(buffer_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(fill_temp_lob_header(res_len))) {
    }
  }
  if (OB_SUCC(ret)) {
    is_init_ = true;
  }
  return ret;
}

int ObTextStringResult::init(const int64_t res_len, ObString &res_buffer)
{
  int ret = OB_SUCCESS;
  if (is_init_) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!(ob_is_string_or_lob_type(type_) || is_lob_storage(type_))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(calc_buffer_len(res_len))) {
  } else if (buff_len_ != res_buffer.length()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    buffer_ = res_buffer.ptr();    
    if (OB_FAIL(fill_temp_lob_header(res_len))) {
    } else {
      is_init_ = true;
    }
  }
  
  return ret;
}

int ObTextStringResult::copy(const ObLobLocatorV2 *loc)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(loc) || !loc->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (loc->size_ == 0) {
  } else {
    buff_len_ = loc->size_;
    buffer_ = (char *)alloc_->alloc(buff_len_);
    if (OB_ISNULL(buffer_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(buffer_, loc->ptr_, buff_len_);
      has_lob_header_ = loc->has_lob_header_;
	  }
  }
  return ret;
}

int ObTextStringResult::append(const char *buffer, int64_t len)
{
  int ret = OB_SUCCESS;
  if (!is_outrow_templob_) {
    if (len == 0) {
    } else if (pos_ + len > buff_len_) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      MEMCPY(buffer_ + pos_, buffer, len);
      pos_ += len;
    }
  } else {
    ret = OB_NOT_IMPLEMENT;
  }
  return ret;
}

int ObTextStringResult::fill(int64_t pos, int c, int64_t len)
{
  int ret = OB_SUCCESS;
  if (!is_outrow_templob_) {
    if (len == 0) {
    } else if (pos + len > buff_len_) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      MEMSET(buffer_ + pos_ + pos, c, len);
    }
  } else {
    ret = OB_NOT_IMPLEMENT;
  }
  return ret;
}


// ToDo: add state for curr, or beginning, or end, adjust payload size?
int ObTextStringResult::lseek(int64_t offset, int state)
{
  int ret = OB_SUCCESS;
  if (pos_ + offset < 0 || pos_ + offset > buff_len_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    pos_ += offset;
  }
  return ret;
}

// Notice: should move pos_ to new pos after write buffer (lseek) !
int ObTextStringResult::get_reserved_buffer(char *&empty_start, int64_t &empty_len)
{
  int ret = OB_SUCCESS;
  if (!is_outrow_templob_) {
    empty_start = buffer_ + pos_;
    empty_len = buff_len_ - pos_;
    if (empty_len < 0) {
      ret = OB_SIZE_OVERFLOW;
    }
  } else {
    ret = OB_NOT_IMPLEMENT;
  }
  return ret;
}

int ObTextStringResult::ob_convert_datum_temporay_lob(ObDatum &datum,
                                                      const SqlTypeMeta &in_obj_meta,
                                                      const SqlTypeMeta &out_obj_meta,
                                                      ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  SqlValueType type = in_obj_meta.get_type();
  if (!is_lob_storage(type)) {
  } else if (in_obj_meta.has_lob_header()
             || !out_obj_meta.has_lob_header()
             || (in_obj_meta.get_type_class() != out_obj_meta.get_type_class())) {
    ret = OB_UNEXPECT_INTERNAL_ERROR;
    COMMON_LOG(WARN, "Lob: not support to convert obj has lob header to tempory lob",
               K(ret), K(datum), K(in_obj_meta), K(out_obj_meta));
  } else if (datum.is_null() || datum.is_nop()) {
  } else {
    ObString row_str = datum.get_string();
    ObTextStringResult new_tmp_lob(out_obj_meta.get_type(), true, &allocator);
    if (OB_FAIL(new_tmp_lob.init(row_str.length()))) {
    } else if (OB_FAIL(new_tmp_lob.append(row_str))) {
    } else {
      ObString result;
      new_tmp_lob.get_result_buffer(result);
      datum.set_string(result);
    }
  }
  return ret;
}

int64_t ObDeltaLob::get_serialize_size() const
{
  int64_t size = 0;
  // header size
  size += get_header_serialize_size();
  // updated lob size
  size += get_partial_data_serialize_size();
  // lob diff size
  size += get_lob_diff_serialize_size();
  return size;
}


int ObDeltaLob::serialize(char* buf, const int64_t buf_len, int64_t& pos) const
{
  int ret = OB_SUCCESS;
  ObLobDiffHeader *diff_header = nullptr;
  if (OB_FAIL(serialize_header(buf, buf_len, pos, diff_header))) {
  } else if (OB_FAIL(serialize_partial_data(buf, buf_len, pos))) {
  } else if (OB_FAIL(serialize_lob_diffs(buf, buf_len, diff_header))) {
  }
  return ret;
}

int ObDeltaLob::serialize_header(char* buf, const int64_t buf_len, int64_t& pos, ObLobDiffHeader *&diff_header) const
{
  int ret = OB_SUCCESS;
  int64_t size = get_header_serialize_size();
  if (pos + size > buf_len) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    ObMemLobCommon *mem_common = new (buf + pos) ObMemLobCommon(ObMemLobType::TEMP_DELTA_LOB, false);
    ObLobCommon *lob_common = new (mem_common->data_) ObLobCommon();
    diff_header = new (lob_common->buffer_) ObLobDiffHeader();
    diff_header->diff_cnt_ = get_lob_diff_cnt();
    diff_header->persist_loc_size_ = static_cast<uint32_t>(get_partial_data_serialize_size());
    pos += size;
  }
  return ret; 
}

int ObDeltaLob::deserialize(const ObLobLocatorV2 &lob_locator)
{
  int ret = OB_SUCCESS;
  ObLobCommon *lob_common = nullptr;
  ObLobDiffHeader *diff_header = nullptr;
  if (! lob_locator.is_delta_temp_lob()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(lob_locator.get_disk_locator(lob_common))) {
  } else if (OB_ISNULL(diff_header = reinterpret_cast<ObLobDiffHeader*>(lob_common->buffer_))){
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(deserialize_partial_data(diff_header))) {
  } else if (OB_FAIL(deserialize_lob_diffs(lob_locator.ptr_, lob_locator.size_, diff_header))) {
  }
  return ret;
}

int ObDeltaLob::has_diff(const ObLobLocatorV2 &locator, int64_t &res)
{
  int ret = OB_SUCCESS;
  bool bres = false;
  if (OB_FAIL(has_diff(locator, bres))) {
  } else {
    res = bres;
  }
  return ret;
}

int ObDeltaLob::has_diff(const ObLobLocatorV2 &locator, bool &res)
{
  int ret = OB_SUCCESS;
  ObLobCommon *lob_common = nullptr;
  if (! locator.is_delta_temp_lob()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(locator.get_disk_locator(lob_common))) {
  } else if (! lob_common->in_row_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObLobDiffHeader *diff_header = reinterpret_cast<ObLobDiffHeader*>(lob_common->buffer_);
    res = diff_header->diff_cnt_ > 0;
  }
  return ret;
}

// Out-of-row reads go through the injected service port; Storage remains the
// implementation owner and Share does not include its manager.

void ObLobTextIterCtx::reuse()
{
  content_byte_len_ = 0;
  content_len_ = 0;
  accessed_byte_len_ = 0;
  accessed_len_ = 0;
  last_accessed_byte_len_ = 0;
  last_accessed_len_ = 0;
  iter_count_ = 0;
  if (OB_NOT_NULL(read_cursor_)) {
    read_service_.free_lob_query_iter(*this);
  }
}

ObTextStringIter::~ObTextStringIter()
{
  if (OB_NOT_NULL(ctx_) && OB_NOT_NULL(ctx_->read_cursor_)) {
    ctx_->read_service_.free_lob_query_iter(*ctx_);
  }
}

int ObTextStringIter::get_outrow_lob_full_data(ObIAllocator *allocator /*nullptr*/)
{
  UNUSED(allocator);
  int ret = OB_SUCCESS;
  if (!has_lob_header_ || !is_outrow_ || OB_ISNULL(ctx_) || OB_ISNULL(ctx_->alloc_)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: error condition", K(ret), K(has_lob_header_), K(is_outrow_), KP(ctx_));
  } else if (OB_FAIL(ctx_->read_service_.get_outrow_lob_full_data(
                 *ctx_, cs_type_, has_lob_header_, is_outrow_, tmp_alloc_))) {
  }
  return ret;
}

int ObTextStringIter::get_delta_lob_full_data(ObLobLocatorV2& lob_locator, ObIAllocator *allocator, ObString &data_str)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx_)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: error condition", K(ret), KP(ctx_));
  } else if (OB_FAIL(ctx_->read_service_.get_delta_lob_full_data(
                 *ctx_, type_, cs_type_, lob_locator, allocator, data_str))) {
  }
  return ret;
}

int ObTextStringIter::get_outrow_prefix_data(uint32_t prefix_char_len)
{
  int ret = OB_SUCCESS;
  if (!has_lob_header_ || !is_outrow_ || OB_ISNULL(ctx_) || OB_ISNULL(ctx_->alloc_)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: error condition", K(ret), K(has_lob_header_), K(is_outrow_), KP(ctx_));
  } else if (OB_FAIL(ctx_->read_service_.get_outrow_prefix_data(
                 *ctx_, cs_type_, has_lob_header_, is_outrow_, tmp_alloc_, prefix_char_len))) {
  }
  return ret;
}

int ObTextStringIter::get_first_block(ObString &str)
{
  int ret = OB_SUCCESS;
  if (!is_outrow_ || OB_ISNULL(ctx_) || !has_lob_header_) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: error condition", K(ret), K(is_outrow_), KP(ctx_), K(has_lob_header_));
  } else if (OB_FAIL(ctx_->read_service_.get_first_block(
                 *ctx_, cs_type_, has_lob_header_, is_outrow_, tmp_alloc_, str, state_))) {
  }
  return ret;
}

int ObTextStringIter::get_next_block_inner(ObString &str)
{
  // 1. calc reserved len and memmove(done locally on the share side)
  // 2. fetch the next chunk through the port and update ctx
  int ret = OB_SUCCESS;
  if (!is_outrow_ || !has_lob_header_ || OB_ISNULL(ctx_) || OB_ISNULL(ctx_->read_cursor_)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: error condition", K(ret), K(is_outrow_), K(has_lob_header_), KP(ctx_));
  } else if (ctx_->content_byte_len_ == 0) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: no data", K(ret), K(is_outrow_), KP(ctx_));
  } else if (OB_FAIL(reserve_data())) {
  } else if (OB_FAIL(ctx_->read_service_.get_next_block_inner(
                 *ctx_, cs_type_, has_lob_header_, is_outrow_, str, state_))) {
  }
  return ret;
}

int ObTextStringIter::get_char_len(int64_t &char_length)
{
  int ret = OB_SUCCESS;
  if (!is_init_) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: iter state error", K(ret), K(is_init_), K(state_));
  } else if (!is_lob_ || !is_outrow_ || !has_lob_header_) {
    ObString data_str = datum_str_;
    if (is_lob_ && has_lob_header_) {
      ObLobLocatorV2 loc(data_str, has_lob_header_);
      if (OB_FAIL(loc.get_inrow_data(data_str))) {
      }
    }
    char_length = ObCharset::strlen_char(cs_type_, data_str.ptr(), static_cast<int64_t>(data_str.length()));
  } else { // outrow lob
    if (OB_ISNULL(ctx_)) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "Lob: error condition", K(ret), K(is_outrow_), KP(ctx_));
    } else if (OB_FAIL(ctx_->read_service_.get_outrow_char_len(
                   *ctx_, cs_type_, tmp_alloc_, char_length))) {
    }
  }
  return ret;
}

int64_t ObDeltaLob::get_header_serialize_size() const
{
  int64_t size = 0;
  // ObMemLobCommon
  size += sizeof(ObMemLobCommon);
  // ObLobCommon;
  size += sizeof(ObLobCommon);
  // ObLobDiffHeader
  size += sizeof(ObLobDiffHeader);
  return size;
}

}
}

namespace oceanbase
{
namespace common
{
namespace lob_helper
{
int read_real_string_data(
    ObIAllocator *allocator,
    SqlValueType type,
    ObCollationType cs_type,
    bool has_lob_header,
    ObString &str,
    const ObLobReadOptions *options)
{
  int ret = OB_SUCCESS;
  if (is_lob_storage(type)) {
    ObArenaAllocator *tmp_alloc_ptr = nullptr;
    ObArenaAllocator tmp_allocator("ObLobRRSD", OB_MALLOC_NORMAL_BLOCK_SIZE);
    {
      tmp_alloc_ptr = &tmp_allocator;
    }
    ObTextStringIter str_iter(type, cs_type, str, has_lob_header);
    if (OB_FAIL(str_iter.init(0/*buffer_len*/, options, allocator, tmp_alloc_ptr))) {
    } else if (OB_FAIL(str_iter.get_full_data(str))) {
    }
  }
  return ret;
}

}  // namespace lob_helper
}  // namespace common
namespace common
{
int ObTextStringResult::init_cast_buffer(int64_t res_len,
    DatumCastParams *params, ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (is_init_) {
  } else if (OB_ISNULL(allocator) && OB_ISNULL(params)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObTextStringResult::calc_buffer_len(res_len))) {
  } else if (buff_len_ == 0) {
    OB_ASSERT(has_lob_header_ == false); // empty result without header
  } else {
    buffer_ = OB_ISNULL(allocator)
              ? static_cast<char*>(params->alloc(buff_len_)) : static_cast<char*>(allocator->alloc(buff_len_));
    if (OB_ISNULL(buffer_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(fill_temp_lob_header(res_len))) {
    }
    if (OB_SUCC(ret)) {
      is_init_ = true;
    }
  }
  return ret;
}

int ObTextStringDatumValueResult::init(int64_t res_len, ObIAllocator *allocator)
{
  if (is_init_) { return OB_SUCCESS; }
  if (result_ == nullptr) { return OB_INVALID_ARGUMENT; }
  return init_cast_buffer(res_len, params_, allocator);
}

void ObTextStringDatumValueResult::set_result()
{
  result_->meta_.set_type(type_);
  result_->meta_.set_inrow();
  if (type_ != ObCollectionSQLType) {
    result_->meta_.set_collation_level(CS_LEVEL_IMPLICIT);
  }
  result_->datum_.set_string(buffer_, static_cast<uint32_t>(pos_));
  if (is_lob_storage(type_) && has_lob_header_) {
    result_->meta_.set_has_lob_header();
  }
}
}  // namespace common
}  // namespace oceanbase
