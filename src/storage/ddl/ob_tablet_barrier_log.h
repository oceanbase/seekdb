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

#ifndef SRC_STORAGE_OB_TABLET_BARRIER_LOG_H_
#define SRC_STORAGE_OB_TABLET_BARRIER_LOG_H_

#include "lib/utility/ob_print_utils.h"
#include "share/scn.h"

namespace oceanbase
{
namespace storage
{
enum ObTabletBarrierLogStateEnum
{
  TABLET_BARRIER_LOG_INIT = 0,
  TABLET_BARRIER_LOG_WRITTING,
  TABLET_BARRIER_SOURCE_LOG_WRITTEN,
  TABLET_BARRIER_DEST_LOG_WRITTEN
};

struct ObTabletBarrierLogState final
{
public:
  ObTabletBarrierLogState();
  ~ObTabletBarrierLogState() = default;

  ObTabletBarrierLogStateEnum &get_state() { return state_; }
  share::SCN get_scn() const { return scn_; }
  int64_t get_schema_version() const { return schema_version_; }

  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV(K_(state));
private:
  ObTabletBarrierLogStateEnum to_persistent_state() const;
private:
  ObTabletBarrierLogStateEnum state_;
  share::SCN scn_;
  int64_t schema_version_;
};
}
}

#endif /* SRC_STORAGE_OB_TABLET_BARRIER_LOG_H_ */
