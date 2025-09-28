/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
  tenant_id_ = OB_INVALID_TENANT_ID;
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
    LOG_WARN("not init", KR(ret), K_(tenant_id));
  } else {
    ObTabletLSPair start_pair;
    if (checksum_items_.count() > 0) {
      ObTabletChecksumItem tmp_item;
      if (OB_FAIL(checksum_items_.at(checksum_items_.count() - 1, tmp_item))) {
        LOG_WARN("fail to fetch last checksum item", KR(ret), K_(tenant_id), K_(checksum_items));
      } else if (OB_FAIL(start_pair.init(tmp_item.tablet_id_, tmp_item.ls_id_))) {
        LOG_WARN("fail to init start tablet_ls_pair", KR(ret), K(tmp_item));
      }
    }
    if (OB_SUCC(ret)) {
      checksum_items_.reuse();
      if (OB_FAIL(ObTabletChecksumOperator::load_tablet_checksum_items(*sql_proxy_, start_pair, 
          BATCH_FETCH_COUNT, tenant_id_, compaction_scn_, checksum_items_))) {
        LOG_WARN("fail to load tablet checksums", KR(ret), K_(tenant_id), K(start_pair), 
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