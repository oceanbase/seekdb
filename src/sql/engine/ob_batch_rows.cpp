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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/ob_batch_rows.h"

namespace oceanbase
{
namespace sql
{

DEF_TO_STRING(ObBatchRows)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(size), K_(end), KP(skip_),
       "skip_bit_vec", ObLogPrintHex(reinterpret_cast<char *>(skip_),
                                     NULL == skip_ ? 0 : ObBitVector::memory_size(size_)));
  J_OBJ_END();
  return pos;
}

} // end namespace sql
} // end namespace oceanbase
