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

#ifndef OCEANBASE_LOGSERVICE_PALF_CALLBACK_
#define OCEANBASE_LOGSERVICE_PALF_CALLBACK_
#include <stdint.h>
#include "common/ob_role.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/list/ob_dlink_node.h"
#include "lib/utility/ob_print_utils.h"
#include "log_meta_info.h"
#include "share/log/palf/lsn.h"
namespace oceanbase
{
namespace common
{
class ObAddr;
}
namespace palf
{
class PalfFSCb
{
public:
  // end_lsn returns the position of the next log after the last confirmed log
  virtual int update_end_lsn(const LSN &end_lsn, const share::SCN &end_scn) = 0;
};

class PalfMonitorCb
{
public:
  // record events
  virtual int record_set_base_lsn_event(const LSN &new_base_lsn) = 0;
  virtual int record_advance_base_info_event(const PalfBaseInfo &palf_base_info) = 0;
  virtual int record_truncate_event(const LSN &lsn,
                                    const int64_t min_block_id,
                                    const int64_t max_block_id,
                                    const int64_t truncate_end_block_id) = 0;
};

} // end namespace palf
} // end namespace oceanbase
#endif
