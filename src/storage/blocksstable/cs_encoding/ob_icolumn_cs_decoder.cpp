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

#include "ob_icolumn_cs_decoder.h"
#include "ob_cs_encoding_util.h"
#include "storage/access/ob_aggregate_base.h"

namespace oceanbase
{
namespace blocksstable
{
using namespace common;

int ObIColumnCSDecoder::get_null_count(
    const ObColumnCSDecoderCtx &ctx,
    const int32_t *row_ids,
    const int64_t row_cap,
    int64_t &null_count) const
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

} // end of namespace oceanbase
} // end of namespace oceanbase
