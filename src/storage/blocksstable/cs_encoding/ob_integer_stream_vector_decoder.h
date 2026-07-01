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

#ifndef OCEANBASE_CS_ENCODING_OB_INTEGER_STREAM_VECTOR_DECODER_H_
#define OCEANBASE_CS_ENCODING_OB_INTEGER_STREAM_VECTOR_DECODER_H_

#include "ob_stream_encoding_struct.h"
#include "ob_column_encoding_struct.h"
#include "src/share/vector/ob_uniform_vector.h"
#include "src/share/vector/ob_continuous_vector.h"
#include "src/share/vector/ob_discrete_vector.h"
#include "src/share/vector/ob_fixed_length_vector.h"

namespace oceanbase
{
namespace blocksstable
{

class ObVectorDecodeCtx;
class ObIntegerStreamVecDecoder final
{
public:
  static int decode_vector(
      const ObBaseColumnDecoderCtx &base_col_ctx,
      const char *data,
      const ObIntegerStreamDecoderCtx &ctx,
      const char *ref_data,
      const ObVecDecodeRefWidth ref_width,
      ObVectorDecodeCtx &vector_ctx);

private:
  template<typename VectorType>
  static int decode_vector_(
      const ObBaseColumnDecoderCtx &base_col_ctx,
      const char *data,
      const ObIntegerStreamDecoderCtx &ctx,
      const char *ref_data,
      const ObVecDecodeRefWidth ref_width,
      ObVectorDecodeCtx &vector_ctx);

};

} // namesapce blocksstable
} // namespace oceanbase

#endif // OCEANBASE_CS_ENCODING_OB_INTEGER_STREAM_VECTOR_DECODER_H_
