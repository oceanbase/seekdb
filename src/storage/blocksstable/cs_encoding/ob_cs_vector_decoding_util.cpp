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

#include "ob_cs_vector_decoding_util.h"

namespace oceanbase
{
namespace blocksstable
{

int ObCSVectorDecodingUtil::decode_all_null_vector(
    const int32_t *row_ids,
    const int64_t row_cap,
    sql::VectorHeader &vec_header,
    const int64_t vec_offset)
{
  int ret = OB_SUCCESS;
  VectorFormat vec_format = vec_header.get_format();
  ObIVector *vector = vec_header.get_vector();
  switch (vec_format) {
    case VEC_FIXED:
    case VEC_DISCRETE:
    case VEC_CONTINUOUS: {
      ObBitmapNullVectorBase *null_vec_base = static_cast<ObBitmapNullVectorBase*>(vector);
      for (int64_t i = 0; i < row_cap; i++) {
        null_vec_base->set_null(vec_offset + i);
      }
      break;
    }
    case VEC_UNIFORM: {
      ObUniformFormat<false> *uni_vec = static_cast<ObUniformFormat<false> *>(vector);
      for (int64_t i = 0; i < row_cap; i++) {
        uni_vec->set_null(vec_offset + i);
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected vector format", K(ret), K(vec_format));
      break;
    }
  }
  return ret;
}

}
}
