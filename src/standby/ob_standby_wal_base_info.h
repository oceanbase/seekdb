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

#ifndef OCEANBASE_STANDBY_OB_STANDBY_WAL_BASE_INFO_H_
#define OCEANBASE_STANDBY_OB_STANDBY_WAL_BASE_INFO_H_

#include "lib/ob_define.h"
#include "share/log/types/base_info.h"
#include "share/scn.h"

namespace oceanbase
{
namespace standby
{

struct ObFetchStandbyWalBaseInfoArg final
{
  OB_UNIS_VERSION(1);
public:
  ObFetchStandbyWalBaseInfoArg();
  ~ObFetchStandbyWalBaseInfoArg() {}
  bool is_valid() const { return replay_start_scn_.is_valid(); }

  TO_STRING_KV(K_(replay_start_scn));
  share::SCN replay_start_scn_;
};

struct ObFetchStandbyWalBaseInfoResult final
{
  OB_UNIS_VERSION(1);
public:
  ObFetchStandbyWalBaseInfoResult();
  ~ObFetchStandbyWalBaseInfoResult() {}
  bool is_valid() const { return wal_base_info_.is_valid() && source_end_scn_.is_valid(); }

  TO_STRING_KV(K_(wal_base_info), K_(source_end_lsn), K_(source_end_scn), K_(located_log));
  wal::BaseInfo wal_base_info_;
  wal::LSN source_end_lsn_;
  share::SCN source_end_scn_;
  bool located_log_;
};

class ObStandbyWalBaseInfoBuilder final
{
public:
  static int build(
      const ObFetchStandbyWalBaseInfoArg &arg,
      ObFetchStandbyWalBaseInfoResult &result);
};

} // namespace standby
} // namespace oceanbase

#endif /* OCEANBASE_STANDBY_OB_STANDBY_WAL_BASE_INFO_H_ */
