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

#define USING_LOG_PREFIX SHARE

#include "share/ob_tablet_checksum_iterator.h"

namespace oceanbase
{
namespace share
{
using namespace oceanbase::common;


void ObTabletChecksumIterator::reset()
{
  reuse();
  sql_proxy_ = nullptr;
  is_inited_ = false;
}

void ObTabletChecksumIterator::reuse()
{
  cur_idx_ = 0;
  checksum_items_.reuse();
  compaction_scn_.reset();
}


int ObTabletChecksumIterator::fetch_next_batch()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ObTabletID start_tablet_id;
    if (checksum_items_.count() > 0) {
      ObTabletChecksumItem tmp_item;
      if (OB_FAIL(checksum_items_.at(checksum_items_.count() - 1, tmp_item))) {
        LOG_WARN("fail to fetch last checksum item", KR(ret), K_(checksum_items));
      } else {
        start_tablet_id = tmp_item.tablet_id_;
      }
    }
    if (OB_SUCC(ret)) {
      checksum_items_.reuse();
      if (OB_FAIL(ObTabletChecksumOperator::load_tablet_checksum_items(*sql_proxy_, start_tablet_id,
          BATCH_FETCH_COUNT, compaction_scn_, checksum_items_))) {
        LOG_WARN("fail to load tablet checksums", KR(ret), K(start_tablet_id),
          K_(compaction_scn));
      } else if (OB_UNLIKELY(0 == checksum_items_.count())) {
        ret = OB_ITER_END;
      }
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
