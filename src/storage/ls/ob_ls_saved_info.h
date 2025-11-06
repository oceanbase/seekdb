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

#ifndef OCEABASE_STORAGE_LS_SAVED_INFO_
#define OCEABASE_STORAGE_LS_SAVED_INFO_

#include "lib/utility/ob_print_utils.h"
#include "logservice/palf/lsn.h"
#include "share/scn.h"

namespace oceanbase
{
namespace storage
{

struct ObLSSavedInfo final
{
  OB_UNIS_VERSION(1);
public:
  ObLSSavedInfo();
  ~ObLSSavedInfo() = default;
  bool is_valid() const;
  void reset();
  bool is_empty() const;

  TO_STRING_KV(K_(clog_checkpoint_scn), K_(clog_base_lsn), K_(replayable_point), K_(tablet_change_checkpoint_scn));

  share::SCN clog_checkpoint_scn_;
  palf::LSN clog_base_lsn_;
  int64_t replayable_point_;
  share::SCN tablet_change_checkpoint_scn_;
};


}
}

#endif
