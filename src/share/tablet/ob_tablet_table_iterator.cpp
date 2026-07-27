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

#include "share/tablet/ob_tablet_table_iterator.h"
#include "share/ob_tablet_meta_table_compaction_operator.h"

namespace oceanbase
{
namespace share
{
using namespace common;


ObTabletMetaIterator::ObTabletMetaIterator()
  : is_inited_(false),
    prefetch_tablet_idx_(0)
{}

void ObTabletMetaIterator::reset()
{
  is_inited_ = false;
  
  prefetch_tablet_idx_ = -1;
  prefetched_tablets_.reset();
}

int ObTabletMetaIterator::inner_init()
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tablet metadata iterator init twice", KR(ret));
  } else {
    prefetch_tablet_idx_ = 0;
    prefetched_tablets_.reset();
  }
  return ret;
}

int ObTabletMetaIterator::next(ObTabletRuntimeInfo &tablet_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (prefetch_tablet_idx_ == -1) {
    ret = OB_ITER_END;
  } else {
    bool find = false;
    while (OB_SUCC(ret) && !find) {
      if (prefetch_tablet_idx_ < prefetched_tablets_.count()) {
        // directly get from prefetched tablet_info
        tablet_info.reset();
        if (OB_FAIL(tablet_info.assign(prefetched_tablets_.at(prefetch_tablet_idx_)))) {
          LOG_WARN("fail to assign tablet_info", KR(ret), K_(prefetch_tablet_idx));
        } else if (tablet_info.is_valid()) {
          find = true;
        }
        ++prefetch_tablet_idx_;
      } else if (OB_FAIL(prefetch())) { // need to prefetch a batch of tablet_info
        if (OB_ITER_END != ret) {
          LOG_WARN("fail to prefetch", KR(ret), K_(prefetch_tablet_idx));
        }
        prefetch_tablet_idx_ = -1;
      } else {
        prefetch_tablet_idx_ = 0;
      }
    }
  }
  return ret;
}

/**
 * -------------------------------------------------------------------ObCompactionTabletMetaIterator-------------------------------------------------------------------
 */
ObCompactionTabletMetaIterator::ObCompactionTabletMetaIterator(
  const bool first_check, const int64_t compaction_scn)
  : ObTabletMetaIterator(),
    first_check_(first_check),
    compaction_scn_(compaction_scn),
    batch_size_(TABLET_META_TABLE_RANGE_GET_SIZE),
    end_tablet_id_()
  {}

void ObCompactionTabletMetaIterator::reset()
{
  ObTabletMetaIterator::reset();
  first_check_ = false;
  compaction_scn_ = 0;
  end_tablet_id_.reset();
  batch_size_ = 0;
}

int ObCompactionTabletMetaIterator::next(ObTabletRuntimeInfo &tablet_info)
{
  int ret = OB_SUCCESS;
  do {
    if (OB_FAIL(ObTabletMetaIterator::next(tablet_info))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("fail to get next tablet info", KR(ret));
      }
    } else if (!tablet_info.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet_info is invalid", KR(ret), K(tablet_info));
    }
  } while (OB_SUCC(ret) && !tablet_info.is_valid());
  return ret;
}

int ObCompactionTabletMetaIterator::init(
    const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(batch_size));
  } else if (OB_FAIL(ObTabletMetaIterator::inner_init())) {
    LOG_WARN("failed to init", KR(ret));
  } else {
    batch_size_ = batch_size;
    is_inited_ = true;
  }
  return ret;
}

int ObCompactionTabletMetaIterator::prefetch()
{
  int ret = OB_SUCCESS;
  if (prefetch_tablet_idx_ >= prefetched_tablets_.count()) {
    ObTabletID tmp_last_tablet_id;
    if (OB_FAIL(ObTabletMetaTableCompactionOperator::range_scan_for_compaction(compaction_scn_,
        end_tablet_id_,
        batch_size_,
        !first_check_/*only_unreported*/,
        tmp_last_tablet_id,
        prefetched_tablets_))) {
      if (OB_ITER_END != ret) {
        LOG_WARN("fail to range get by operator", KR(ret),
            K_(end_tablet_id), K_(batch_size), K_(prefetched_tablets));
      } else {
        prefetch_tablet_idx_ = -1;
      }
    } else if (prefetched_tablets_.count() <= 0) {
      prefetch_tablet_idx_ = -1;
      ret = OB_ITER_END;
    } else {
      end_tablet_id_ = tmp_last_tablet_id;
      prefetch_tablet_idx_ = 0;
    }
  }
  return ret;
}
} // end namespace share
} // end namespace oceanbase
