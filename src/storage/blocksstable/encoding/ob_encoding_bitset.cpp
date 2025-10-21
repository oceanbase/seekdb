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

#include "ob_encoding_bitset.h"

namespace oceanbase
{
namespace blocksstable
{
using namespace common;

int ObBitMapMetaBaseWriter::init(
    const common::ObIArray<int64_t> *exc_row_ids,
    const ObColDatums *col_datums,
    const common::ObObjMeta type)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = common::OB_INIT_TWICE;
    STORAGE_LOG(WARN, "init twice", K(ret));
  } else if (OB_ISNULL(exc_row_ids) || OB_ISNULL(col_datums) || OB_UNLIKELY(exc_row_ids->count() <= 0)) {
    ret = common::OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid argument", K(ret), KP(exc_row_ids), KP(col_datums));
  } else {
    exc_row_ids_ = exc_row_ids;
    col_datums_ = col_datums;
    type_ = type;
    is_inited_ = true;
  }
  return ret;
}

int64_t ObBitMapMetaBaseWriter::size() const
{
  return sizeof(ObBitMapMetaHeader) + meta_.data_offset_ + exc_total_size_;
}

} // end namespace blocksstable
} // end namespace oceanbase

