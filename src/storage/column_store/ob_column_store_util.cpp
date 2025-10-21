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

#include "ob_column_store_util.h"
#include "storage/memtable/ob_memtable.h"

namespace oceanbase
{
namespace storage
{
void ObCSDatumRange::set_datum_range(const int64_t start, const int64_t end)
{
  datums_[0].set_int(start);
  datums_[1].set_int(end);
  cs_datum_range_.start_key_.datums_ = &datums_[0];
  cs_datum_range_.start_key_.datum_cnt_ = 1;
  cs_datum_range_.set_left_closed();
  cs_datum_range_.end_key_.datums_ = &datums_[1];
  cs_datum_range_.end_key_.datum_cnt_ = 1;
  cs_datum_range_.set_right_closed();
}

} /* storage */
} /* oceanbase */
