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

#include "storage/blocksstable/ob_batch_datum_rows.h"

namespace oceanbase
{
namespace blocksstable
{

void ObBatchDatumRows::reset()
{
  row_flag_.reset();
  mvcc_row_flag_.reset();
  trans_id_.reset();
  vectors_.reset();
  row_count_ = 0;
}

int ObBatchDatumRows::to_datum_row(int64_t idx, ObDatumRow &datum_row) const {
  int ret = OB_SUCCESS;

  if ((idx < 0 || idx >= row_count_) || datum_row.count_ != vectors_.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(idx), K(datum_row.count_), K(vectors_.count()), KR(ret));
  }

  if (OB_SUCC(ret)) {
    datum_row.row_flag_ = row_flag_;
    datum_row.mvcc_row_flag_ = mvcc_row_flag_;
    datum_row.trans_id_ = trans_id_;
  }

  const char *pay_load = nullptr;
  bool is_null = false;
  ObLength length = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < vectors_.count(); i ++) {
    common::ObIVector *vec = vectors_.at(i);
    if (vec == nullptr) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("vec should not be null", KR(ret));
    } else {
      vec->get_payload(idx, is_null, pay_load, length);
      if (is_null) {
        datum_row.storage_datums_[i].set_null();
      } else {
        datum_row.storage_datums_[i].shallow_copy_from_datum(ObDatum(pay_load, length, is_null));
      }
    }
  }

  return ret;
}

} // namespace blocksstable
} // namespace oceanbase
