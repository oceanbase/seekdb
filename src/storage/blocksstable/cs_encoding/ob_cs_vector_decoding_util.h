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

#ifndef OCEANBASE_CS_ENCODING_OB_CS_VECTOR_DECODING_UTIL_H_
#define OCEANBASE_CS_ENCODING_OB_CS_VECTOR_DECODING_UTIL_H_

#include "src/share/vector/ob_uniform_vector.h"
#include "src/share/vector/ob_continuous_vector.h"
#include "src/share/vector/ob_discrete_vector.h"
#include "src/share/vector/ob_fixed_length_vector.h"

namespace oceanbase
{
namespace blocksstable
{
class ObCSVectorDecodingUtil final
{
public:
  static int decode_all_null_vector(
      const int32_t *row_ids,
      const int64_t row_cap,
      sql::VectorHeader &vec_header,
      const int64_t vec_offset);
};

}
}

#endif
