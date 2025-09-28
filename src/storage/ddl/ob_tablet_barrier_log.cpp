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

#define USING_LOG_PREFIX STORAGE
#include "ob_tablet_barrier_log.h"

namespace oceanbase
{
using namespace share;
namespace storage
{
ObTabletBarrierLogState::ObTabletBarrierLogState()
  : state_(TABLET_BARRIER_LOG_INIT), scn_(SCN::min_scn()), schema_version_(0)
{
}

ObTabletBarrierLogStateEnum ObTabletBarrierLogState::to_persistent_state() const
{
  ObTabletBarrierLogStateEnum persistent_state = TABLET_BARRIER_LOG_INIT;
  switch (state_) {
    case TABLET_BARRIER_LOG_INIT:
      // fall through
    case TABLET_BARRIER_LOG_WRITTING:
      persistent_state = TABLET_BARRIER_LOG_INIT;
      break;
    case TABLET_BARRIER_SOURCE_LOG_WRITTEN:
      persistent_state = TABLET_BARRIER_SOURCE_LOG_WRITTEN;
      break;
    case TABLET_BARRIER_DEST_LOG_WRITTEN:
      persistent_state = TABLET_BARRIER_DEST_LOG_WRITTEN;
      break;
  }
  return persistent_state;
}





}
}

