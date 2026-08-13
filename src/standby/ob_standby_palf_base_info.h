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

#ifndef OCEANBASE_STANDBY_OB_STANDBY_PALF_BASE_INFO_H_
#define OCEANBASE_STANDBY_OB_STANDBY_PALF_BASE_INFO_H_

#include "lib/ob_define.h"
#include "share/log/palf/palf_base_info.h"
#include "share/scn.h"

namespace oceanbase
{
namespace standby
{

struct ObFetchStandbyPalfBaseInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObFetchStandbyPalfBaseInfoArg();
  ~ObFetchStandbyPalfBaseInfoArg() {}
  bool is_valid() const { return replay_start_scn_.is_valid(); }

  TO_STRING_KV(K_(replay_start_scn));
  share::SCN replay_start_scn_;
};

struct ObFetchStandbyPalfBaseInfoResult final
{
  OB_UNIS_VERSION(1);
public:
  ObFetchStandbyPalfBaseInfoResult();
  ~ObFetchStandbyPalfBaseInfoResult() {}
  bool is_valid() const { return palf_base_info_.is_valid() && source_end_scn_.is_valid(); }

  TO_STRING_KV(K_(palf_base_info), K_(source_end_lsn), K_(source_end_scn), K_(located_log));
  palf::PalfBaseInfo palf_base_info_;
  palf::LSN source_end_lsn_;
  share::SCN source_end_scn_;
  bool located_log_;
};

class ObStandbyPalfBaseInfoBuilder final
{
public:
  static int build(
      const ObFetchStandbyPalfBaseInfoArg &arg,
      ObFetchStandbyPalfBaseInfoResult &result);
};

} // namespace standby
} // namespace oceanbase

#endif /* OCEANBASE_STANDBY_OB_STANDBY_PALF_BASE_INFO_H_ */
