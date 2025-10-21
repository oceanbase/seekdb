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

#ifndef OCEANBASE_ENCODING_OB_INTEGER_COLUMN_DECODER_H_
#define OCEANBASE_ENCODING_OB_INTEGER_COLUMN_DECODER_H_

#include "ob_icolumn_cs_decoder.h"

namespace oceanbase
{
namespace blocksstable
{

class ObIntegerColumnDecoder : public ObIColumnCSDecoder
{
public:
  static const ObCSColumnHeader::Type type_ = ObCSColumnHeader::INTEGER;
  ObIntegerColumnDecoder() {}
  virtual ~ObIntegerColumnDecoder() {}
  ObIntegerColumnDecoder(const ObIntegerColumnDecoder&) = delete;
  ObIntegerColumnDecoder &operator=(const ObIntegerColumnDecoder&) = delete;

  virtual int decode(const ObColumnCSDecoderCtx &ctx,
      const int32_t row_id, common::ObDatum &datum) const override;
  virtual int batch_decode(const ObColumnCSDecoderCtx &ctx, const int32_t *row_ids,
      const int64_t row_cap, common::ObDatum *datums) const override;
  virtual int decode_vector(const ObColumnCSDecoderCtx &ctx, ObVectorDecodeCtx &vector_ctx) const override;

  virtual int get_null_count(const ObColumnCSDecoderCtx &ctx,
     const int32_t *row_ids, const int64_t row_cap, int64_t &null_count) const override;

  virtual ObCSColumnHeader::Type get_type() const override { return type_; }

  virtual int pushdown_operator(
      const sql::ObPushdownFilterExecutor *parent,
      const ObColumnCSDecoderCtx &col_ctx,
      const sql::ObWhiteFilterExecutor &filter,
      const sql::PushdownFilterInfo &pd_filter_info,
      common::ObBitmap &result_bitmap) const override;

  virtual int get_aggregate_result(
      const ObColumnCSDecoderCtx &ctx,
      const ObPushdownRowIdCtx &pd_row_id_ctx,
      storage::ObAggCellBase &agg_cell) const override;

private:
  static int nu_nn_operator(const ObIntegerColumnDecoderCtx &ctx,
                            const sql::ObPushdownFilterExecutor *parent,
                            const sql::ObWhiteFilterExecutor &filter,
                            const sql::PushdownFilterInfo &pd_filter_info,
                            common::ObBitmap &result_bitmap);

  static int comparison_operator(const ObIntegerColumnDecoderCtx &ctx,
                                 const sql::ObPushdownFilterExecutor *parent,
                                 const sql::ObWhiteFilterExecutor &filter,
                                 const sql::PushdownFilterInfo &pd_filter_info,
                                 common::ObBitmap &result_bitmap);

  static int tranverse_integer_comparison_op(const ObIntegerColumnDecoderCtx &ctx,
                                             const sql::ObWhiteFilterOperatorType &ori_op_type,
                                             const bool is_col_signed,
                                             const uint64_t filter_val,
                                             const int64_t filter_val_size,
                                             const bool is_filter_signed,
                                             const int64_t row_start,
                                             const int64_t row_cnt,
                                             const sql::ObPushdownFilterExecutor *parent,
                                             common::ObBitmap &result_bitmap);

  static int between_operator(const ObIntegerColumnDecoderCtx &ctx,
                              const sql::ObPushdownFilterExecutor *parent,
                              const sql::ObWhiteFilterExecutor &filter,
                              const sql::PushdownFilterInfo &pd_filter_info,
                              common::ObBitmap &result_bitmap);
  static int tranverse_integer_between_op(const ObIntegerColumnDecoderCtx &ctx,
                                          const sql::ObPushdownFilterExecutor *parent,
                                          const sql::ObWhiteFilterExecutor &filter,
                                          const sql::PushdownFilterInfo &pd_filter_info,
                                          ObBitmap &result_bitmap);

  static int in_operator(const ObIntegerColumnDecoderCtx &ctx,
                         const sql::ObPushdownFilterExecutor *parent,
                         const sql::ObWhiteFilterExecutor &filter,
                         const sql::PushdownFilterInfo &pd_filter_info,
                         common::ObBitmap &result_bitmap);

  static int tranverse_integer_in_op(const ObIntegerColumnDecoderCtx &ctx,
                                     bool *filter_vals_valid,
                                     uint64_t *filter_vals,
                                     const sql::ObPushdownFilterExecutor *parent,
                                     const sql::ObWhiteFilterExecutor &filter,
                                     const sql::PushdownFilterInfo &pd_filter_info,
                                     common::ObBitmap &result_bitmap);

  template<typename Operator>
  static int tranverse_datum_all_op(const ObIntegerColumnDecoderCtx &ctx,
                                    const sql::PushdownFilterInfo &pd_filter_info,
                                    common::ObBitmap &result_bitmap,
                                    Operator const &eval);

  static int traverse_integer_in_agg(const ObIntegerColumnDecoderCtx &ctx,
                                     const bool is_col_signed,
                                     const int64_t row_start,
                                     const int64_t row_count,
                                     storage::ObAggCellBase &agg_cell);

};

}  // end namespace blocksstable
}  // end namespace oceanbase

#endif  // OCEANBASE_ENCODING_OB_INTEGER_COLUMN_DECODER_H_
