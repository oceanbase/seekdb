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

#include "ob_row_sample_iterator.h"

namespace oceanbase
{
using namespace common;
namespace storage
{
int ObMemtableRowSampleIterator::get_next_row(blocksstable::ObDatumRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(iterator_)) {
    ret = OB_NOT_INIT;
    STORAGE_LOG(WARN, "row sample iterator is not inited", K(ret), KP_(iterator));
  } else if (OB_FAIL(iterator_->get_next_row(row))) {
    if (OB_ITER_END != ret) {
      STORAGE_LOG(WARN, "multiple merge failed to get next row", K(ret));
    } else {
      STORAGE_LOG(INFO, "total sample row count", K(row_num_));
    }
  } else {
    row_num_++;
  }
  return ret;
}
}
}
