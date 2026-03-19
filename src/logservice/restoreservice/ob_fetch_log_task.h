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
#ifndef OCEANBASE_LOGSERVICE_OB_RESTORE_TASK_H_
#define OCEANBASE_LOGSERVICE_OB_RESTORE_TASK_H_

#include "lib/utility/ob_print_utils.h"
#include "ob_remote_log_iterator.h"           // Iterator
#include "share/ob_ls_id.h"                   // ObLSID
#include "logservice/palf/lsn.h"              // LSN
#include "share/scn.h"              // SCN
#include <cstdint>
namespace oceanbase
{
namespace logservice
{
// The granularity for Restore Service to schedule log fetch.
// ObFetchLogTask is produced by Restore Service and consumed by FetchLogWorkers.
//
// For one ls, more than one tasks can be generated and consumed in parallel,
// which gives to ability to catch up in physical standby and enhance the log fetch speed in physical restore.
class ObFetchLogTask : public common::ObLink
{
public:
  ObFetchLogTask(const share::ObLSID &id,
                 const share::SCN &pre_scn,
                 const palf::LSN &lsn,
                 const int64_t size,
                 const int64_t proposal_id,
                 const int64_t version);

  ~ObFetchLogTask() { reset(); }
  bool is_valid() const;
  void reset();
  TO_STRING_KV(K_(id), K_(proposal_id), K_(version), K_(pre_scn), K_(start_lsn),
      K_(cur_lsn), K_(end_lsn), K_(max_fetch_scn), K_(max_submit_scn), K_(iter));

public:
  share::ObLSID id_;
  // to distinguish stale tasks which is generated in previous leader
  int64_t proposal_id_;
  int64_t version_;
  share::SCN pre_scn_;    // heuristic log scn to locate piece, may be imprecise one
  palf::LSN start_lsn_;
  palf::LSN cur_lsn_;
  palf::LSN end_lsn_;
  share::SCN max_fetch_scn_;     // Maximum SCN of fetched logs
  share::SCN max_submit_scn_;    // Maximum SCN of submitted logs
  ObRemoteLogGroupEntryIterator iter_;
};

struct FetchLogTaskCompare final
{
public:
  FetchLogTaskCompare() {}
  ~FetchLogTaskCompare() {}
  bool operator()(const ObFetchLogTask *left, const ObFetchLogTask *right)
  {
    return left->start_lsn_ < right->start_lsn_;
  }
};
} // namespace logservice
} // namespace oceanbase
#endif /* OCEANBASE_LOGSERVICE_OB_RESTORE_TASK_H_ */
