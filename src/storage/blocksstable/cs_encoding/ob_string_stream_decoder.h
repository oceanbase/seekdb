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

#ifndef OCEANBASE_STRING_STREAM_DECODER_H_
#define OCEANBASE_STRING_STREAM_DECODER_H_

#include "ob_string_stream_encoder.h"
#include "ob_integer_stream_decoder.h"
#include "ob_column_encoding_struct.h"

namespace oceanbase
{
namespace blocksstable
{

typedef void (*ConvertStringToDatumFunc)(
    const ObBaseColumnDecoderCtx &base_col_ctx,
    const char *str_data,
    const ObStringStreamDecoderCtx &str_ctx,
    const char *offset_data,
    const char *ref_data,
    const int32_t *row_ids,
    const int64_t row_cap_or_id,
    common::ObDatum *datums);

extern ObMultiDimArray_T<ConvertStringToDatumFunc, 5/*offset_width_V*/, ObRefStoreWidthV::MAX_WIDTH_V,
    ObBaseColumnDecoderCtx::ObNullFlag::MAX, 2/*need_copy_V*/> convert_string_to_datum_funcs;

class ObStringStreamDecoder
{
public:
  static int build_decoder_ctx(const ObStreamData &str_data,
                               ObStringStreamDecoderCtx &ctx,
                               uint16_t &str_meta_size);
private:
  static int decode_stream_meta_(const ObStreamData &str_data,
                                 ObStringStreamDecoderCtx &ctx,
                                 uint16_t &str_meta_size);

};

} // end namespace blocksstable
} // end namespace oceanbase

#endif // OCEANBASE_STRING_STREAM_ENCODER_H_
