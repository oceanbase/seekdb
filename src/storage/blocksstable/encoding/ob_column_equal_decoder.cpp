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

#include "ob_column_equal_decoder.h"

namespace oceanbase
{
using namespace common;
namespace blocksstable
{
const ObColumnHeader::Type ObColumnEqualDecoder::type_;

ObColumnEqualDecoder::ObColumnEqualDecoder()
  : inited_(false), meta_header_(NULL)
{
}

ObColumnEqualDecoder::~ObColumnEqualDecoder()
{
}

int ObColumnEqualDecoder::decode(const ObColumnDecoderCtx &ctx, ObDatum &datum, const int64_t row_id,
    const ObBitStream &bs, const char *data, const int64_t len) const
{
  UNUSED(bs);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(row_id < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(row_id));
  } else {
    int64_t ref = 0;
    if (!has_exc(ctx)) {
      ref = -1;
    } else {
      const ObObjType store_type = ctx.col_header_->get_store_obj_type();
      const ObObjTypeClass tc = ob_obj_type_class(store_type);
      switch (get_store_class_map()[tc]) {
        case ObUIntSC:
        case ObIntSC: {
          if (OB_FAIL(ObBitMapMetaReader<ObUIntSC>::read(
              meta_header_->payload_, ctx.micro_block_header_->row_count_,
              ctx.is_bit_packing(), row_id,
              ctx.col_header_->length_ - sizeof(ObColumnEqualMetaHeader),
              ref, datum, store_type))) {
          }
          break;
        }
        case ObNumberSC: {
          if (OB_FAIL(ObBitMapMetaReader<ObNumberSC>::read(
              meta_header_->payload_, ctx.micro_block_header_->row_count_,
              ctx.is_bit_packing(), row_id,
              ctx.col_header_->length_ - sizeof(ObColumnEqualMetaHeader),
              ref, datum, store_type))) {
          }
          break;
        }
        case ObDecimalIntSC: {
          if (OB_FAIL(ObBitMapMetaReader<ObDecimalIntSC>::read(
              meta_header_->payload_, ctx.micro_block_header_->row_count_,
              ctx.is_bit_packing(), row_id,
              ctx.col_header_->length_ - sizeof(ObColumnEqualMetaHeader),
              ref, datum, store_type))) {
          }
          break;
        }
        case ObStringSC:
        case ObTextSC:
        case ObJsonSC:
        case ObGeometrySC: {
          if (OB_FAIL(ObBitMapMetaReader<ObStringSC>::read(
              meta_header_->payload_, ctx.micro_block_header_->row_count_,
              ctx.is_bit_packing(), row_id,
              ctx.col_header_->length_ - sizeof(ObColumnEqualMetaHeader),
              ref, datum, store_type))) {
          }
          break;
        }
        case ObOTimestampSC: {
          if (OB_FAIL(ObBitMapMetaReader<ObOTimestampSC>::read(
              meta_header_->payload_, ctx.micro_block_header_->row_count_,
              ctx.is_bit_packing(), row_id,
              ctx.col_header_->length_ - sizeof(ObColumnEqualMetaHeader),
              ref, datum, store_type))) {
          }
          break;
        }
        case ObIntervalSC: {
          if (OB_FAIL(ObBitMapMetaReader<ObIntervalSC>::read(
              meta_header_->payload_, ctx.micro_block_header_->row_count_,
              ctx.is_bit_packing(), row_id,
              ctx.col_header_->length_ - sizeof(ObColumnEqualMetaHeader),
              ref, datum, store_type))) {
          }
          break;
        }
        default:
          ret = OB_INNER_STAT_ERROR;
          LOG_WARN("not supported store class", K(ret), K(ctx));
      }
    }

    // not an exception, get from reffed column
    if (OB_SUCC(ret) && -1 == ref) {
      if (OB_FAIL(ctx.ref_decoder_->decode(*ctx.ref_ctx_, datum, row_id, bs, data, len))) {
      }
    }
  }
  return ret;
}

int ObColumnEqualDecoder::update_pointer(const char *old_block, const char *cur_block)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(old_block) || OB_ISNULL(cur_block)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(old_block), KP(cur_block));
  } else {
    ObIColumnDecoder::update_pointer(meta_header_, old_block, cur_block);
  }
  return ret;
}

int ObColumnEqualDecoder::get_ref_col_idx(int64_t &ref_col_idx) const
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ref_col_idx = meta_header_->ref_col_idx_;
  }
  return ret;
}

}//end namespace blocksstable
}//end namespace oceanbase
