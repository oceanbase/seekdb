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

#include "ob_inter_column_substring_decoder.h"

namespace oceanbase
{
namespace blocksstable
{
using namespace common;
const ObColumnHeader::Type ObInterColSubStrDecoder::type_;
ObInterColSubStrDecoder::ObInterColSubStrDecoder()
    : meta_header_(NULL)
{
}

ObInterColSubStrDecoder::~ObInterColSubStrDecoder()
{
}

int ObInterColSubStrDecoder::decode(const ObColumnDecoderCtx &ctx, common::ObDatum &datum, const int64_t row_id,
    const ObBitStream &bs, const char *data, const int64_t len) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited())) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(data) || OB_UNLIKELY(len < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(data), K(len));
  } else {
    int64_t ref = 0;
    if (!has_exc(ctx)) {
      ref = -1;
    } else {
      if (OB_FAIL(ObBitMapMetaReader<ObStringSC>::read(
          meta_header_->payload_,
          ctx.micro_block_header_->row_count_,
          ctx.is_bit_packing(), row_id,
          ctx.col_header_->length_ - sizeof(ObInterColSubStrMetaHeader),
          ref, datum, ctx.col_header_->get_store_obj_type()))) {
      }
    }

    // not an exception data
    if (OB_SUCC(ret) && -1 == ref) {
      ObDatum ref_datum;
      if (OB_FAIL(ctx.ref_decoder_->decode(*ctx.ref_ctx_, ref_datum, row_id, bs, data, len))) {
      } else if (ref_datum.is_null()) {
        datum.set_null();
      } else if (ref_datum.is_nop()) {
        datum.set_ext();
        datum.no_cv(datum.extend_obj_)->set_ext(common::ObActionFlag::OP_NOP);
      } else {
        const char *cell_data =
            reinterpret_cast<const char *>(meta_header_) + ctx.col_header_->length_
            + row_id * (meta_header_->start_pos_byte_ + meta_header_->val_len_byte_);
        int64_t start_pos = 0;
        if (!meta_header_->is_same_start_pos()) {
          MEMCPY(&start_pos, cell_data, meta_header_->start_pos_byte_);
        } else {
          start_pos = meta_header_->start_pos_;
        }
        int64_t val_len = 0;
        if (!meta_header_->is_fix_length()) {
          MEMCPY(&val_len, cell_data + meta_header_->start_pos_byte_, meta_header_->val_len_byte_);
        } else {
          val_len = meta_header_->length_;
        }

        datum.pack_ =  static_cast<int32_t>(val_len);
        datum.ptr_ = ref_datum.ptr_ + start_pos;
      }
    }
  }
  return ret;
}

int ObInterColSubStrDecoder::update_pointer(const char *old_block, const char *cur_block)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(old_block) || OB_ISNULL(cur_block)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(old_block), KP(cur_block));
  } else {
    ObIColumnDecoder::update_pointer(meta_header_, old_block, cur_block);
    //ObIColumnDecoder::update_pointer(meta_data_, old_block, cur_block);
  }
  return ret;
}

int ObInterColSubStrDecoder::get_ref_col_idx(int64_t &ref_col_idx) const
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ref_col_idx = meta_header_->ref_col_idx_;
  }
  return ret;
}

} // end namespace blocksstable
} // end namespace oceanbase
