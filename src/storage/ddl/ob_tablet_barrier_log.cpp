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

