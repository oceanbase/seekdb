
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

#include "ob_column_datum_iter.h"
namespace oceanbase
{
namespace blocksstable
{

int ObColumnDatumIter::get_next(const ObDatum *&datum)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(col_datums_.count() == idx_)) {
    ret = OB_ITER_END;
  } else {
    datum = &col_datums_.at(idx_);
    idx_++;
  }

  return ret;
}

int ObDictDatumIter::get_next(const ObDatum *&datum)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(iter_ == ht_.end())) {
    ret = OB_ITER_END;
  } else {
    datum = &iter_->datum_;
    iter_++;
  }

  return ret;
}

int ObEncodingHashTableDatumIter::get_next(const ObDatum *&datum)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(iter_ == ht_.end())) {
    ret = OB_ITER_END;
  } else {
    datum = iter_->header_->datum_;
    iter_++;
  }

  return ret;
}

}  // namespace blocksstable
}  // namespace oceanbase
