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

#ifndef OCEANBASE_ENCODING_OB_SEMISTRUCT_COLUMN_DECODER_H_
#define OCEANBASE_ENCODING_OB_SEMISTRUCT_COLUMN_DECODER_H_

#include "storage/blocksstable/cs_encoding/ob_icolumn_cs_decoder.h"

namespace oceanbase
{
namespace blocksstable
{
class ObSemiStructColumnDecoder : public ObIColumnCSDecoder
{
public:
  static const ObCSColumnHeader::Type type_ = ObCSColumnHeader::SEMISTRUCT;
  ObSemiStructColumnDecoder() {}
  virtual ~ObSemiStructColumnDecoder() {}

  ObSemiStructColumnDecoder(const ObSemiStructColumnDecoder&) = delete;
  ObSemiStructColumnDecoder &operator=(const ObSemiStructColumnDecoder&) = delete;

  virtual int decode(const ObColumnCSDecoderCtx &ctx,
    const int32_t row_id, common::ObDatum &datum) const override;
  virtual int batch_decode(const ObColumnCSDecoderCtx &ctx, const int32_t *row_ids,
      const int64_t row_cap, common::ObDatum *datums) const override;
  virtual int decode_vector(const ObColumnCSDecoderCtx &ctx, ObVectorDecodeCtx &vector_ctx) const override;

  virtual int get_null_count(const ObColumnCSDecoderCtx &ctx,
     const int32_t *row_ids, const int64_t row_cap, int64_t &null_count) const override;

  virtual int pushdown_operator(
      const sql::ObPushdownFilterExecutor *parent,
      const ObColumnCSDecoderCtx &col_ctx,
      const sql::ObWhiteFilterExecutor &filter,
      const sql::PushdownFilterInfo &pd_filter_info,
      common::ObBitmap &result_bitmap) const override;

  virtual ObCSColumnHeader::Type get_type() const override { return type_; }

};

}  // end namespace blocksstable
}  // end namespace oceanbase

#endif  // OCEANBASE_ENCODING_OB_SEMISTRUCT_COLUMN_DECODER_H_
