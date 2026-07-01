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

#include "storage/direct_load/ob_direct_load_multiple_datum_range.h"
#include "share/schema/ob_table_param.h"

namespace oceanbase
{
namespace storage
{
using namespace common;
using namespace blocksstable;

ObDirectLoadMultipleDatumRange::ObDirectLoadMultipleDatumRange()
{
}

ObDirectLoadMultipleDatumRange::~ObDirectLoadMultipleDatumRange()
{
}

void ObDirectLoadMultipleDatumRange::reset()
{
  start_key_.reset();
  end_key_.reset();
  border_flag_.set_data(0);
}



int ObDirectLoadMultipleDatumRange::assign(const ObDirectLoadMultipleDatumRange &other)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!other.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(other));
  } else {
    reset();
    start_key_ = other.start_key_;
    end_key_ = other.end_key_;
    border_flag_ = other.border_flag_;
  }
  return ret;
}

int ObDirectLoadMultipleDatumRange::assign(const ObTabletID &tablet_id,
                                           const ObDatumRange &range)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid() || !range.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(tablet_id), K(range));
  } else {
    reset();
    if (OB_FAIL(
          start_key_.assign(tablet_id, range.start_key_.datums_, range.start_key_.datum_cnt_))) {
      LOG_WARN("fail to assign rowkey", KR(ret));
    } else if (OB_FAIL(
                 end_key_.assign(tablet_id, range.end_key_.datums_, range.end_key_.datum_cnt_))) {
      LOG_WARN("fail to assign rowkey", KR(ret));
    } else {
      border_flag_ = range.border_flag_;
    }
  }
  return ret;
}

void ObDirectLoadMultipleDatumRange::set_whole_range()
{
  start_key_.set_min_rowkey();
  end_key_.set_max_rowkey();
  border_flag_.set_all_open();
}


} // namespace storage
} // namespace oceanbase
