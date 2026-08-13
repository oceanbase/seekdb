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

#include "data_plane/lob/ob_json_lob.h"
#include "data_plane/lob/ob_lob_read.h"
#include "data_plane/lob/ob_lob_value.h"
#include "ob_lob_manager.h"
#include "common/json_type/ob_json_diff.h"
#include "share/rc/ob_server_runtime.h"
#include "share/ob_server_struct.h"
#include "storage/lob/ob_lob_handler.h"
#include "storage/lob/ob_lob_locator_struct.h"
#include "storage/lob/ob_lob_persistent_reader.h"
#include "storage/lob/ob_lob_tablet_dml.h"
#include "share/ob_lob_access_utils.h"
#include "share/lob/ob_lob_text_iter_context.h"
#include "query/engine/expr/ob_expr_util.h"

namespace oceanbase
{
namespace storage
{
using common::ObLobDiff;
using common::ObLobDiffHeader;

static int check_write_length(ObLobAccessParam& param, int64_t expected_len)
{
  int ret = OB_SUCCESS;
  if (ObLobDataOutRowCtx::OpType::SQL != param.op_type_) {
    // skip not full write
  } else if (param.byte_size_ != expected_len) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("size not match", K(ret), K(expected_len), K(param.byte_size_));
  }
  return ret;
}

const ObLobCommon ObLobManager::ZERO_LOB = ObLobCommon();

// for only one lob meta in mysql mode, we can have no char len here
static int is_store_char_len(ObLobAccessParam& param, int64_t store_chunk_size, int64_t add_len)
{
  int ret = OB_SUCCESS;
  if (! param.is_char()) {
  } else if (store_chunk_size <= (param.byte_size_ + add_len)) {
  } else if (param.tablet_id_.is_inner_tablet()) {
  } else {
    param.is_store_char_len_ = false;
  }
  return ret;
}

int ObLobManager::server_module_new(ObLobManager *&m) {
  int ret = OB_SUCCESS;
  
  auto attr = lib::ObMemAttr("LobManager");
  m = OB_NEW(ObLobManager, attr);
  if (OB_ISNULL(m)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc memory", K(ret));
  }
  return ret;
}



int ObLobManager::init()
{
  int ret = OB_SUCCESS;
  
  lib::ObMemAttr mem_attr("LobAllocator", ObCtxIds::LOB_CTX_ID);
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObLobManager init twice.", K(ret));
  } else if (OB_FAIL(allocator_.init(common::ObMallocAllocator::get_instance(), OB_MALLOC_MIDDLE_BLOCK_SIZE, mem_attr))) {
  } else if (OB_FAIL(ext_info_log_allocator_.init(
      common::ObMallocAllocator::get_instance(), 
      OB_MALLOC_NORMAL_BLOCK_SIZE,
      lib::ObMemAttr("ExtInfoLog", ObCtxIds::LOB_CTX_ID)))) {
  } else {
    OB_ASSERT(sizeof(ObLobCommon) == sizeof(uint32));
    lob_ctx_.lob_meta_mngr_ = &meta_manager_;
    lob_ctx_.lob_piece_mngr_ = &piece_manager_;
    is_inited_ = true;
  }
  if (OB_UNLIKELY(!is_inited_)) {
    destroy();
  }
  return ret;
}

int ObLobManager::start()
{
  int ret = OB_SUCCESS;
  // TODO
  return ret;
}

int ObLobManager::stop()
{
  STORAGE_LOG(INFO, "[LOB]stop");
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLobManager is not initialized", K(ret));
  } else {
    // TODO
    // 1. Trigger asynchronous flush of memory data in LobOperator
    // 2. Clean up temporary LOB
  }
  return ret;
}

void ObLobManager::wait()
{
  STORAGE_LOG(INFO, "[LOB]wait");
  // TODO
  // 1. Wait for the asynchronous flush of memory data in LobOperator to complete
}

void ObLobManager::destroy()
{
  STORAGE_LOG(INFO, "[LOB]destroy");
  // TODO
  // 1. LobOperator.destroy()
  allocator_.reset();
  is_inited_ = false;
}

// Only use for default lob col val
int ObLobManager::fill_lob_header(ObIAllocator &allocator, ObString &data, ObString &out)
{
  int ret = OB_SUCCESS;
  void* buf = allocator.alloc(data.length() + sizeof(ObLobCommon));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to alloc memory for lob data", K(ret), K(data));
  } else {
    ObLobCommon *lob_data = new(buf)ObLobCommon();
    MEMCPY(lob_data->buffer_, data.ptr(), data.length());
    out.assign_ptr(reinterpret_cast<char*>(buf), static_cast<ObString::obstr_size_t>(data.length() + sizeof(ObLobCommon)));
  }
  return ret;
}

// Only use for default lob col val.
int ObLobManager::fill_lob_header(
    ObIAllocator &allocator,
    blocksstable::ObStorageDatum &datum)
{
  int ret = OB_SUCCESS;
  if (datum.is_null() || datum.is_nop_value()) {
  } else {
    ObString data = datum.get_string();
    ObString out;
    if (OB_FAIL(ObLobManager::fill_lob_header(allocator, data, out))) {
    } else {
      datum.set_string(out);
    }
  }
  return ret;
}

// Only use for default lob col val
int ObLobManager::fill_lob_header(ObIAllocator &allocator,
    const ObIArray<share::schema::ObColDesc> &column_ids,
    blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < column_ids.count(); ++i) {
    if (column_ids.at(i).col_type_.is_lob_storage()) {
      if (OB_FAIL(fill_lob_header(allocator, datum_row.storage_datums_[i]))) {
      }
    }
  }
  return ret;
}


// delta tmp lob locator
// Content:
// ObMemLobCommon |
// tmp delta disk locator | -> [ObLobCommon : {inrow : 1, init : 0}]
// inline buffer | [tmp_header][persis disk locator][tmp_diff][inline_data]

// full tmp lob locator
// Content:
// ObMemLobCommon |
// ObMemLobOraCommon |
// disk locator | -> [ObLobCommon : {inrow : 1, init : 0}]
// inline buffer | [inline_data]

int ObLobManager::query(
    ObLobAccessParam& param,
    ObString& output_data)
{
  int ret = OB_SUCCESS;
  bool is_char = param.coll_type_ != common::ObCollationType::CS_TYPE_BINARY;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLobManager is not initialized", K(ret));
  } else if (OB_FAIL(param.prepare())) {
  } else {
    ObLobCommon *lob_common = param.lob_common_;
    if (OB_ISNULL(lob_common)) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("get lob data null.", K(ret));
    } else if (OB_FAIL(param.check_handle_size())) {
    } else if (lob_common->in_row_ || (param.lob_locator_ != nullptr && param.lob_locator_->has_inrow_data())) {
      ObString data;
      if (param.lob_locator_ != nullptr && param.lob_locator_->has_inrow_data()) {
        if (OB_FAIL(param.lob_locator_->get_inrow_data(data))) {
        }
      } else { // lob_common->in_row_
        if (lob_common->is_init_) {
          param.lob_data_ = reinterpret_cast<ObLobData*>(lob_common->buffer_);
          data.assign_ptr(param.lob_data_->buffer_, param.lob_data_->byte_size_);
        } else {
          data.assign_ptr(lob_common->buffer_, param.byte_size_);
        }
      }
      uint32_t byte_offset = param.offset_ > data.length() ? data.length() : param.offset_;
      uint32_t max_len = ObCharset::strlen_char(param.coll_type_, data.ptr(), data.length()) - byte_offset;
      uint32_t byte_len = (param.len_ > max_len) ? max_len : param.len_;
      ObLobCharsetUtil::transform_query_result_charset(param.coll_type_, data.ptr(), data.length(), byte_len, byte_offset);
      if (OB_UNLIKELY(data.length() < byte_offset + byte_len)) {
        ret = OB_SIZE_OVERFLOW;
        LOG_WARN("data length is not enough.", K(ret), KPC(lob_common), KPC(param.lob_data_), K(byte_offset), K(byte_len));
      } else if (param.inrow_read_nocopy_) {
        output_data.assign_ptr(data.ptr() + byte_offset, byte_len);
      } else if (output_data.write(data.ptr() + byte_offset, byte_len) != byte_len) {
        ret = OB_ERR_INTERVAL_INVALID;
        LOG_WARN("failed to write buffer to output_data.", K(ret), K(output_data), K(byte_offset), K(byte_len));
      }
    } else if (OB_FAIL(query_outrow(param, output_data))) {
    }
  }
  return ret;
}

int ObLobManager::query_inrow_get_iter(
    ObLobAccessParam& param,
    ObString &data,
    uint32_t offset,
    bool scan_backward,
    ObLobQueryIter *&result)
{
  int ret = OB_SUCCESS;
  bool is_char = param.coll_type_ != common::ObCollationType::CS_TYPE_BINARY;
  uint32_t byte_offset = offset;
  uint32_t byte_len = param.len_;
  if (byte_offset > data.length()) {
    byte_offset = data.length();
  }
  if (byte_len + byte_offset > data.length()) {
    byte_len = data.length() - byte_offset;
  }
  if (is_char) {
    ObLobCharsetUtil::transform_query_result_charset(param.coll_type_, data.ptr(), data.length(), byte_len, byte_offset);
  }
  if (OB_UNLIKELY(data.length() < byte_offset + byte_len)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("data length is not enough.", K(ret), K(byte_offset), K(param.len_));
  } else {
    ObLobInRowQueryIter* iter = OB_NEW(ObLobInRowQueryIter, ObMemAttr("LobQueryIter"));
    if (OB_ISNULL(iter)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("alloc lob meta scan iterator fail", K(ret));
    } else if (OB_FAIL(iter->open(data, byte_offset, byte_len, param.coll_type_, scan_backward))) {
    } else {
      result = iter;
    }
  }
  return ret;
}

int ObLobManager::query(
    ObLobAccessParam& param,
    ObLobQueryIter *&result)
{
  int ret = OB_SUCCESS;
  bool is_in_row = false;
  bool is_char = param.coll_type_ != common::ObCollationType::CS_TYPE_BINARY;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLobManager is not initialized", K(ret));
  } else if (OB_FAIL(param.prepare())) {
  } else {
    ObLobCommon *lob_common = param.lob_common_;
    if (OB_ISNULL(lob_common)) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("get lob data null.", K(ret));
    } else if (OB_FAIL(param.check_handle_size())) {
    } else if (param.lob_locator_ != nullptr && param.lob_locator_->has_inrow_data()) {
      ObString data;
      if (OB_FAIL(param.lob_locator_->get_inrow_data(data))) {
      } else if (OB_FAIL(query_inrow_get_iter(param, data, param.offset_, param.scan_backward_, result))) {
        LOG_WARN("fail to get inrow query iter", K(ret));
        if (OB_NOT_NULL(result)) {
          result->reset();
          OB_DELETE(ObLobQueryIter, "unused", result);
          result = nullptr;
        }
      }
    } else if (lob_common->in_row_) {
      ObString data;
      if (lob_common->is_init_) {
        param.lob_data_ = reinterpret_cast<ObLobData*>(lob_common->buffer_);
        data.assign_ptr(param.lob_data_->buffer_, param.lob_data_->byte_size_);
      } else {
        data.assign_ptr(lob_common->buffer_, param.byte_size_);
      }
      if (OB_FAIL(query_inrow_get_iter(param, data, param.offset_, param.scan_backward_, result))) {
        LOG_WARN("fail to get inrow query iter", K(ret));
        if (OB_NOT_NULL(result)) {
          result->reset();
          OB_DELETE(ObLobQueryIter, "unused", result);
          result = nullptr;
        }
      }
    } else if (OB_FAIL(query_outrow(param, result))) {
    }
  }
  return ret;
}

int ObLobManager::query(ObString& data, ObLobQueryIter *&result)
{
  INIT_SUCC(ret);
  ObLobInRowQueryIter* iter = OB_NEW(ObLobInRowQueryIter, ObMemAttr("LobQueryIter"));
  if (OB_ISNULL(iter)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("alloc lob meta scan iterator fail", K(ret));
  } else if (OB_FAIL(iter->open(data, 0, data.length(), CS_TYPE_BINARY, false))) {
  } else {
    result = iter;
  }
  return ret;  
}

int ObLobManager::load_all(ObLobAccessParam &param, ObLobPartialData &partial_data)
{
  INIT_SUCC(ret);
  char *output_buf = nullptr;
  uint64_t output_len = param.byte_size_;
  ObString output_data;
  if (OB_ISNULL(output_buf = static_cast<char*>(param.allocator_->alloc(output_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc fail", K(ret), K(param));
  } else if (OB_FALSE_IT(output_data.assign_buffer(output_buf, output_len))) {
  } else if (OB_FAIL(query(param, output_data))) {
  } else if (OB_FAIL(partial_data.data_.push_back(ObLobChunkData(output_data)))) {
  } else {
    ObLobSeqId seq_id_generator(param.allocator_);
    ObString seq_id;
    uint64_t offset = 0;
    int64_t chunk_count = (param.byte_size_ + partial_data.chunk_size_ - 1)/partial_data.chunk_size_;
    for (int64_t i = 0; OB_SUCC(ret) && i < chunk_count; ++i) {
      ObLobChunkIndex chunk_index;
      chunk_index.offset_ = offset;
      chunk_index.pos_ = offset;
      chunk_index.byte_len_ = std::min(output_len, offset + partial_data.chunk_size_) - offset;
      chunk_index.data_idx_ = 0;
      if (OB_FAIL(seq_id_generator.get_next_seq_id(seq_id))) {
      } else if (OB_FAIL(ob_write_string(*param.allocator_, seq_id, chunk_index.seq_id_))) {
      } else if (OB_FAIL(partial_data.push_chunk_index(chunk_index))) {
      } else {
        offset += partial_data.chunk_size_;
      }
    }
  }
  return ret;
}

int ObLobManager::query(
    ObIAllocator *allocator,
    ObLobLocatorV2 &locator,
    int64_t query_timeout_ts,
    bool is_load_all,
    ObLobPartialData *partial_data,
    ObLobCursor *&cursor)
{
  INIT_SUCC(ret);
  ObLobAccessParam *param = nullptr;
  bool is_partial_data_alloc = false;
  if (! locator.has_lob_header() || ! locator.is_persist_lob() || locator.is_inrow()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid locator", KR(ret), K(locator));
  } else if (OB_ISNULL(cursor = OB_NEWx(ObLobCursor, allocator))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("alloc fail", K(ret), "size", sizeof(ObLobCursor));
  } else if (OB_ISNULL(param = OB_NEWx(ObLobAccessParam, allocator))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("alloc fail", K(ret), "size", sizeof(ObLobAccessParam));
  } else if (OB_FAIL(build_lob_param(*param, *allocator, CS_TYPE_BINARY,
                      0, UINT64_MAX, query_timeout_ts, locator))) {
  } else if (! param->lob_common_->is_init_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lob common not init", K(ret), KPC(param->lob_common_), KPC(param));
  } else if (OB_ISNULL(param->lob_data_ = reinterpret_cast<ObLobData*>(param->lob_common_->buffer_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lob data is null", K(ret), KPC(param->lob_common_), KPC(param));
  } else if (OB_ISNULL(partial_data)) {
    is_partial_data_alloc = true;
    if (OB_ISNULL(partial_data = OB_NEWx(ObLobPartialData, allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc lob param fail", K(ret), "size", sizeof(ObLobPartialData));
    } else if (OB_FAIL(partial_data->init())) {
    } else if (OB_FAIL(locator.get_chunk_size(partial_data->chunk_size_))) {
    } else {
      partial_data->data_length_ = param->byte_size_;
      partial_data->locator_.assign_ptr(locator.ptr_, locator.size_);
      if (is_load_all && OB_FAIL(load_all(*param, *partial_data))) {
        LOG_WARN("load_all fail", K(ret));
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(cursor->init(allocator, param, partial_data, lob_ctx_.lob_meta_mngr_))) {
  }

  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(cursor)) {
      cursor->~ObLobCursor();
      cursor = nullptr;
    }
    if (OB_NOT_NULL(partial_data) && is_partial_data_alloc) {
      partial_data->~ObLobPartialData();
      partial_data = nullptr;
    }
  }
  return ret;
}

int ObLobManager::equal(ObLobLocatorV2& lob_left,
                        ObLobLocatorV2& lob_right,
                        ObLobCompareParams& cmp_params,
                        bool& result)
{
  INIT_SUCC(ret);
  int64_t old_len = 0;
  int64_t new_len = 0;
  int64_t cmp_res = 0;
  if (OB_FAIL(lob_left.get_lob_data_byte_len(old_len))) {
  } else if (OB_FAIL(lob_right.get_lob_data_byte_len(new_len))) {
  } else if (new_len != old_len) {
    result = false;
  } else if (lob_left.has_inrow_data() && lob_right.has_inrow_data()) {
    // do both inrow check
    ObString left_str;
    ObString right_str;
    if (OB_FAIL(lob_left.get_inrow_data(left_str))) {
    } else if (OB_FAIL(lob_right.get_inrow_data(right_str))) {
    } else {
      result = (0 == MEMCMP(left_str.ptr(), right_str.ptr(), left_str.length()));
    }
  } else if (OB_FAIL(compare(lob_left, lob_right, cmp_params, cmp_res))) {
  } else {
    result = (0 == cmp_res);
  }
  return ret;
}

int ObLobManager::compare(ObLobLocatorV2& lob_left,
                          ObLobLocatorV2& lob_right,
                          ObLobCompareParams& cmp_params,
                          int64_t& result) {
  INIT_SUCC(ret);
  ObArenaAllocator tmp_allocator("LobCmp", OB_MALLOC_MIDDLE_BLOCK_SIZE);
  ObLobManager *lob_mngr = ::oceanbase::share::server_service<::oceanbase::storage::ObLobManager>();
  if (OB_ISNULL(lob_mngr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get lob manager handle null.", K(ret));
  } else if(!lob_left.has_lob_header() || !lob_right.has_lob_header()) {
    ret = OB_ERR_ARG_INVALID;
    LOG_WARN("invalid lob. should have lob locator", K(ret));
  } else {
    // get lob access param
    ObLobAccessParam param_left;
    ObLobAccessParam param_right;
    if (OB_FAIL(build_lob_param(param_left, tmp_allocator, cmp_params.collation_left_,
                cmp_params.offset_left_, cmp_params.compare_len_, cmp_params.timeout_, lob_left))) {
    } else if(OB_FAIL(build_lob_param(param_right, tmp_allocator, cmp_params.collation_right_,
                cmp_params.offset_right_, cmp_params.compare_len_, cmp_params.timeout_, lob_right))) {
    } else if(OB_FAIL(compare(param_left, param_right, result))) {
    }
  }
  return ret;
}

int ObLobManager::compare(ObLobAccessParam& param_left,
                          ObLobAccessParam& param_right,
                          int64_t& result) {
  INIT_SUCC(ret);
  common::ObCollationType collation_left = param_left.coll_type_;
  common::ObCollationType collation_right = param_right.coll_type_;
  common::ObCollationType cmp_collation = collation_left;
  ObIAllocator* tmp_allocator = param_left.allocator_;
  ObLobQueryIter *iter_left = nullptr;
  ObLobQueryIter *iter_right = nullptr;
  if(OB_ISNULL(tmp_allocator)) {
    ret = OB_ERR_ARG_INVALID;
    LOG_WARN("invalid alloctor param", K(ret), K(param_left));
  } else if((collation_left == CS_TYPE_BINARY && collation_right != CS_TYPE_BINARY)
            || (collation_left != CS_TYPE_BINARY && collation_right == CS_TYPE_BINARY)) {
    ret = OB_ERR_ARG_INVALID;
    LOG_WARN("invalid collation param", K(ret), K(param_left), K(param_right));
  } else if (OB_FAIL(query(param_left, iter_left))) {
  } else if (OB_FAIL(query(param_right, iter_right))) {
  } else {
    uint64_t read_buff_size = OB_MALLOC_MIDDLE_BLOCK_SIZE; // 64KB
    char *read_buff = nullptr;
    char *charset_convert_buff_ptr = nullptr;
    bool need_convert_charset = (collation_left != CS_TYPE_BINARY);
    uint64_t charset_convert_buff_size = need_convert_charset ?
                                         read_buff_size * ObCharset::CharConvertFactorNum : 0;

    if (OB_ISNULL((read_buff = static_cast<char*>(tmp_allocator->alloc(read_buff_size * 2))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc read buffer failed.", K(ret), K(read_buff_size));
    } else if (need_convert_charset &&
               OB_ISNULL((charset_convert_buff_ptr = static_cast<char*>(tmp_allocator->alloc(charset_convert_buff_size))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc charset convert buffer failed.", K(ret), K(charset_convert_buff_size));
    } else {
      ObDataBuffer charset_convert_buff(charset_convert_buff_ptr, charset_convert_buff_size);
      ObString read_buffer_left;
      ObString read_buffer_right;
      read_buffer_left.assign_buffer(read_buff, read_buff_size);
      read_buffer_right.assign_buffer(read_buff + read_buff_size, read_buff_size);

      // compare right after charset convert
      ObString convert_buffer_right;
      convert_buffer_right.assign_ptr(nullptr, 0);

      while (OB_SUCC(ret) && result == 0) {
        if (read_buffer_left.length() == 0) {
          // reset buffer and read next block
          read_buffer_left.assign_buffer(read_buff, read_buff_size);
          if (OB_FAIL(iter_left->get_next_row(read_buffer_left))) {
            if (ret != OB_ITER_END) {
              LOG_WARN("failed to get next buffer for left lob.", K(ret));
            } else {
              ret = OB_SUCCESS;
            }
          }
        }

        if (OB_SUCC(ret) && convert_buffer_right.length() == 0) {
          read_buffer_right.assign_buffer(read_buff + read_buff_size, read_buff_size);
          charset_convert_buff.set_data(charset_convert_buff_ptr, charset_convert_buff_size);
          convert_buffer_right.assign_ptr(nullptr, 0);

          if (OB_FAIL(iter_right->get_next_row(read_buffer_right))) {
            if (ret != OB_ITER_END) {
              LOG_WARN("failed to get next buffer for right lob", K(ret));
            } else {
              ret = OB_SUCCESS;
            }
          } else if (need_convert_charset) {
            // convert right lob to left charset if necessary
            if(OB_FAIL(sql::ObExprUtil::convert_string_collation(
                                  read_buffer_right, collation_right,
                                  convert_buffer_right, cmp_collation,
                                  charset_convert_buff))) {
            }
          } else {
            convert_buffer_right.assign_ptr(read_buffer_right.ptr(), read_buffer_right.length());
          }
        }
        if (OB_SUCC(ret)) {
          if (read_buffer_left.length() == 0 && convert_buffer_right.length() == 0) {
            result = 0;
            ret = OB_ITER_END;
          } else if (read_buffer_left.length() == 0 && convert_buffer_right.length() > 0) {
            result = -1;
          } else if (read_buffer_left.length() > 0 && convert_buffer_right.length() == 0) {
            result = 1;
          } else {
            uint64_t cmp_len = read_buffer_left.length() > convert_buffer_right.length() ?
                                    convert_buffer_right.length() : read_buffer_left.length();
            ObString substr_lob_left;
            ObString substr_lob_right;
            substr_lob_left.assign_ptr(read_buffer_left.ptr(), cmp_len);
            substr_lob_right.assign_ptr(convert_buffer_right.ptr(), cmp_len);
            result = common::ObCharset::strcmp(cmp_collation, substr_lob_left, substr_lob_right);
            if (result > 0) {
              result = 1;
            } else if (result < 0) {
              result = -1;
            }

            read_buffer_left.assign_ptr(read_buffer_left.ptr() + cmp_len, read_buffer_left.length() - cmp_len);
            convert_buffer_right.assign_ptr(convert_buffer_right.ptr() + cmp_len, convert_buffer_right.length() - cmp_len);
          }
        }
      }
      if (ret == OB_ITER_END) {
        ret = OB_SUCCESS;
      }
    }
    if (OB_NOT_NULL(read_buff)) {
      tmp_allocator->free(read_buff);
    }
    if (OB_NOT_NULL(charset_convert_buff_ptr)) {
      tmp_allocator->free(charset_convert_buff_ptr);
    }
  }
  if (OB_NOT_NULL(iter_left)) {
    iter_left->reset();
    OB_DELETE(ObLobQueryIter, "unused", iter_left);
  }
  if (OB_NOT_NULL(iter_right)) {
    iter_right->reset();
    OB_DELETE(ObLobQueryIter, "unused", iter_right);
  }
  return ret;
}

void ObLobManager::transform_lob_id(uint64_t src, uint64_t &dst)
{
  dst = htonll(src << 1);
  char *bytes = reinterpret_cast<char*>(&dst);
  bytes[7] |= 0x01;
}

int ObLobManager::check_need_out_row(
    ObLobAccessParam& param,
    int64_t add_len,
    ObString &data,
    bool need_combine_data,
    bool alloc_inside,
    bool &need_out_row)
{
  int ret = OB_SUCCESS;
  if (param.main_table_rowkey_col_) {
    need_out_row = false;
  } else {
    need_out_row = (param.byte_size_ + add_len) > param.get_inrow_threshold();
    if (param.lob_locator_ != nullptr) {
      // TODO @lhd remove after tmp lob support outrow
      if (!param.lob_locator_->is_persist_lob()) {
        need_out_row = false;
      }
    }
  }
  // in_row : 0 | need_out_row : 0  --> invalid
  // in_row : 0 | need_out_row : 1  --> do nothing, keep out_row
  // in_row : 1 | need_out_row : 0  --> do nothing, keep in_row
  // in_row : 1 | need_out_row : 1  --> in_row to out_row
  if (need_out_row && param.is_index_table_) {
    // The inrow datum may read from a table with different lob_inrow_threshold, which need out row in current table.
    // If the column is outrow in main table, the index table can not be written data.
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "outrow lob in index table");
    LOG_WARN("outrow lob in index table is not supported", K(ret));
  } else if (!param.lob_common_->in_row_ && !need_out_row) {
    if (!param.lob_common_->is_init_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid lob data", K(ret), KPC(param.lob_common_), K(data));
    } else if (param.byte_size_ > 0) {
      need_out_row = true;
    } else {
      // currently only insert support outrow -> inrow
      ObLobCommon *lob_common = nullptr;
      if (OB_ISNULL(lob_common = OB_NEWx(ObLobCommon, param.allocator_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc buf failed.", K(ret), "size", sizeof(ObLobCommon));
      } else {
        lob_common->in_row_ = 1;
        param.lob_common_ = lob_common;
        param.lob_data_ = nullptr;
        param.lob_locator_ = nullptr;
        param.handle_size_ = sizeof(ObLobCommon);
      }
    }
  } else if (param.lob_common_->in_row_ && need_out_row) {
    // combine lob_data->buffer and data
    if (need_combine_data) {
      if (param.byte_size_ > 0) {
        uint64_t total_size = param.byte_size_ + data.length();
        char *buf = static_cast<char*>(param.allocator_->alloc(total_size));
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("alloc buf failed.", K(ret), K(total_size));
        } else {
          MEMCPY(buf, param.lob_common_->get_inrow_data_ptr(), param.byte_size_);
          MEMCPY(buf + param.byte_size_, data.ptr(), data.length());
          data.assign_ptr(buf, total_size);
        }
      }
    } else {
      data.assign_ptr(param.lob_common_->get_inrow_data_ptr(), param.byte_size_);
    }

    // alloc full lob out row header
    if (OB_SUCC(ret)) {
      char *buf = static_cast<char*>(param.allocator_->alloc(ObLobConstants::LOB_OUTROW_FULL_SIZE));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc buf failed.", K(ret));
      } else if (OB_FAIL(is_store_char_len(param, param.get_schema_chunk_size(), add_len))) {
      } else {
        MEMCPY(buf, param.lob_common_, sizeof(ObLobCommon));
        ObLobCommon *new_lob_common = reinterpret_cast<ObLobCommon*>(buf);
        if (new_lob_common->is_init_) {
          MEMCPY(new_lob_common->buffer_, param.lob_common_->buffer_, sizeof(ObLobData));
        } else {
          // init lob data and alloc lob id(when not init)
          ObLobData *new_lob_data = new(new_lob_common->buffer_)ObLobData();
          if (OB_FAIL(lob_ctx_.lob_meta_mngr_->fetch_lob_id(param, new_lob_data->id_.lob_id_))) {
          } else if (! param.lob_meta_tablet_id_.is_valid()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("lob_meta_tablet_id is invalid", K(ret), K(param));
          } else {
            new_lob_data->id_.tablet_id_ = param.lob_meta_tablet_id_.id();
            transform_lob_id(new_lob_data->id_.lob_id_, new_lob_data->id_.lob_id_);
            new_lob_common->is_init_ = true;
          }
        }
        if (OB_SUCC(ret)) {
          if (alloc_inside) {
            param.allocator_->free(param.lob_common_);
          }
          param.lob_common_ = new_lob_common;
          param.lob_data_ = reinterpret_cast<ObLobData*>(param.lob_common_->buffer_);
          // refresh in_row flag
          param.lob_common_->in_row_ = 0;
          // init out row ctx
          ObLobDataOutRowCtx *ctx = new(param.lob_data_->buffer_)ObLobDataOutRowCtx();
          ctx->chunk_size_ = param.get_schema_chunk_size() / ObLobDataOutRowCtx::OUTROW_LOB_CHUNK_SIZE_UNIT;
          // init char len
          uint64_t *char_len = reinterpret_cast<uint64_t*>(ctx + 1);
          *char_len = (param.is_store_char_len_) ? 0 : UINT64_MAX;
          param.handle_size_ = ObLobConstants::LOB_OUTROW_FULL_SIZE;
        }
      }
    }
  } else if (! param.lob_common_->in_row_ && need_out_row) {
    // outrow -> outrow : keep outrow
    int64_t store_chunk_size = 0;
    bool has_char_len = param.lob_handle_has_char_len();
    if (add_len < 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("add_len is negative", K(ret), K(param));
    } else if (add_len == 0) {
      // no data add, keep char_len state
      param.is_store_char_len_ = has_char_len;
    } else if (OB_FAIL(param.get_store_chunk_size(store_chunk_size))) {
    } else if (OB_FAIL(is_store_char_len(param, store_chunk_size, add_len))) {
    } else if (param.op_type_ != ObLobDataOutRowCtx::OpType::SQL) {
      if (! param.is_store_char_len_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected case", K(ret), K(param), K(has_char_len));
      }
    } else if (0 != param.offset_ || 0 != param.byte_size_) {
      if (! param.is_store_char_len_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected case", K(ret), K(param), K(has_char_len));
      }
    } else if (has_char_len && param.is_store_char_len_) {
      // keep char_len
    } else if (! has_char_len && ! param.is_store_char_len_) {
      // keep no char_len
    } else if (has_char_len && ! param.is_store_char_len_) {
      // old data has char , but new data no char_len
      // reset char_len to UINT64_MAX from 0
      int64_t *char_len = param.get_char_len_ptr();
      if (OB_ISNULL(char_len)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("char_len ptr is null", K(ret), K(param), K(has_char_len));
      } else if (*char_len != 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("char_len should be zero", K(ret), K(param), K(has_char_len), K(*char_len));
      } else {
        *char_len = UINT64_MAX;
      }
    } else if (! has_char_len && param.is_store_char_len_) {
      if (param.handle_size_ < ObLobConstants::LOB_OUTROW_FULL_SIZE) {
        LOG_INFO("old old data", K(param));
        param.is_store_char_len_ = true;
      } else if (param.is_full_insert()) {
        // reset char_len to 0 from UINT64_MAX
        int64_t *char_len = param.get_char_len_ptr();
        if (OB_ISNULL(char_len)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("char_len ptr is null", K(ret), K(param), K(has_char_len));
        } else if (*char_len != UINT64_MAX) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("char_len should be zero", K(ret), K(param), K(has_char_len), K(*char_len));
        } else {
          *char_len = 0;
        }
      } else {
        // Partial update always stores char_len in MySQL-only mode.
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unsupport situation", K(ret), K(param), K(has_char_len));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unkown situation", K(ret), K(param), K(has_char_len));
    }
  }
  return ret;
}

int ObLobManager::append(
    ObLobAccessParam& param,
    ObLobLocatorV2 &lob)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_allocator("LobTmp", OB_MALLOC_MIDDLE_BLOCK_SIZE);
  param.set_tmp_allocator(&tmp_allocator);

  bool is_char = param.coll_type_ != common::ObCollationType::CS_TYPE_BINARY;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLobManager is not initialized", K(ret));
  } else if (OB_FAIL(param.prepare())) {
  } else if (!lob.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid lob locator", K(ret));
  } else if (!lob.has_lob_header()) { // 4.0 text tc compatiable
    ObString data;
    data.assign_ptr(lob.ptr_, lob.size_);
    if (OB_FAIL(append(param, data))) {
    }
  } else if (lob.is_delta_temp_lob()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid lob locator", K(ret));
  } else if (lob.has_inrow_data()) {
    ObString data;
    if (OB_FAIL(lob.get_inrow_data(data))) {
    } else if (OB_FAIL(append(param, data))) {
    }
  } else {
    bool alloc_inside = false;
    bool need_out_row = false;
    if (OB_FAIL(prepare_lob_common(param, alloc_inside))) {
    }
    ObLobCommon *lob_common = param.lob_common_;
    ObLobData *lob_data = param.lob_data_;
    int64_t append_lob_len = 0;
    ObString ori_inrow_data;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(param.check_handle_size())) {
    } else if (OB_FAIL(lob.get_lob_data_byte_len(append_lob_len))) {
    } else if (OB_FAIL(check_need_out_row(param, append_lob_len, ori_inrow_data, false, alloc_inside, need_out_row))) {
    } else if (OB_ISNULL(lob_common = param.lob_common_)) { // check_need_out_row may change lob_common
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("lob_commob is nul", K(ret), K(param), KPC(lob_common), KPC(lob_data), K(lob));
    } else if (!need_out_row) {
      // do inrow append
      int32_t cur_handle_size = lob_common->get_handle_size(param.byte_size_);
      int32_t ptr_offset = 0;
      if (OB_NOT_NULL(param.lob_locator_)) {
        ptr_offset = reinterpret_cast<char*>(param.lob_common_) - reinterpret_cast<char*>(param.lob_locator_->ptr_);
        cur_handle_size += ptr_offset;
      }
      uint64_t total_size = cur_handle_size + append_lob_len;
      char *buf = static_cast<char*>(param.allocator_->alloc(total_size));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc buf failed.", K(ret), K(total_size));
      } else {
        if (OB_NOT_NULL(param.lob_locator_)) {
          MEMCPY(buf, param.lob_locator_->ptr_, ptr_offset);
        }
        ObLobCommon *new_lob_common = reinterpret_cast<ObLobCommon*>(buf + ptr_offset);
        MEMCPY(new_lob_common, lob_common, cur_handle_size - ptr_offset);
        ObString data;
        data.assign_buffer(buf + cur_handle_size, append_lob_len);
        SMART_VAR(ObLobAccessParam, read_param) {
          
          if (OB_FAIL(build_lob_param(read_param, *param.get_tmp_allocator(), param.coll_type_,
                      0, UINT64_MAX, param.timeout_, lob))) {
          } else if (OB_FAIL(query(read_param, data))) {
          }
        }
        if (OB_SUCC(ret)) {
          // refresh lob info
          param.byte_size_ += data.length();
          if (new_lob_common->is_init_) {
            ObLobData *new_lob_data = reinterpret_cast<ObLobData*>(new_lob_common->buffer_);
            new_lob_data->byte_size_ += data.length();
          }
          if (alloc_inside) {
            param.allocator_->free(param.lob_common_);
          }
          param.lob_common_ = new_lob_common;
          param.handle_size_ = total_size;
          if (OB_NOT_NULL(param.lob_locator_)) {
            param.lob_locator_->ptr_ = buf;
            param.lob_locator_->size_ = total_size;
            if (OB_FAIL(fill_lob_locator_extern(param))) {
            }
          }
        }
      }
    } else if (OB_FAIL(append_outrow(param, lob, append_lob_len, ori_inrow_data))) {
    } else if (OB_FAIL(check_write_length(param, append_lob_len))) {
    }
  }
  param.set_tmp_allocator(nullptr);
  return ret;
}

int ObLobManager::append(ObLobAccessParam& param, ObLobLocatorV2& lob, ObLobMetaWriteIter &iter)
{
  int ret = OB_SUCCESS;
  bool is_char = param.coll_type_ != common::ObCollationType::CS_TYPE_BINARY;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLobManager is not initialized", K(ret));
  } else if (OB_FAIL(param.set_lob_locator(param.lob_locator_))) {
  } else if (!lob.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid lob locator", K(ret));
  } else if (lob.is_delta_temp_lob()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid lob locator", K(ret));
  } else {
    bool alloc_inside = false;
    bool need_out_row = false;
    if (OB_FAIL(prepare_lob_common(param, alloc_inside))) {
    }
    ObLobCommon *lob_common = param.lob_common_;
    ObLobData *lob_data = param.lob_data_;
    int64_t append_lob_len = 0;
    ObString ori_inrow_data;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(param.check_handle_size())) {
    } else if (OB_FAIL(lob.get_lob_data_byte_len(append_lob_len))) {
    } else if (OB_FAIL(check_need_out_row(param, append_lob_len, ori_inrow_data, false, alloc_inside, need_out_row))) {
    } else if (!need_out_row) {
      // do inrow append
      int32_t cur_handle_size = lob_common->get_handle_size(param.byte_size_);
      int32_t ptr_offset = 0;
      if (OB_NOT_NULL(param.lob_locator_)) {
        ptr_offset = reinterpret_cast<char*>(param.lob_common_) - reinterpret_cast<char*>(param.lob_locator_->ptr_);
        cur_handle_size += ptr_offset;
      }
      uint64_t total_size = cur_handle_size + append_lob_len;
      char *buf = static_cast<char*>(param.allocator_->alloc(total_size));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc buf failed.", K(ret), K(total_size));
      } else {
        if (OB_NOT_NULL(param.lob_locator_)) {
          MEMCPY(buf, param.lob_locator_->ptr_, ptr_offset);
        }
        ObLobCommon *new_lob_common = reinterpret_cast<ObLobCommon*>(buf + ptr_offset);
        MEMCPY(new_lob_common, lob_common, cur_handle_size - ptr_offset);
        ObString data;
        data.assign_buffer(buf + cur_handle_size, append_lob_len);
        SMART_VAR(ObLobAccessParam, read_param) {
          
          if (OB_FAIL(build_lob_param(read_param, *param.get_tmp_allocator(), param.coll_type_,
                      0, UINT64_MAX, param.timeout_, lob))) {
          } else if (OB_FAIL(query(read_param, data))) {
          }
        }
        if (OB_SUCC(ret)) {
          // refresh lob info
          param.byte_size_ += data.length();
          if (new_lob_common->is_init_) {
            ObLobData *new_lob_data = reinterpret_cast<ObLobData*>(new_lob_common->buffer_);
            new_lob_data->byte_size_ += data.length();
          }
          param.lob_common_ = new_lob_common;
          param.handle_size_ = total_size;
          if (OB_NOT_NULL(param.lob_locator_)) {
            param.lob_locator_->ptr_ = buf;
            param.lob_locator_->size_ = total_size;
            if (OB_FAIL(fill_lob_locator_extern(param))) {
            }
          }
        }
        iter.set_end();
      }
    } else if (!lob.has_lob_header()) {
      ObString data;
      data.assign_ptr(lob.ptr_, lob.size_);
      ObLobCtx lob_ctx = lob_ctx_;
      if (OB_FAIL(lob_ctx.lob_meta_mngr_->append(param, iter))) {
      }
    } else {
      // prepare out row ctx
      ObLobCtx lob_ctx = lob_ctx_;
      int64_t store_chunk_size = 0;
      if (OB_FAIL(param.init_out_row_ctx(append_lob_len))) {
      } else if (OB_FAIL(param.get_store_chunk_size(store_chunk_size))) {
      }
      // prepare read buffer
      ObString read_buffer;
      uint64_t read_buff_size = OB_MIN(store_chunk_size, append_lob_len);
      char *read_buff = nullptr;
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(read_buff = static_cast<char*>(param.get_tmp_allocator()->alloc(read_buff_size)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc read buffer failed.", K(ret), K(read_buff_size));
      } else {
        read_buffer.assign_buffer(read_buff, read_buff_size);
      }

      // prepare read full lob
      if (OB_SUCC(ret)) {
        ObLobLocatorV2* copy_locator = nullptr;
        ObLobAccessParam *read_param = reinterpret_cast<ObLobAccessParam*>(param.get_tmp_allocator()->alloc(sizeof(ObLobAccessParam)));
        if (OB_ISNULL(read_param)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("alloc read param failed.", K(ret), K(sizeof(ObLobAccessParam)));
        } else if (OB_ISNULL(copy_locator = OB_NEWx(ObLobLocatorV2, param.get_tmp_allocator()))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("alloc ObLobLocatorV2 failed.", K(ret), K(sizeof(ObLobLocatorV2)));
        } else {
          read_param = new(read_param)ObLobAccessParam();
          
          *copy_locator = lob;
          if (OB_FAIL(build_lob_param(*read_param, *param.get_tmp_allocator(), param.coll_type_,
                      0, UINT64_MAX, param.timeout_, *copy_locator))) {
          } else {
            ObLobQueryIter *qiter = nullptr;
            if (OB_FAIL(query(*read_param, qiter))) {
            } else if (OB_FAIL(iter.open(param, qiter, read_param, read_buffer))) {
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObLobManager::prepare_lob_common(ObLobAccessParam& param, bool &alloc_inside)
{
  int ret = OB_SUCCESS;
  alloc_inside = false;
  if (OB_ISNULL(param.lob_common_)) {
    // alloc new lob_data
    void *tbuf = param.allocator_->alloc(ObLobConstants::LOB_OUTROW_FULL_SIZE);
    if (OB_ISNULL(tbuf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate memory for LobData", K(ret));
    } else {
      // init full out row
      param.lob_common_ = new(tbuf)ObLobCommon();
      param.lob_data_ = new(param.lob_common_->buffer_)ObLobData();
      ObLobDataOutRowCtx *outrow_ctx = new(param.lob_data_->buffer_)ObLobDataOutRowCtx();
      outrow_ctx->chunk_size_ = param.get_schema_chunk_size() / ObLobDataOutRowCtx::OUTROW_LOB_CHUNK_SIZE_UNIT;
      // init char len
      uint64_t *char_len = reinterpret_cast<uint64_t*>(outrow_ctx + 1);
      *char_len = 0;
      param.handle_size_ = ObLobConstants::LOB_OUTROW_FULL_SIZE;
      alloc_inside = true;
    }
  } else if (param.lob_common_->is_init_) {
    param.lob_data_ = reinterpret_cast<ObLobData*>(param.lob_common_->buffer_);

    if (0 == param.lob_data_->byte_size_) {
      // that is insert when lob_data_->byte_size_ is zero.
      // so should update chunk size
      ObLobDataOutRowCtx *outrow_ctx = reinterpret_cast<ObLobDataOutRowCtx*>(param.lob_data_->buffer_);
      outrow_ctx->chunk_size_ = param.get_schema_chunk_size() / ObLobDataOutRowCtx::OUTROW_LOB_CHUNK_SIZE_UNIT;
    }
  }
  return ret;
}

int ObLobManager::append(
    ObLobAccessParam& param,
    ObString& data)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_allocator("LobTmp", OB_MALLOC_MIDDLE_BLOCK_SIZE);
  param.set_tmp_allocator(&tmp_allocator);
  bool save_is_reverse = param.scan_backward_;
  uint64_t save_param_len = param.len_;
  bool is_char = param.coll_type_ != common::ObCollationType::CS_TYPE_BINARY;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLobManager is not initialized", K(ret));
  } else if (OB_FAIL(param.prepare())) {
  } else {
    bool alloc_inside = false;
    bool need_out_row = false;
    if (OB_FAIL(prepare_lob_common(param, alloc_inside))) {
    }
    ObLobCommon *lob_common = param.lob_common_;
    ObLobData *lob_data = param.lob_data_;
    bool ori_is_inrow = (lob_common == nullptr) ? false : (lob_common->in_row_ == 1);
    int64_t store_chunk_size = 0;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(param.check_handle_size())) {
    } else if (OB_FAIL(check_need_out_row(param, data.length(), data, true, alloc_inside, need_out_row))) {
    } else if (OB_ISNULL(lob_common = param.lob_common_)) { // check_need_out_row may change lob_common
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("lob_common is nul", K(ret), K(param), KPC(lob_common), KPC(lob_data), K(data));
    } else if (!need_out_row) {
      // do inrow append
      int32_t cur_handle_size = lob_common->get_handle_size(param.byte_size_);
      int32_t ptr_offset = 0;
      if (OB_NOT_NULL(param.lob_locator_)) {
        ptr_offset = reinterpret_cast<char*>(param.lob_common_) - reinterpret_cast<char*>(param.lob_locator_->ptr_);
        cur_handle_size += ptr_offset;
      }
      uint64_t total_size = cur_handle_size + data.length();
      char *buf = static_cast<char*>(param.allocator_->alloc(total_size));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc buf failed.", K(ret), K(total_size));
      } else {
        if (OB_NOT_NULL(param.lob_locator_)) {
          MEMCPY(buf, param.lob_locator_->ptr_, ptr_offset);
        }
        ObLobCommon *new_lob_common = reinterpret_cast<ObLobCommon*>(buf + ptr_offset);
        MEMCPY(new_lob_common, lob_common, cur_handle_size - ptr_offset);
        MEMCPY(buf + cur_handle_size, data.ptr(), data.length());
        // refresh lob info
        param.byte_size_ += data.length();
        if (new_lob_common->is_init_) {
          ObLobData *new_lob_data = reinterpret_cast<ObLobData*>(new_lob_common->buffer_);
          new_lob_data->byte_size_ += data.length();
        }
        if (alloc_inside) {
          param.allocator_->free(param.lob_common_);
        }
        param.lob_common_ = new_lob_common;
        param.handle_size_ = total_size;
        if (OB_NOT_NULL(param.lob_locator_)) {
          param.lob_locator_->ptr_ = buf;
          param.lob_locator_->size_ = total_size;
          if (OB_FAIL(fill_lob_locator_extern(param))) {
          }
        }
      }
    } else if (OB_FAIL(append_outrow(param, ori_is_inrow, data))) {
    } else if (OB_FAIL(check_write_length(param, data.length()))) {
    }
  }
  if (OB_SUCC(ret)) {
    param.len_ = save_param_len;
    param.scan_backward_ = save_is_reverse;
  }
  param.set_tmp_allocator(nullptr);
  return ret;
}

int ObLobManager::prepare_for_write(
    ObLobAccessParam& param,
    ObString &old_data,
    bool &need_out_row)
{
  int ret = OB_SUCCESS;
  int64_t max_bytes_in_char = 4;
  uint64_t modified_end = param.offset_ + param.len_;
  if (param.coll_type_ != CS_TYPE_BINARY) {
    modified_end *= max_bytes_in_char;
  }
  uint64_t total_size = param.byte_size_ > modified_end ? param.byte_size_ : modified_end;
  need_out_row = (total_size > param.get_inrow_threshold());
  if (param.lob_common_->in_row_) {
    old_data.assign_ptr(param.lob_common_->get_inrow_data_ptr(), param.byte_size_);
  }
  if (param.lob_locator_ != nullptr) {
    // @lhd remove after tmp lob support outrow
    if (!param.lob_locator_->is_persist_lob()) {
      need_out_row = false;
    }
  }
  // in_row : 0 | need_out_row : 0  --> invalid
  // in_row : 0 | need_out_row : 1  --> do nothing, keep out_row
  // in_row : 1 | need_out_row : 0  --> do nothing, keep in_row
  // in_row : 1 | need_out_row : 1  --> in_row to out_row
  if (!param.lob_common_->in_row_ && !need_out_row) {
    if (!param.lob_common_->is_init_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid lob data", K(ret), KPC(param.lob_common_));
    } else {
      need_out_row = true;
    }
  } else if (param.lob_common_->in_row_ && need_out_row) {
    // alloc full lob out row header
    if (OB_SUCC(ret)) {
      char* buf = static_cast<char*>(param.allocator_->alloc(ObLobConstants::LOB_OUTROW_FULL_SIZE));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc buf failed.", K(ret), K(total_size));
      } else {
        MEMCPY(buf, param.lob_common_, sizeof(ObLobCommon));
        ObLobCommon *new_lob_common = reinterpret_cast<ObLobCommon*>(buf);
        new_lob_common->in_row_ = 0;
        if (new_lob_common->is_init_) {
          MEMCPY(new_lob_common->buffer_, param.lob_common_->buffer_, sizeof(ObLobData));
        } else {
          // init lob data and alloc lob id(when not init)
          ObLobData *new_lob_data = new(new_lob_common->buffer_)ObLobData();
          if (OB_FAIL(lob_ctx_.lob_meta_mngr_->fetch_lob_id(param, new_lob_data->id_.lob_id_))) {
          } else if (! param.lob_meta_tablet_id_.is_valid()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("lob_meta_tablet_id is invalid", K(ret), K(param));
          } else {
            new_lob_data->id_.tablet_id_ = param.lob_meta_tablet_id_.id();
            transform_lob_id(new_lob_data->id_.lob_id_, new_lob_data->id_.lob_id_);
            new_lob_common->is_init_ = true;
          }
        }
        if (OB_SUCC(ret)) {
          param.lob_common_ = new_lob_common;
          param.lob_data_ = reinterpret_cast<ObLobData*>(param.lob_common_->buffer_);
          // init out row ctx
          ObLobDataOutRowCtx *ctx = new(param.lob_data_->buffer_)ObLobDataOutRowCtx();
          // init char len
          uint64_t *char_len = reinterpret_cast<uint64_t*>(ctx + 1);
          *char_len = 0;
          param.handle_size_ = ObLobConstants::LOB_OUTROW_FULL_SIZE;
        }
      }
    }
  }
  return ret;
}

int ObLobManager::process_delta(ObLobAccessParam& param, ObLobLocatorV2& lob_locator)
{
  int ret = OB_SUCCESS;
  if (lob_locator.is_delta_temp_lob()) {
    ObString data;
    ObLobCommon *lob_common = nullptr;
    if (OB_FAIL(lob_locator.get_disk_locator(lob_common))) {
    } else if (!lob_common->in_row_) {
      ret = OB_NOT_IMPLEMENT;
      LOG_WARN("Unsupport out row delta tmp lob locator", K(ret), KPC(lob_common));
    } else {
      ObLobDiffHeader *diff_header = reinterpret_cast<ObLobDiffHeader*>(lob_common->buffer_);
      if (param.lob_common_ == nullptr) {
        ObLobCommon *persis_lob = diff_header->get_persist_lob();
        param.lob_locator_ = nullptr;
        param.lob_common_ = persis_lob;
        param.handle_size_ = diff_header->persist_loc_size_;
        param.byte_size_ = persis_lob->get_byte_size(param.handle_size_);
      }
      ObLobDiff *diffs = diff_header->get_diff_ptr();
      char *data_ptr = diff_header->get_inline_data_ptr();
      // process diffs
      for (int64_t i = 0 ; OB_SUCC(ret) && i < diff_header->diff_cnt_; ++i) {
        ObString tmp_data(diffs[i].byte_len_, data_ptr + diffs[i].offset_);
        param.offset_ = diffs[i].ori_offset_;
        switch (diffs[i].type_) {
          case ObLobDiff::DiffType::APPEND: {
            param.op_type_ = ObLobDataOutRowCtx::OpType::APPEND;
            param.len_ = diffs[i].ori_len_;
            ObLobLocatorV2 src_lob(tmp_data);
            if (OB_FAIL(append(param, src_lob))) {
            }
            if (ret == OB_SNAPSHOT_DISCARDED && src_lob.is_persist_lob()) {
              ret = OB_ERR_LOB_SPAN_TRANSACTION;
              LOG_WARN("fail to read src lob, make update inner sql do not retry", K(ret));
            }
            break;
          }
          case ObLobDiff::DiffType::WRITE: {
            param.op_type_ = ObLobDataOutRowCtx::OpType::WRITE;
            param.len_ = diffs[i].ori_len_;
            bool can_do_append = false;
            if (diffs[i].flags_.can_do_append_) {
              if (param.lob_handle_has_char_len()) {
                int64_t *len = param.get_char_len_ptr();
                if (*len == param.offset_) {
                  can_do_append = true;
                  param.offset_ = 0;
                }
              }
            }

            ObLobLocatorV2 src_lob(tmp_data);
            if (can_do_append) {
              if (OB_FAIL(append(param, src_lob))) {
              }
            } else {
              if (OB_FAIL(write(param, src_lob, diffs[i].dst_offset_))) {
              }
            }
            if (ret == OB_SNAPSHOT_DISCARDED && src_lob.is_persist_lob()) {
              ret = OB_ERR_LOB_SPAN_TRANSACTION;
              LOG_WARN("fail to read src lob, make update inner sql do not retry", K(ret));
            }
            break;
          }
          case ObLobDiff::DiffType::ERASE: {
            param.op_type_ = ObLobDataOutRowCtx::OpType::ERASE;
            param.len_ = diffs[i].ori_len_;
            if (OB_FAIL(erase(param))) {
            }
            break;
          }
          case ObLobDiff::DiffType::ERASE_FILL_ZERO: {
            param.op_type_ = ObLobDataOutRowCtx::OpType::WRITE;
            param.len_ = diffs[i].ori_len_;
            param.is_fill_zero_ = true;
            if (OB_FAIL(erase(param))) {
            }
            break;
          }
          case ObLobDiff::DiffType::WRITE_DIFF : {
            if (i != 0) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("first type must be write_diff", K(ret), K(i), K(diff_header), K(diffs[i]));
            } else if (OB_FAIL(process_diff(param, lob_locator, diff_header))) {
            } else {
              i = diff_header->diff_cnt_;
            }
            break;
          }
          default: {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid diff type", K(ret), K(i), K(diffs[i]));
          }
        }
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid lob locator type", K(ret), K(lob_locator));
  }
  return ret;
}

int ObLobManager::process_diff(ObLobAccessParam& param, ObLobLocatorV2& delta_locator, ObLobDiffHeader *diff_header)
{
  int ret = OB_SUCCESS;
  ObLobDiffUpdateHandler handler(param);
  if (OB_FAIL(handler.init(lob_ctx_.lob_meta_mngr_))) {
  } else if (OB_FAIL(handler.execute(delta_locator, diff_header))) {
  }
  return ret;
}

int ObLobManager::fill_lob_locator_extern(ObLobAccessParam& param)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(param.lob_locator_)) {
    if (param.lob_locator_->has_extern()) {
      ObMemLobExternHeader *ext_header = nullptr;
      if (OB_FAIL(param.lob_locator_->get_extern_header(ext_header))) {
      } else {
        ext_header->payload_size_ = param.byte_size_;
      }
    }
  }
  return ret;
}

int ObLobManager::getlength(ObLobAccessParam& param, uint64_t &len)
{
  int ret = OB_SUCCESS;
  bool is_char = param.coll_type_ != common::ObCollationType::CS_TYPE_BINARY;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLobManager is not initialized", K(ret));
  } else if (OB_FAIL(param.prepare())) {
  } else {
    ObLobCommon *lob_common = param.lob_common_;
    if (OB_ISNULL(lob_common)) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("get lob data null.", K(ret));
    } else if (OB_FAIL(param.check_handle_size())) {
    } else if (!is_char) { // return byte len
      len = lob_common->get_byte_size(param.handle_size_);
    } else if (param.lob_handle_has_char_len()) {
      len = *param.get_char_len_ptr();
    } else if (lob_common->in_row_ || // calc char len
               (param.lob_locator_ != nullptr && param.lob_locator_->has_inrow_data())) {
      ObString data;
      if (param.lob_locator_ != nullptr && param.lob_locator_->has_inrow_data()) {
        if (OB_FAIL(param.lob_locator_->get_inrow_data(data))) {
        }
      } else {
        if (lob_common->is_init_) {
          param.lob_data_ = reinterpret_cast<ObLobData*>(lob_common->buffer_);
          data.assign_ptr(param.lob_data_->buffer_, param.lob_data_->byte_size_);
        } else {
          data.assign_ptr(lob_common->buffer_, param.byte_size_);
        }
      }
      if (OB_SUCC(ret)) {
        len = ObCharset::strlen_char(param.coll_type_, data.ptr(), data.length());
      }
    } else { // do meta scan
      ObLobQueryLengthHandler handler(param);
      if (OB_FAIL(handler.init(lob_ctx_.lob_meta_mngr_))) {
      } else  if (OB_FAIL(handler.execute())) {
      } else {
        len = handler.result_;
      }
    }
  }
  return ret;
}

int ObLobManager::write_inrow_inner(ObLobAccessParam& param, ObString& data, ObString& old_data)
{
  int ret = OB_SUCCESS;
  ObLobCommon *lob_common = param.lob_common_;
  int64_t cur_handle_size = lob_common->get_handle_size(param.byte_size_) - param.byte_size_;
  int64_t ptr_offset = 0;
  if (OB_NOT_NULL(param.lob_locator_)) {
    ptr_offset = reinterpret_cast<char*>(param.lob_common_) - reinterpret_cast<char*>(param.lob_locator_->ptr_);
    cur_handle_size += ptr_offset;
  }
  int64_t lob_cur_mb_len = ObCharset::strlen_char(param.coll_type_, lob_common->get_inrow_data_ptr(), param.byte_size_);
  int64_t offset_byte_len = 0;
  int64_t amount_byte_len = 0;
  int64_t lob_replaced_byte_len = 0;
  int64_t res_len = 0;
  if (param.offset_ >= lob_cur_mb_len) {
    offset_byte_len = param.byte_size_ + (param.offset_ - lob_cur_mb_len);
    amount_byte_len = ObCharset::charpos(param.coll_type_, data.ptr(), data.length(), param.len_);
    res_len = offset_byte_len + amount_byte_len;
  } else {
    offset_byte_len = ObCharset::charpos(param.coll_type_,
                                          old_data.ptr(),
                                          old_data.length(),
                                          param.offset_);
    amount_byte_len = ObCharset::charpos(param.coll_type_, data.ptr(), data.length(), param.len_);
    lob_replaced_byte_len = ObCharset::charpos(param.coll_type_,
                                                old_data.ptr() + offset_byte_len,
                                                old_data.length() - offset_byte_len,
                                                (param.len_ + param.offset_ > lob_cur_mb_len) ? (lob_cur_mb_len - param.offset_) : param.len_);
    res_len = old_data.length() - lob_replaced_byte_len + amount_byte_len;
  }

  res_len += cur_handle_size;
  char *buf = static_cast<char*>(param.allocator_->alloc(res_len));
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc buf failed.", K(ret), K(res_len));
  } else {
    ObString space = ObCharsetUtils::get_const_str(param.coll_type_, ' ');
    if (param.coll_type_ == CS_TYPE_BINARY) {
      MEMSET(buf, 0x00, res_len);
    } else {
      uint32_t space_len = space.length();
      if (space_len == 0 || res_len%space_len != 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid space_len or res-len", K(ret), K(res_len), K(space_len));
      } else if (space_len > 1) {
        for (int i = 0; i < res_len/space_len; i++) {
          MEMCPY(buf + i * space_len, space.ptr(), space_len);
        }
      } else {
        MEMSET(buf, *space.ptr(), res_len);
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      if (OB_NOT_NULL(param.lob_locator_)) {
        MEMCPY(buf, param.lob_locator_->ptr_, ptr_offset);
      }
      ObLobCommon *new_lob_common = reinterpret_cast<ObLobCommon*>(buf + ptr_offset);
      MEMCPY(new_lob_common, lob_common, cur_handle_size - ptr_offset);
      char* new_data_ptr = const_cast<char*>(new_lob_common->get_inrow_data_ptr());
      if (offset_byte_len >= old_data.length()) {
        MEMCPY(new_data_ptr, old_data.ptr(), old_data.length());
        MEMCPY(new_data_ptr + offset_byte_len, data.ptr(), amount_byte_len);
      } else {
        MEMCPY(new_data_ptr, old_data.ptr(), offset_byte_len);
        MEMCPY(new_data_ptr + offset_byte_len, data.ptr(), amount_byte_len);
        if (offset_byte_len + amount_byte_len < old_data.length()) {
          MEMCPY(new_data_ptr + offset_byte_len + amount_byte_len,
                  old_data.ptr() + offset_byte_len + lob_replaced_byte_len,
                  old_data.length() - offset_byte_len - lob_replaced_byte_len);
        }
      }

      // refresh lob info
      param.byte_size_ = res_len - cur_handle_size;
      if (new_lob_common->is_init_) {
        ObLobData *new_lob_data = reinterpret_cast<ObLobData*>(new_lob_common->buffer_);
        new_lob_data->byte_size_ = res_len - cur_handle_size;
      }
      param.lob_common_ = new_lob_common;
      param.handle_size_ = res_len;
      if (OB_NOT_NULL(param.lob_locator_)) {
        param.lob_locator_->ptr_ = buf;
        param.lob_locator_->size_ = res_len;
        if (OB_FAIL(fill_lob_locator_extern(param))) {
        }
      }
    }
  }
  return ret;
}

int ObLobManager::write_inrow(ObLobAccessParam& param, ObLobLocatorV2& lob, uint64_t offset, ObString& old_data)
{
  int ret = OB_SUCCESS;
  SMART_VAR(ObLobAccessParam, read_param) {
    if (OB_ISNULL(param.allocator_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("param tmp allocator is null", K(ret), K(param));
    } else if (OB_FAIL(build_lob_param(read_param, *param.allocator_, param.coll_type_,
                offset, param.len_, param.timeout_, lob))) {
    } else {
      ObLobQueryIter *iter = nullptr;
      if (OB_FAIL(query(read_param, iter))) {
      } else {
        // prepare read buffer
        ObString read_buffer;
        uint64_t read_buff_size = OB_MIN(LOB_READ_BUFFER_LEN, read_param.byte_size_);
        char *read_buff = static_cast<char*>(param.allocator_->alloc(read_buff_size));
        if (OB_ISNULL(read_buff)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("alloc buf failed.", K(ret), K(read_buff_size));
        } else {
          read_buffer.assign_buffer(read_buff, read_buff_size);
        }

        uint64_t write_offset = param.offset_;
        while (OB_SUCC(ret)) {
          if (OB_FAIL(iter->get_next_row(read_buffer))) {
            if (ret != OB_ITER_END) {
              LOG_WARN("failed to get next buffer.", K(ret));
            }
          } else {
            param.offset_ = write_offset;
            uint64_t read_char_len = ObCharset::strlen_char(param.coll_type_, read_buffer.ptr(), read_buffer.length());
            param.len_ = read_char_len;
            if (OB_FAIL(write_inrow_inner(param, read_buffer, old_data))) {
            } else {
              // update offset and len
              write_offset += read_char_len;
              old_data.assign_ptr(param.lob_common_->get_inrow_data_ptr(), param.byte_size_);
            }
          }
        }
        if (ret == OB_ITER_END) {
          ret = OB_SUCCESS;
        }
      }
      if (OB_NOT_NULL(iter)) {
        iter->reset();
        OB_DELETE(ObLobQueryIter, "unused", iter);
      }
    }
  }
  return ret;
}

int ObLobManager::write_outrow(ObLobAccessParam& param, ObLobLocatorV2& lob, uint64_t offset, ObString& old_data)
{
  int ret = OB_SUCCESS;
  ObLobQueryIter *iter = nullptr;
  SMART_VAR(ObLobAccessParam, read_param) {
    if (OB_ISNULL(param.allocator_)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("param tmp allocator is null", K(ret), K(param));
    } else if (OB_FAIL(build_lob_param(read_param, *param.allocator_, param.coll_type_,
                offset, param.len_, param.timeout_, lob))) {
    } else if (OB_FAIL(query(read_param, iter))) {
    } else {
      ObLobWriteHandler handler(param);
      // prepare read buffer
      ObString read_buffer;
      uint64_t read_buff_size = OB_MIN(LOB_READ_BUFFER_LEN, read_param.byte_size_);
      char *read_buff = static_cast<char*>(param.allocator_->alloc(read_buff_size));
      if (OB_ISNULL(read_buff)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc buf failed.", K(ret), K(read_buff_size));
      } else if (FALSE_IT(read_buffer.assign_buffer(read_buff, read_buff_size))) {
      } else if (OB_FAIL(handler.init(lob_ctx_.lob_meta_mngr_))) {
      } else if (OB_FAIL(handler.execute(iter, read_buffer, old_data))) {
      }
    }
  }

  if (OB_NOT_NULL(iter)) {
    iter->reset();
    OB_DELETE(ObLobQueryIter, "unused", iter);
  }
  return ret;
}

int ObLobManager::write(ObLobAccessParam& param, ObLobLocatorV2& lob, uint64_t offset)
{
  int ret = OB_SUCCESS;
  bool is_char = param.coll_type_ != common::ObCollationType::CS_TYPE_BINARY;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLobManager is not initialized", K(ret));
  } else if (OB_FAIL(param.prepare())) {
  } else {
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(param.check_handle_size())) {
    } else {
      ObString old_data;
      bool out_row = false;
      if (OB_FAIL(prepare_for_write(param, old_data, out_row))) {
      } else {
        if (!out_row) {
          if (OB_FAIL(write_inrow(param, lob, offset, old_data))) {
          }
        } else if (OB_FAIL(write_outrow(param, lob, offset, old_data))) {
        }
      }
    }
  }
  return ret;
}


int ObLobManager::fill_zero(char *ptr, uint64_t length, bool is_char,
  const ObCollationType coll_type, uint32_t byte_len, uint32_t byte_offset, uint32_t &char_len)
{
  int ret = OB_SUCCESS;
  ObString space = ObCharsetUtils::get_const_str(coll_type, ' ');
  uint32_t space_len = space.length();
  uint32_t converted_len = space.length() * char_len;
  if (converted_len > byte_len) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to fill zero for length invalid", K(ret), K(space_len), K(char_len), K(byte_len));
  } else {
    char* dst_start = ptr + byte_offset + converted_len;
    char* src_start = ptr + byte_offset + byte_len;
    uint32_t cp_len = length - (byte_len + byte_offset);
    if (cp_len > 0 && dst_start != src_start) {
      MEMMOVE(dst_start, src_start, cp_len);
    }
    if (!is_char) {
      MEMSET(ptr + byte_offset, 0x00, converted_len);
    } else {
      if (space_len > 1) {
        for (int i = 0; i < char_len; i++) {
          MEMCPY(ptr + byte_offset + i * space_len, space.ptr(), space_len);
        }
      } else {
        MEMSET(ptr + byte_offset, ' ', char_len);
      }
    }
    char_len = converted_len;
  }
  return ret;
}

int ObLobManager::erase_outrow(ObLobAccessParam& param)
{
  int ret = OB_SUCCESS;
  if (param.is_full_delete()) {
    ObLobFullDeleteHandler handler(param);
    if (OB_FAIL(handler.init(lob_ctx_.lob_meta_mngr_))) {
    } else if (OB_FAIL(handler.execute())) {
    }
  } else {
    ObLobEraseHandler handler(param);
    if (OB_FAIL(handler.init(lob_ctx_.lob_meta_mngr_))) {
    } else if (OB_FAIL(handler.execute())) {
    }
  }
  return ret;
}

int ObLobManager::erase(ObLobAccessParam& param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLobManager is not initialized", K(ret));
  } else if (OB_FAIL(param.prepare())) {
  } else {
    if (OB_FAIL(OB_ISNULL(param.lob_common_))) {
    } else if (OB_FAIL(param.check_handle_size())) {
    } else if (param.lob_common_->in_row_) {
      if (param.lob_common_->is_init_) {
        param.lob_data_ = reinterpret_cast<ObLobData*>(param.lob_common_->buffer_);
      }
      ObString data;
      if (param.lob_data_ != nullptr) {
        data.assign_ptr(param.lob_data_->buffer_, param.lob_data_->byte_size_);
      } else {
        data.assign_ptr(param.lob_common_->buffer_, param.byte_size_);
      }
      uint32_t byte_offset = param.offset_;
      if (OB_UNLIKELY(data.length() < byte_offset)) {
        // offset overflow, do nothing
      } else {
        // allow erase len oversize, get max(param.len_, actual_len)
        uint32_t max_len = ObCharset::strlen_char(param.coll_type_, data.ptr(), data.length()) - byte_offset;
        uint32_t char_len = (param.len_ > max_len) ? max_len : param.len_;
        uint32_t byte_len = char_len;
        ObLobCharsetUtil::transform_query_result_charset(param.coll_type_, data.ptr(), data.length(), byte_len, byte_offset);
        if (OB_UNLIKELY(data.length() < byte_offset + byte_len)) {
          ret = OB_SIZE_OVERFLOW;
          LOG_WARN("data length is not enough.", K(ret), KPC(param.lob_data_), K(byte_offset), K(byte_len));
        } else {
          if (param.is_fill_zero_) { // do fill zero
            bool is_char = (param.coll_type_ != CS_TYPE_BINARY);
            if (OB_FAIL(fill_zero(data.ptr(), data.length(), is_char, param.coll_type_, byte_len, byte_offset, char_len))) {
            } else {
              param.byte_size_ = param.byte_size_ - byte_len + char_len;
              if (param.lob_data_ != nullptr) {
                param.lob_data_->byte_size_ = param.byte_size_;
              }
              if (OB_NOT_NULL(param.lob_locator_)) {
                param.lob_locator_->size_ = param.lob_locator_->size_ - byte_len + char_len;
                if (OB_FAIL(fill_lob_locator_extern(param))) {
                }
              }
            }
          } else { // do erase
            char* dst_start = data.ptr() + byte_offset;
            char* src_start = data.ptr() + byte_offset + byte_len;
            uint32_t cp_len = data.length() - (byte_len + byte_offset);
            if (cp_len > 0) {
              MEMMOVE(dst_start, src_start, cp_len);
            }
            param.byte_size_ -= byte_len;
            param.handle_size_ -= byte_len;
            if (param.lob_data_ != nullptr) {
              param.lob_data_->byte_size_ = param.byte_size_;
            }
            if (OB_NOT_NULL(param.lob_locator_)) {
              param.lob_locator_->size_ -= byte_len;
              if (OB_FAIL(fill_lob_locator_extern(param))) {
              }
            }
          }
        }
      }
    } else if (param.is_fill_zero_) {
      ObLobFillZeroHandler handler(param);
      if (OB_FAIL(handler.init(lob_ctx_.lob_meta_mngr_))) {
      } else if (OB_FAIL(handler.execute())) {
      }
    } else if (OB_FAIL(erase_outrow(param))) {
    }
  }
  return ret;
}

int ObLobManager::build_lob_param(ObLobAccessParam& param,
                                  ObIAllocator &allocator,
                                  ObCollationType coll_type,
                                  uint64_t offset,
                                  uint64_t len,
                                  int64_t timeout,
                                  ObLobLocatorV2 &lob)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(param.set_lob_locator(&lob))) {
  } else {
    param.coll_type_ = coll_type;
    if (param.coll_type_ == CS_TYPE_INVALID) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("get collation type failed.", K(ret));
    } else {
      // common arg
      param.allocator_ = &allocator;
      param.byte_size_ = param.lob_common_->get_byte_size(param.handle_size_);
      param.offset_ = offset;
      param.len_ = len;
      param.timeout_ = timeout;
      // outrow arg for do lob meta scan
      if (OB_SUCC(ret) && lob.is_persist_lob() && !lob.has_inrow_data()) {
        ObMemLobLocationInfo *location_info = nullptr;
        if (OB_FAIL(lob.get_location_info(location_info))) {
        } else if (OB_FALSE_IT(param.tablet_id_ = ObTabletID(location_info->tablet_id_))) {
        } else if (OB_FAIL(param.set_tx_read_snapshot(lob))) {
        }
      }
    }
  }
  return ret;
}

int ObLobManager::append_outrow(ObLobAccessParam& param, bool ori_is_inrow, ObString &data)
{
  int ret = OB_SUCCESS;
  if (param.is_full_insert()) {
    ObLobFullInsertHandler handler(param);
    if (OB_FAIL(handler.init(lob_ctx_.lob_meta_mngr_))) {
    } else if (OB_FAIL(handler.execute(data))) {
    }
  } else {
    ObLobAppendHandler handler(param);
    if (OB_FAIL(handler.init(lob_ctx_.lob_meta_mngr_))) {
    } else if (OB_FAIL(handler.execute(data, ori_is_inrow))) {
    }
  }
  return ret;
}

int ObLobManager::append_outrow(
    ObLobAccessParam& param,
    ObLobLocatorV2& lob,
    int64_t append_lob_len,
    ObString& ori_inrow_data)
{
  int ret = OB_SUCCESS;
  ObLobQueryIter *iter = nullptr;
  SMART_VAR(ObLobAccessParam, read_param) {
    
    if (OB_ISNULL(param.get_tmp_allocator())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("param allocator is null", K(ret), K(param));
    } else if (OB_FAIL(build_lob_param(read_param, *param.get_tmp_allocator(), param.coll_type_,
                0, UINT64_MAX, param.timeout_, lob))) {
    } else if (OB_FAIL(query(read_param, iter))) {
    } else {
      if (param.is_full_insert()) {
        ObLobFullInsertHandler handler(param);
        if (OB_FAIL(handler.init(lob_ctx_.lob_meta_mngr_))) {
        } else if (OB_FAIL(handler.execute(iter, append_lob_len, ori_inrow_data))) {
        }
      } else {
        ObLobAppendHandler handler(param);
        if (OB_FAIL(handler.init(lob_ctx_.lob_meta_mngr_))) {
        } else if(OB_FAIL(handler.execute(iter, append_lob_len, ori_inrow_data))) {
        }
      }
    }
    // finish query, release resource
    if (OB_NOT_NULL(iter)) {
      iter->reset();
      OB_DELETE(ObLobQueryIter, "unused", iter);
    }
  }
  return ret;
}

int ObLobManager::query_outrow(ObLobAccessParam& param, ObLobQueryIter *&result)
{
  int ret = OB_SUCCESS;
  ObLobQueryIterHandler handler(param);
  if (OB_FAIL(handler.init(lob_ctx_.lob_meta_mngr_))) {
  } else  if (OB_FAIL(handler.execute())) {
  } else {
    result = handler.result_;
  }
  return ret;
}

int ObLobManager::query_outrow(ObLobAccessParam& param, ObString &buffer)
{
  int ret = OB_SUCCESS;
  ObLobQueryDataHandler handler(param, buffer);
  if (OB_FAIL(handler.init(lob_ctx_.lob_meta_mngr_))) {
  } else  if (OB_FAIL(handler.execute())) {
  }
  return ret;
}

int ObLobManager::insert(ObLobAccessParam& param, const ObLobLocatorV2 &src_data_locator, ObArray<ObLobMetaInfo> &lob_meta_list)
{
  int ret = OB_SUCCESS;
  int64_t new_byte_len = 0;
  if (OB_FAIL(src_data_locator.get_lob_data_byte_len(new_byte_len))) {
  } else if (OB_ISNULL(param.lob_common_)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("null lob common", K(ret), K(param));
  } else if (! param.lob_common_->is_init_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lob common not init", K(ret), KPC(param.lob_common_), K(param));  
  } else if (OB_ISNULL(param.lob_data_ = reinterpret_cast<ObLobData*>(param.lob_common_->buffer_))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lob_data_ is null", KR(ret), K(param));
  } else if (OB_FAIL(param.init_out_row_ctx(new_byte_len))) {
  } else {
    ObLobSimplePersistInsertIter insert_iter(&param, param.allocator_, lob_meta_list);
    if (OB_FAIL(insert_iter.init())) {
    } else if (OB_FAIL(lob_ctx_.lob_meta_mngr_->batch_insert(param, insert_iter))) {
    }
  }
  return ret;
}

int ObLobManager::prepare_insert_task(
    ObLobAccessParam& param,
    bool &is_outrow,
    ObLobDataInsertTask &task)
{
  int ret = OB_SUCCESS;
  // old inrow  | new inrow   --> alloc new locator and but no need lob id
  // old inrow  | new outrow  --> alloc new locator and need new lob id
  // old outrow | new inrow   --> alloc new locator, but no need lob id
  // old outrow | new outrow  --> keep locator

  const int64_t lob_inrow_threshold = param.get_inrow_threshold();
  int64_t new_byte_len = 0;
  if (OB_FAIL(task.src_data_locator_.get_lob_data_byte_len(new_byte_len))) {
  } else if (new_byte_len <= lob_inrow_threshold) {
    // skip if inrow store
  } else if (OB_FAIL(prepare_outrow_locator(param, task))) {
  } else {
    is_outrow = true;
  }
  return ret;
}

int ObLobManager::prepare_outrow_locator(ObLobAccessParam& param, ObLobDataInsertTask &task)
{
  int ret = OB_SUCCESS;
  const ObLobLocatorV2 &src_data_locator = task.src_data_locator_;
  ObLobDiskLocatorBuilder locator_builder;
  int64_t new_byte_len = 0;
  const int64_t lob_chunk_size = param.get_schema_chunk_size();
  if (OB_FAIL(src_data_locator.get_lob_data_byte_len(new_byte_len))) {
  } else if (OB_FAIL(locator_builder.init(*param.allocator_))) {
  } else if (OB_FAIL(prepare_lob_id(param, locator_builder))) {
  } else if (OB_FAIL(locator_builder.set_chunk_size(lob_chunk_size))) {
  } else if (OB_FAIL(locator_builder.set_byte_len(new_byte_len))) {
  } else if (OB_FAIL(prepare_char_len(param, locator_builder, task))) {
  } else if (OB_FAIL(prepare_seq_no(param, locator_builder, task))) {
  } else if (OB_FAIL(locator_builder.to_locator(task.cur_data_locator_))) {
  } else {
  }
  return ret;
}

int ObLobManager::prepare_char_len(ObLobAccessParam& param, ObLobDiskLocatorBuilder &locator_builder, ObLobDataInsertTask &task)
{
  int ret = OB_SUCCESS;
  const ObLobLocatorV2 &src_data_locator = task.src_data_locator_;
  int64_t new_byte_len = 0;
  uint64_t char_len = 0;
  const int64_t lob_chunk_size = param.get_schema_chunk_size();
  if (OB_FAIL(src_data_locator.get_lob_data_byte_len(new_byte_len))) {
  } else if (OB_FAIL(is_store_char_len(param, lob_chunk_size, new_byte_len))) {
  } else if (! param.is_store_char_len_) {
    char_len = UINT64_MAX;
  } else if (param.is_blob()) {
    // blob char_len is equal byte_len
    char_len = new_byte_len;
  } else {
    ObString inrow_data;
    ObInRowLobDataSpliter spilter(task.lob_meta_list_);
    if (! src_data_locator.has_inrow_data()) {
      if (OB_FAIL(ObLobDiskLocatorWrapper::get_char_len(src_data_locator, char_len))) {
      }
    } else if (OB_FAIL(src_data_locator.get_inrow_data(inrow_data))) {
    } else if (inrow_data.length() != new_byte_len) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("byte len is not match", K(ret), K(new_byte_len), "inrow_data_length", inrow_data.length());
    } else if (OB_FAIL(spilter.split(param.coll_type_, param.get_schema_chunk_size(), inrow_data))) {
    } else {
      char_len = spilter.char_pos();
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(locator_builder.set_char_len(char_len))) {
  }
  return ret;
}

int ObLobManager::prepare_lob_id(ObLobAccessParam& param, ObLobDiskLocatorBuilder &locator_builder)
{
  int ret = OB_SUCCESS;
  ObLobId lob_id;
  if (OB_ISNULL(param.lob_common_)) {
    if (OB_FAIL(alloc_lob_id(param, lob_id))) {
    }
  } else {
    const ObLobCommon *lob_common = param.lob_common_;
    if (lob_common->in_row_ || ! lob_common->is_init_) {
      if (OB_FAIL(alloc_lob_id(param, lob_id))) {
      }
    } else {
      const ObLobData *lob_data = reinterpret_cast<const ObLobData*>(lob_common->buffer_);
      lob_id = lob_data->id_;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (! lob_id.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lob id is invalid", K(ret), K(lob_id), K(param));
  } else if (OB_FAIL(locator_builder.set_lob_id(lob_id))) {
  }
  return ret;
}

int ObLobManager::alloc_lob_id(ObLobAccessParam& param, ObLobId &lob_id)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(lob_ctx_.lob_meta_mngr_->fetch_lob_id(param, lob_id.lob_id_))) {
  } else if (! param.lob_meta_tablet_id_.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("lob_meta_tablet_id is invalid", K(ret), K(param));
  } else {
    lob_id.tablet_id_ = param.lob_meta_tablet_id_.id();
    // used for lob order
    transform_lob_id(lob_id.lob_id_, lob_id.lob_id_);
  }
  return ret;
}


int ObLobManager::prepare_seq_no(ObLobAccessParam& param, ObLobDiskLocatorBuilder &locator_builder, ObLobDataInsertTask &task)
{
  int ret = OB_SUCCESS;
  ObLobDataOutRowCtx::OpType type = ObLobDataOutRowCtx::OpType::EXT_INFO_LOG;
  int64_t seq_no_cnt = 1;
  transaction::ObTxSEQ seq_no_st;
  int64_t new_byte_len = 0;
  const int64_t lob_chunk_size = param.get_schema_chunk_size();
  if (OB_FAIL(task.src_data_locator_.get_lob_data_byte_len(new_byte_len))) {
  } else if (new_byte_len < lob_chunk_size && (OB_ISNULL(param.lob_common_) || param.lob_common_->in_row_)) {
    // means insert, not update
    type = ObLobDataOutRowCtx::OpType::SQL;
  } else if (OB_FAIL(locator_builder.set_ext_info_log_length(ObLobManager::LOB_OUTROW_FULL_SIZE + 1 /*ext info log type*/))) {
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(param.tx_desc_->get_and_inc_tx_seq(param.parent_seq_no_.get_branch(), seq_no_cnt, seq_no_st))) {
  } else if (OB_FAIL(locator_builder.set_seq_no(type, seq_no_st, seq_no_cnt))) {
  }
  return ret;
}

} // storage
} // oceanbase

// ===== lob-read domain port implementation (ObLobManager : common::ObILobReadService) =====
// ObLobAccessParam construction plus query/getlength calls are all locked inside this storage implementation。
namespace oceanbase
{
namespace storage
{

static int init_lob_access_param(storage::ObLobManager &lob_mngr,
                                 storage::ObLobAccessParam &param,
                                 common::ObLobTextIterCtx *lob_iter_ctx,
                                 common::ObCollationType cs_type,
                                 common::ObIAllocator *allocator = nullptr)
{
  int ret = OB_SUCCESS;
  int64_t timeout_ts = 0;

  if (OB_ISNULL(lob_iter_ctx)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: invalid lob iter ctx.", K(ret));
  } else if (OB_ISNULL(allocator = (allocator == nullptr ? lob_iter_ctx->alloc_: allocator))) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: allocator is null", K(ret), KP(allocator), KP(lob_iter_ctx->alloc_));
  } else if (!lob_iter_ctx->locator_.is_persist_lob()) {
    ret = OB_NOT_IMPLEMENT;
    COMMON_LOG(WARN, "Lob: outrow temp lob is not supported", K(ret), K(lob_iter_ctx->locator_));
  } else if (lob_iter_ctx->locator_.is_delta_temp_lob()) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: is delta lob", K(ret), K(lob_iter_ctx->locator_));
  // worker timeout_ts is not guaranteed to be always valid
  // so take the greater value of both
  } else if (lob_iter_ctx->timeout_ts_ <= 0) {
    timeout_ts = OB_MAX(ObTimeUtility::current_time() + 60 * USECS_PER_SEC, THIS_WORKER.get_timeout_ts());
  } else {
    timeout_ts = OB_MAX(lob_iter_ctx->timeout_ts_, THIS_WORKER.get_timeout_ts());
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(lob_mngr.build_lob_param(param, *allocator, cs_type,
                  0, UINT64_MAX, timeout_ts, lob_iter_ctx->locator_))) {
  } else if (! param.snapshot_.tx_id().is_valid()) {
    // if tx_id is valid, means read may be in a tx
    // lob can not set read_latest flag
    // so reuse lob aux table iterator only if tx_id is invalid
    // for exmaple
    //   insert into t values (1,'v0');
    //   insert ignore into t values (1,'v11'), (1,'v222') on duplicate key update c1 = md5(c1);
    // second read shoud get "v11" not "v0"
    param.access_ctx_ = static_cast<ObLobAccessCtx *>(lob_iter_ctx->access_context_);
  }

  return ret;
}


int ObLobManager::get_outrow_lob_full_data(common::ObLobTextIterCtx &ctx,
                                           common::ObCollationType cs_type,
                                           bool has_lob_header,
                                           bool is_outrow,
                                           common::ObIAllocator *tmp_alloc)
{
  int ret = OB_SUCCESS;
  if (!has_lob_header || !is_outrow || OB_ISNULL(ctx.alloc_)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: error condition", K(ret), K(has_lob_header), K(is_outrow), K(ctx.timeout_ts_));
  } else { // outrow persist lob
    storage::ObLobAccessParam param;
    if (OB_SUCC(init_lob_access_param(*this, param, &ctx, cs_type, tmp_alloc))) {
      param.len_ = (ctx.total_access_len_ == 0 ? param.byte_size_ : ctx.total_access_len_);

      if (!param.tablet_id_.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
        COMMON_LOG(WARN, "Lob: invalid param.", K(ret), K(param));
      } else if (param.byte_size_ == 0) {
        // empty lob
        ctx.content_byte_len_ = 0;
      } else if (param.byte_size_ < 0 || param.len_ == 0) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN,"Lob: calc byte size is negative.", K(ret), K(param));
      } else if (param.byte_size_ > OB_MAX_LONGTEXT_LENGTH) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN,"Lob: unable to read full data over 512M lob.", K(ret), K(param));
      } else {
        ctx.total_byte_len_ = param.byte_size_;
        ctx.buff_byte_len_ = static_cast<uint32_t>(param.byte_size_);//TODO(gehao.wh): check convert from 64 to 32
        ctx.buff_ = static_cast<char *>(ctx.alloc_->alloc(ctx.buff_byte_len_));
        ObString output_data;

        if (OB_ISNULL(ctx.buff_)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          COMMON_LOG(WARN,"Lob: failed to alloc output buffer",
              K(ret), KP(ctx.buff_), K(ctx.buff_byte_len_));
        } else {
          output_data.assign_buffer(ctx.buff_, ctx.buff_byte_len_);
          if (OB_FAIL(query(param, output_data))) {
          } else {
            ctx.content_byte_len_ = output_data.length();
            // Notice: content_len_ (char len) is not updated!
          }
        }
      }
    }
  }
  return ret;
}


int ObLobManager::get_delta_lob_full_data(common::ObLobTextIterCtx &ctx,
                                          common::ObObjType type,
                                          common::ObCollationType cs_type,
                                          common::ObLobLocatorV2 &lob_locator,
                                          common::ObIAllocator *allocator,
                                          common::ObString &data_str)
{
  int ret = OB_SUCCESS;
  ObLobCommon *lob_common = nullptr;
  ObLobDiffHeader *diff_header = nullptr;
  if (! ob_is_json(type)) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "only json support", K(ret), K(type));
  } else if (OB_FAIL(lob_locator.get_disk_locator(lob_common))) {
  } else if (! lob_common->in_row_) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "Unsupport out row delta tmp lob locator", K(ret), KPC(lob_common));
  } else if (OB_ISNULL(diff_header = reinterpret_cast<ObLobDiffHeader*>(lob_common->buffer_))) {
    ret = OB_ERR_UNEXPECTED;
    COMMON_LOG(WARN, "diff_header is null", K(ret), KPC(lob_common));
  } else {
    char *buf = diff_header->data_;
    int64_t data_len = diff_header->persist_loc_size_;
    int64_t pos = 0;
    ObLobPartialData partial_data;
    if (OB_FAIL(partial_data.init())) {
    } else if (OB_FAIL(partial_data.deserialize(buf, data_len, pos))) {
    } else {
      storage::ObLobAccessParam param;
      ctx.locator_ = partial_data.locator_;
      if (OB_FAIL(init_lob_access_param(*this, param, &ctx, cs_type, allocator))) {
      } else if (!param.tablet_id_.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
        COMMON_LOG(WARN, "Lob: invalid param.", K(ret), K(param));
      } else if ((param.len_ = param.byte_size_) <= 0) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN,"Lob: calc byte size is negative.", K(ret), K(param));
      } else if (param.byte_size_ > OB_MAX_LONGTEXT_LENGTH) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN,"Lob: unable to read full data over 512M lob.", K(ret), K(param));
      } else if (partial_data.data_length_ > OB_MAX_LONGTEXT_LENGTH) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN,"Lob: unable to read full data over 512M lob.", K(ret), K(param), K(partial_data));
      } else {
        ctx.total_byte_len_ = partial_data.data_length_;
        ctx.buff_byte_len_ = static_cast<uint32_t>(partial_data.data_length_);
        ctx.buff_ = static_cast<char *>(ctx.alloc_->alloc(ctx.buff_byte_len_));
        ObString output_data;
        if (OB_ISNULL(ctx.buff_)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          COMMON_LOG(WARN,"Lob: failed to alloc output buffer",
              K(ret), KP(ctx.buff_), K(ctx.buff_byte_len_));
        } else {
          output_data.assign_buffer(ctx.buff_, ctx.buff_byte_len_);
          if (OB_FAIL(query(param, output_data))) {
          } else {
            output_data.set_length(static_cast<int32_t>(partial_data.data_length_));
            for(int32_t i = 0; OB_SUCC(ret) && i < partial_data.index_.count(); ++i) {
              ObLobChunkIndex &idx =  partial_data.index_[i];
              if (1 == idx.is_modified_ || 1 == idx.is_add_) {
                ObLobChunkData &chunk_data = partial_data.data_[idx.data_idx_];
                MEMCPY(output_data.ptr() + idx.offset_, chunk_data.data_.ptr() + idx.pos_, idx.byte_len_);
              }
            }
            ctx.content_byte_len_ = output_data.length();
            data_str = output_data;
          }
        }
      }
    }
  }
  return ret;
}


int ObLobManager::get_outrow_prefix_data(common::ObLobTextIterCtx &ctx,
                                         common::ObCollationType cs_type,
                                         bool has_lob_header,
                                         bool is_outrow,
                                         common::ObIAllocator *tmp_alloc,
                                         uint32_t prefix_char_len)
{
  int ret = OB_SUCCESS;
  if (!has_lob_header || !is_outrow || OB_ISNULL(ctx.alloc_)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: error condition", K(ret), K(has_lob_header), K(is_outrow), K(ctx.timeout_ts_));
  } else { // outrow persist lob
    storage::ObLobAccessParam param;
    if (OB_SUCC(init_lob_access_param(*this, param, &ctx, cs_type, tmp_alloc))) {
      param.len_ = prefix_char_len;

      if (!param.tablet_id_.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
        COMMON_LOG(WARN, "Lob: invalid param.", K(ret), K(param));
      } else if (param.byte_size_ < 0 || param.len_ == 0) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN,"Lob: calc byte size is negative.", K(ret), K(param));
      } else {
        ctx.total_byte_len_ = param.byte_size_;
        ctx.buff_byte_len_ = prefix_char_len * common::ObTextStringIter::MAX_CHAR_MULTIPLIER;
        ctx.buff_ = static_cast<char *>(ctx.alloc_->alloc(ctx.buff_byte_len_));
        ObString output_data;
        if (OB_ISNULL(ctx.buff_)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          COMMON_LOG(WARN,"Lob: failed to alloc output buffer",
              K(ret), KP(ctx.buff_), K(ctx.buff_byte_len_));
        } else {
          output_data.assign_buffer(ctx.buff_, ctx.buff_byte_len_);
          if (OB_FAIL(query(param, output_data))) {
          } else {
            ctx.content_byte_len_ = output_data.length();
            // Notice: content_len_ (char len) is not updated!
          }
        }
      }
    }
  }
  return ret;
}


int ObLobManager::get_first_block(common::ObLobTextIterCtx &ctx,
                                  common::ObCollationType cs_type,
                                  bool has_lob_header,
                                  bool is_outrow,
                                  common::ObIAllocator *tmp_alloc,
                                  common::ObString &str,
                                  common::ObTextStringIterState &state)
{
  int ret = OB_SUCCESS;
  if (!is_outrow || !has_lob_header) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: error condition", K(ret), K(is_outrow), K(has_lob_header));
  } else {
    storage::ObLobAccessParam param;
    if (OB_SUCC(init_lob_access_param(*this, param, &ctx, cs_type, tmp_alloc))) {
      param.scan_backward_ = ctx.is_backward_;
      param.offset_ = ctx.start_offset_;
      param.len_ = (ctx.total_access_len_ == 0 ? param.byte_size_ : ctx.total_access_len_);

      // update buffer len according to reserve length config
      ctx.total_byte_len_ = param.byte_size_;
      if (ctx.reserved_byte_len_ > 0 || ctx.reserved_len_ > 0) {
        int64_t max_reserved_byte = MAX(ctx.reserved_byte_len_, ctx.reserved_len_ * common::ObTextStringIter::MAX_CHAR_MULTIPLIER);
        if (ctx.buff_byte_len_ < max_reserved_byte) {
          COMMON_LOG(INFO,"Lob: buffer size changed due to configurations",
            K(ctx.buff_byte_len_), K(ctx.reserved_byte_len_),
            K(ctx.reserved_len_), K(max_reserved_byte));
          ctx.buff_byte_len_ = static_cast<uint32_t>(max_reserved_byte);
        }
      }

      if (!param.tablet_id_.is_valid()) {
        ret = OB_INVALID_ARGUMENT;
        COMMON_LOG(WARN, "Lob: invalid param.", K(ret), K(param));
      } else if (param.byte_size_ == 0) {
        state = common::TEXTSTRING_ITER_END;
      } else if (param.byte_size_ < 0 || param.len_ == 0) {
        ret = OB_ERR_UNEXPECTED;
        COMMON_LOG(WARN,"Lob: calc byte size is negative.", K(ret), K(param));
      } else {
        if (OB_ISNULL(ctx.buff_)) {
          ctx.buff_ = static_cast<char *>(ctx.alloc_->alloc(ctx.buff_byte_len_));
        }
        ObString output_data;
        ObLobQueryIter *query_iter = nullptr;
        output_data.assign_buffer(ctx.buff_, ctx.buff_byte_len_);

        // 1. start query iter, and query one time
        // 2. update access param
        if (OB_ISNULL(ctx.buff_)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          COMMON_LOG(WARN,"Lob: failed to alloc output buffer",
              K(ret), KP(ctx.buff_), K(ctx.buff_byte_len_));
        } else if (OB_FAIL(query(param, query_iter))) {
          // query() may return a partially constructed iterator on failure;
          // retain it in the protocol context so the normal cleanup path owns it.
          ctx.read_cursor_ = query_iter;
          COMMON_LOG(WARN,"Lob: falied to query lob iter.", K(ret), K(param));
        } else if (FALSE_IT(ctx.read_cursor_ = query_iter)) {
        } else if (OB_FAIL(ctx.read_cursor_->get_next_row(output_data))) {
        } else {
          ctx.content_byte_len_ = output_data.length();
          // ToDo: @gehao get char len directly from lob mngr ?
          ctx.content_len_ = static_cast<uint32_t>(ObCharset::strlen_char(cs_type,
                                                    output_data.ptr(),
                                                    static_cast<int64_t>(output_data.length())));
          ctx.last_accessed_byte_len_ = 0;
          ctx.last_accessed_len_ = 0;
          ctx.accessed_byte_len_ = ctx.content_byte_len_;
          ctx.accessed_len_ = ctx.content_len_;
          ctx.iter_count_++;
          str.assign_ptr(ctx.buff_, ctx.content_byte_len_);
          state = common::TEXTSTRING_ITER_NEXT;
        }
      }
    }
  }

  return ret;
}


int ObLobManager::get_next_block_inner(common::ObLobTextIterCtx &ctx,
                                       common::ObCollationType cs_type,
                                       bool has_lob_header,
                                       bool is_outrow,
                                       common::ObString &str,
                                       common::ObTextStringIterState &state)
{
  // reserve(memmove) is already done by share before the call;
  // this side fetches the next chunk through the query iter and updates ctx。
  int ret = OB_SUCCESS;
  if (!is_outrow || !has_lob_header || OB_ISNULL(ctx.read_cursor_)) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "Lob: error condition", K(ret), K(is_outrow), K(has_lob_header), KP(ctx.read_cursor_));
  } else {
    ObString output_data;
    if (!ctx.is_backward_) {
      output_data.assign_buffer(ctx.buff_ + ctx.reserved_byte_len_,
                                ctx.buff_byte_len_ - ctx.reserved_byte_len_);
    } else {
      output_data.assign_buffer(ctx.buff_,
                                ctx.buff_byte_len_ - ctx.reserved_byte_len_);
    }
    if (OB_FAIL(ctx.read_cursor_->get_next_row(output_data))) {
      if (ret == OB_ITER_END) {
        state = common::TEXTSTRING_ITER_END; // iter finished
        ObLobQueryIter *query_iter = static_cast<ObLobQueryIter *>(ctx.read_cursor_);
        query_iter->reset();
        OB_DELETE(ObLobQueryIter, "unused", query_iter);
        ctx.read_cursor_ = nullptr;
        ret = OB_SUCCESS;
      } else {
        COMMON_LOG(WARN,"Lob: falied to get first block.", K(ret));
      }
    } else {
      // if put backward, we should compact buffer remain and move reserved part closed to the reading value
      if (output_data.remain() > 0 && ctx.is_backward_ && ctx.reserved_byte_len_ > 0) {
        // from :[0, output_data.length_][output_data.length_, output_data.buffer_size_][reserved_part]
        // to   :[0, output_data.length_][reserved_part]
        MEMMOVE(output_data.ptr() + output_data.length(),
                ctx.buff_ + ctx.buff_byte_len_ - ctx.reserved_byte_len_,
                ctx.reserved_byte_len_);
      }
      ctx.content_byte_len_ = ctx.reserved_byte_len_ + output_data.length();
      // ToDo: @gehao get directly from lob mngr ?
      uint32 cur_out_len = static_cast<uint32_t>(ObCharset::strlen_char(cs_type,
                                                  output_data.ptr(),
                                                  static_cast<int64_t>(output_data.length())));
      ctx.content_len_ = ctx.reserved_len_ + cur_out_len;
      ctx.last_accessed_byte_len_ = ctx.accessed_byte_len_;
      ctx.last_accessed_len_ = ctx.accessed_len_;
      ctx.accessed_byte_len_ += output_data.length();
      ctx.accessed_len_ += cur_out_len;
      ctx.iter_count_++;
      str.assign_ptr(ctx.buff_, ctx.content_byte_len_);
    }
  }
  return ret;
}


int ObLobManager::get_outrow_char_len(common::ObLobTextIterCtx &ctx,
                                      common::ObCollationType cs_type,
                                      common::ObIAllocator *tmp_alloc,
                                      int64_t &char_length)
{
  int ret = OB_SUCCESS;
  storage::ObLobAccessParam param;
  if (OB_SUCC(init_lob_access_param(*this, param, &ctx, cs_type, tmp_alloc))) {
    uint64_t length = 0;
    if (!param.tablet_id_.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "Lob: invalid param.", K(ret), K(param));
    } else if (OB_FAIL(getlength(param, length))) {
    } else {
      char_length = static_cast<int64_t>(length);
    }
  }
  return ret;
}


void ObLobManager::free_lob_query_iter(common::ObLobTextIterCtx &ctx)
{
  if (OB_NOT_NULL(ctx.read_cursor_)) {
    ObLobQueryIter *query_iter = static_cast<ObLobQueryIter *>(ctx.read_cursor_);
    query_iter->reset();
    OB_DELETE(ObLobQueryIter, "unused", query_iter);
    ctx.read_cursor_ = nullptr;
  }
}


}  // namespace storage

namespace data_plane
{

class ObJsonLobHandle
{
public:
  explicit ObJsonLobHandle(storage::ObLobCursor *cursor)
    : cursor_(cursor)
  {}

  storage::ObLobCursor *cursor_;
};

namespace
{

int create_json_lob_handle(common::ObIAllocator &allocator,
                           storage::ObLobCursor *cursor,
                           ObJsonLobHandle *&handle)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(cursor)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("JSON LOB cursor is null", K(ret));
  } else if (OB_ISNULL(handle = OB_NEWx(ObJsonLobHandle, &allocator, cursor))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate JSON LOB handle", K(ret));
  }
  return ret;
}

storage::ObLobCursor *get_json_lob_cursor(
    const common::ObJsonBinUpdateCtx &update_context)
{
  return static_cast<storage::ObLobCursor *>(update_context.cursor_);
}

class ObJsonLobDeltaCodec : public common::ObDeltaLob
{
public:
  ObJsonLobDeltaCodec(common::ObIAllocator *allocator,
                      int64_t query_timeout_ts,
                      common::ObJsonBinUpdateCtx &update_context,
                      ObJsonLobHandle *&handle)
    : allocator_(allocator),
      query_timeout_ts_(query_timeout_ts),
      update_context_(update_context),
      handle_(handle)
  {}

  explicit ObJsonLobDeltaCodec(common::ObJsonBinUpdateCtx &update_context,
                               ObJsonLobHandle *&handle)
    : allocator_(nullptr),
      query_timeout_ts_(0),
      update_context_(update_context),
      handle_(handle)
  {}

  int64_t get_partial_data_serialize_size() const override
  {
    storage::ObLobPartialData *partial_data = get_partial_data();
    return OB_ISNULL(partial_data) ? 0 : partial_data->get_serialize_size();
  }

  int64_t get_lob_diff_serialize_size() const override
  {
    int64_t len = sizeof(common::ObLobDiff)
        * update_context_.binary_diffs_.count();
    common::ObJsonDiffHeader json_diff_header;
    json_diff_header.cnt_ = update_context_.json_diffs_.count();
    len += json_diff_header.get_serialize_size();
    for (int64_t i = 0; i < update_context_.json_diffs_.count(); ++i) {
      len += update_context_.json_diffs_[i].get_serialize_size();
    }
    return len;
  }

  uint32_t get_lob_diff_cnt() const override
  {
    return static_cast<uint32_t>(update_context_.binary_diffs_.count());
  }

  int serialize_partial_data(char *buf,
                             const int64_t buf_len,
                             int64_t &pos) const override
  {
    int ret = OB_SUCCESS;
    storage::ObLobPartialData *partial_data = get_partial_data();
    if (OB_ISNULL(partial_data)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("JSON LOB partial data is null", K(ret));
    } else if (OB_FAIL(partial_data->serialize(buf, buf_len, pos))) {
    }
    return ret;
  }

  int serialize_lob_diffs(char *buf,
                          const int64_t buf_len,
                          common::ObLobDiffHeader *diff_header) const override
  {
    int ret = OB_SUCCESS;
    char *diff_data_ptr = diff_header->get_inline_data_ptr();
    common::ObLobDiff *lob_diffs = diff_header->get_diff_ptr();
    int64_t data_len = buf_len - (diff_data_ptr - buf);
    int64_t data_pos = 0;

    for (int64_t i = 0; OB_SUCC(ret) && i < diff_header->diff_cnt_; ++i) {
      const common::ObJsonBinaryDiff &diff =
          update_context_.binary_diffs_[i];
      common::ObLobDiff *lob_diff =
          new (lob_diffs + i) common::ObLobDiff();
      lob_diff->type_ = common::ObLobDiff::DiffType::WRITE_DIFF;
      lob_diff->dst_offset_ = diff.dst_offset_;
      lob_diff->dst_len_ = diff.dst_len_;
    }

    common::ObJsonDiffHeader json_diff_header;
    json_diff_header.cnt_ = update_context_.json_diffs_.count();
    if (OB_FAIL(json_diff_header.serialize(
            diff_data_ptr, data_len, data_pos))) {
    }
    for (int64_t i = 0;
         OB_SUCC(ret) && i < update_context_.json_diffs_.count();
         ++i) {
      const common::ObJsonDiff &diff = update_context_.json_diffs_[i];
      if (OB_FAIL(diff.serialize(diff_data_ptr, data_len, data_pos))) {
      }
    }
    return ret;
  }

  int deserialize_partial_data(common::ObLobDiffHeader *diff_header) override
  {
    int ret = OB_SUCCESS;
    storage::ObLobPartialData *partial_data = nullptr;
    storage::ObLobCursor *cursor = nullptr;
    storage::ObLobManager *lob_manager = ::oceanbase::share::server_service<::oceanbase::storage::ObLobManager>();
    common::ObLobLocatorV2 locator;
    int64_t pos = 0;
    if (OB_ISNULL(allocator_) || OB_ISNULL(lob_manager)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("JSON LOB restore dependency is null",
               K(ret), KP(allocator_), KP(lob_manager));
    } else if (OB_ISNULL(partial_data =
                   OB_NEWx(storage::ObLobPartialData, allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate JSON LOB partial data", K(ret));
    } else if (OB_FAIL(partial_data->init())) {
    } else if (OB_FAIL(partial_data->deserialize(
                   diff_header->data_, diff_header->persist_loc_size_, pos))) {
    } else if (OB_FALSE_IT(locator.assign_buffer(
                   partial_data->locator_.ptr(),
                   partial_data->locator_.length()))) {
    } else if (OB_FAIL(lob_manager->query(allocator_,
                                         locator,
                                         query_timeout_ts_,
                                         false,
                                         partial_data,
                                         cursor))) {
    } else if (OB_FAIL(create_json_lob_handle(
                   *allocator_, cursor, handle_))) {
      cursor->~ObLobCursor();
      cursor = nullptr;
    }
    return ret;
  }

  int deserialize_lob_diffs(char *buf,
                            const int64_t buf_len,
                            common::ObLobDiffHeader *diff_header) override
  {
    int ret = OB_SUCCESS;
    common::ObLobDiff *lob_diffs = diff_header->get_diff_ptr();
    char *data_ptr = diff_header->get_inline_data_ptr();
    if (OB_ISNULL(lob_diffs) || OB_ISNULL(data_ptr)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid serialized JSON LOB diff", K(ret), KP(lob_diffs),
               KP(data_ptr));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < diff_header->diff_cnt_; ++i) {
        common::ObJsonBinaryDiff binary_diff;
        binary_diff.dst_offset_ = lob_diffs[i].dst_offset_;
        binary_diff.dst_len_ = lob_diffs[i].dst_len_;
        if (OB_FAIL(update_context_.binary_diffs_.push_back(binary_diff))) {
        }
      }

      const int64_t data_len = buf_len - (data_ptr - buf);
      int64_t data_pos = 0;
      common::ObJsonDiffHeader json_diff_header;
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(json_diff_header.deserialize(
                     data_ptr, data_len, data_pos))) {
      }
      for (int64_t i = 0;
           OB_SUCC(ret) && i < json_diff_header.cnt_;
           ++i) {
        common::ObJsonDiff json_diff;
        if (OB_FAIL(json_diff.deserialize(data_ptr, data_len, data_pos))) {
        } else if (OB_FAIL(update_context_.json_diffs_.push_back(json_diff))) {
        }
      }
    }
    return ret;
  }

private:
  storage::ObLobPartialData *get_partial_data() const
  {
    storage::ObLobCursor *cursor = OB_NOT_NULL(handle_)
        ? handle_->cursor_
        : get_json_lob_cursor(update_context_);
    return OB_ISNULL(cursor) ? nullptr : cursor->partial_data_;
  }

  common::ObIAllocator *allocator_;
  int64_t query_timeout_ts_;
  common::ObJsonBinUpdateCtx &update_context_;
  ObJsonLobHandle *&handle_;
};

} // namespace

int open_json_lob(common::ObIAllocator &allocator,
                  common::ObLobLocatorV2 &locator,
                  int64_t query_timeout_ts,
                  ObJsonLobHandle *&handle)
{
  int ret = OB_SUCCESS;
  storage::ObLobCursor *cursor = nullptr;
  storage::ObLobManager *lob_manager = ::oceanbase::share::server_service<::oceanbase::storage::ObLobManager>();
  if (OB_NOT_NULL(handle)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("JSON LOB handle is already initialized", K(ret));
  } else if (OB_ISNULL(lob_manager)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("LOB manager is null", K(ret));
  } else if (OB_FAIL(lob_manager->query(
                 &allocator, locator, query_timeout_ts,
                 false, nullptr, cursor))) {
  } else if (OB_FAIL(create_json_lob_handle(allocator, cursor, handle))) {
    cursor->~ObLobCursor();
    cursor = nullptr;
  }
  return ret;
}

int restore_json_lob_delta(common::ObIAllocator &allocator,
                           const common::ObLobLocatorV2 &delta_locator,
                           int64_t query_timeout_ts,
                           common::ObJsonBinUpdateCtx &update_context,
                           ObJsonLobHandle *&handle)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(handle)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("JSON LOB handle is already initialized", K(ret));
  } else {
    ObJsonLobDeltaCodec codec(
        &allocator, query_timeout_ts, update_context, handle);
    if (OB_FAIL(codec.deserialize(delta_locator))) {
    }
  }
  return ret;
}

void destroy_json_lob_handle(ObJsonLobHandle *&handle)
{
  if (OB_NOT_NULL(handle)) {
    if (OB_NOT_NULL(handle->cursor_)) {
      handle->cursor_->~ObLobCursor();
      handle->cursor_ = nullptr;
    }
    handle->~ObJsonLobHandle();
    handle = nullptr;
  }
}

int read_json_lob_root_type(ObJsonLobHandle &handle, uint8_t &root_type)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(handle.cursor_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("JSON LOB handle is not open", K(ret));
  } else if (OB_FAIL(handle.cursor_->read_i8(
                 0, reinterpret_cast<int8_t *>(&root_type)))) {
  }
  return ret;
}

int try_get_single_chunk_json_lob(ObJsonLobHandle &handle,
                                  bool &is_single_chunk,
                                  common::ObString &data)
{
  int ret = OB_SUCCESS;
  is_single_chunk = false;
  if (OB_ISNULL(handle.cursor_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("JSON LOB handle is not open", K(ret));
  } else if (FALSE_IT(
                 is_single_chunk =
                     handle.cursor_->has_one_chunk_with_all_data())) {
  } else if (is_single_chunk
             && OB_FAIL(handle.cursor_->get_one_chunk_with_all_data(data))) {
    LOG_WARN("failed to read single-chunk JSON LOB", K(ret));
  }
  return ret;
}

int bind_json_lob(ObJsonLobHandle &handle,
                  common::ObJsonBinUpdateCtx &update_context)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(handle.cursor_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("JSON LOB handle is not open", K(ret));
  } else if (OB_NOT_NULL(update_context.cursor_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("JSON update context already owns a cursor", K(ret));
  } else {
    update_context.set_lob_cursor(handle.cursor_);
    handle.cursor_ = nullptr;
  }
  return ret;
}

int validate_json_lob_delta(const common::ObJsonBinUpdateCtx &update_context)
{
  int ret = OB_SUCCESS;
  storage::ObLobCursor *cursor = get_json_lob_cursor(update_context);
  storage::ObLobPartialData *partial_data =
      OB_ISNULL(cursor) ? nullptr : cursor->partial_data_;
  if (OB_ISNULL(partial_data)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("JSON LOB partial data is null", K(ret));
  }
  for (int64_t i = 0;
       OB_SUCC(ret) && i < partial_data->index_.count();
       ++i) {
    storage::ObLobChunkIndex &chunk_index = partial_data->index_[i];
    const uint64_t chunk_start_offset = chunk_index.offset_;
    const uint64_t chunk_end_offset =
        chunk_index.offset_ + chunk_index.byte_len_;
    bool is_chunk_updated = false;
    for (int64_t j = 0;
         !chunk_index.is_add_ && j < update_context.binary_diffs_.count();
         ++j) {
      const common::ObJsonBinaryDiff &diff =
          update_context.binary_diffs_[j];
      const uint64_t diff_start_offset = diff.dst_offset_;
      const uint64_t diff_end_offset = diff.dst_offset_ + diff.dst_len_;
      if ((diff_start_offset >= chunk_start_offset
           && diff_start_offset < chunk_end_offset)
          || (diff_end_offset > chunk_start_offset
              && diff_end_offset <= chunk_end_offset)
          || (diff_start_offset <= chunk_start_offset
              && chunk_end_offset <= diff_end_offset)) {
        is_chunk_updated = true;
      }
    }
    if (is_chunk_updated
        && !chunk_index.is_add_
        && !(chunk_index.is_modified_ && chunk_index.old_data_idx_ >= 0)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("JSON LOB old chunk data was not recorded",
               K(ret), K(i), K(chunk_index));
    }
  }
  return ret;
}

int64_t get_json_lob_delta_serialize_size(
    const common::ObJsonBinUpdateCtx &update_context)
{
  common::ObJsonBinUpdateCtx &mutable_context =
      const_cast<common::ObJsonBinUpdateCtx &>(update_context);
  ObJsonLobHandle *handle = nullptr;
  ObJsonLobDeltaCodec codec(mutable_context, handle);
  return codec.get_serialize_size();
}

int serialize_json_lob_delta(const common::ObJsonBinUpdateCtx &update_context,
                             char *buf,
                             int64_t buf_len,
                             int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(update_context.cursor_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("JSON update context has no LOB cursor", K(ret));
  } else {
    common::ObJsonBinUpdateCtx &mutable_context =
        const_cast<common::ObJsonBinUpdateCtx &>(update_context);
    ObJsonLobHandle *handle = nullptr;
    ObJsonLobDeltaCodec codec(mutable_context, handle);
    if (OB_FAIL(codec.serialize(buf, buf_len, pos))) {
    }
  }
  return ret;
}

int lob_binary_equal(common::ObLobLocatorV2 &left,
                     common::ObLobLocatorV2 &right,
                     int64_t timeout_ts,
                     transaction::ObTxDesc *tx_desc,
                     bool &is_equal)
{
  int ret = OB_SUCCESS;
  storage::ObLobManager *manager = ::oceanbase::share::server_service<::oceanbase::storage::ObLobManager>();
  if (OB_ISNULL(manager)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("LOB manager is null", K(ret));
  } else {
    storage::ObLobCompareParams params;
    params.collation_left_ = common::CS_TYPE_BINARY;
    params.collation_right_ = common::CS_TYPE_BINARY;
    params.offset_left_ = 0;
    params.offset_right_ = 0;
    params.compare_len_ = UINT64_MAX;
    params.timeout_ = timeout_ts;
    params.tx_desc_ = tx_desc;
    if (OB_FAIL(manager->equal(left, right, params, is_equal))) {
    }
  }
  return ret;
}

int read_lob_to_buffer(common::ObIAllocator &allocator,
                       common::ObLobLocatorV2 &lob,
                       int64_t timeout_ts,
                       transaction::ObTxDesc *tx_desc,
                       common::ObString &buffer)
{
  int ret = OB_SUCCESS;
  storage::ObLobManager *manager = ::oceanbase::share::server_service<::oceanbase::storage::ObLobManager>();
  storage::ObLobAccessParam param;
  param.tx_desc_ = tx_desc;
  if (OB_ISNULL(manager)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("LOB manager is null", K(ret));
  } else if (OB_FAIL(manager->build_lob_param(
                 param, allocator, common::CS_TYPE_BINARY,
                 0, UINT64_MAX, timeout_ts, lob))) {
  } else if (OB_FAIL(manager->query(param, buffer))) {
  }
  return ret;
}

void set_zero_lob_value(common::ObObjType type, common::ObObj &value)
{
  value.set_lob_value(
      type,
      reinterpret_cast<const char *>(&storage::ObLobManager::ZERO_LOB),
      sizeof(common::ObLobCommon));
  value.set_has_lob_header();
}

int fill_lob_header(common::ObIAllocator &allocator,
                    common::ObString &data,
                    common::ObString &out)
{
  return storage::ObLobManager::fill_lob_header(allocator, data, out);
}

int fill_lob_header(common::ObIAllocator &allocator,
                    blocksstable::ObStorageDatum &datum)
{
  return storage::ObLobManager::fill_lob_header(allocator, datum);
}

int fill_lob_header(
    common::ObIAllocator &allocator,
    const common::ObIArray<share::schema::ObColDesc> &column_ids,
    blocksstable::ObDatumRow &datum_row)
{
  return storage::ObLobManager::fill_lob_header(
      allocator, column_ids, datum_row);
}

} // namespace data_plane
}  // namespace oceanbase
