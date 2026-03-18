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

#ifndef OCEANBASE_LOGSERVICE_OB_REMOTE_FETCH_CONTEXT_H_
#define OCEANBASE_LOGSERVICE_OB_REMOTE_FETCH_CONTEXT_H_

#include "lib/container/ob_se_array.h"
#include "logservice/palf/lsn.h"
#include "ob_fetch_log_task.h"
#include "ob_log_restore_define.h"
#include <cstdint>
namespace oceanbase
{
namespace logservice
{
// The fetch log context of one ls,
// if the ls is scheduled with fetch log task,
// it is marked issued.
struct ObRemoteFetchContext
{
  int64_t issue_task_num_;
  int64_t issue_version_;
  int64_t last_fetch_ts_;     // Last time of fetching logs
  palf::LSN max_submit_lsn_;        // Maximum LSN of submitted remote log fetch tasks
  palf::LSN max_fetch_lsn_;         // End LSN of the last fetched log
  share::SCN max_fetch_scn_;  // Log timestamp of the last fetched log
  ObLogRestoreErrorContext error_context_;          // Record error information encountered by this log stream, only valid for leader
  common::ObSEArray<ObFetchLogTask *, 8> submit_array_;

  ObRemoteFetchContext() { reset(); }
  ~ObRemoteFetchContext() { reset(); }
  ObRemoteFetchContext &operator=(const ObRemoteFetchContext &other);
  void reset();
  int reset_sorted_tasks();
  void set_issue_version();
  TO_STRING_KV(K_(issue_task_num), K_(issue_version), K_(last_fetch_ts),
      K_(max_submit_lsn), K_(max_fetch_lsn), K_(max_fetch_scn),
      K_(error_context), "task_count", submit_array_.count());
};
} // namespace logservice
} // namespace oceanbase
#endif /* OCEANBASE_LOGSERVICE_OB_REMOTE_FETCH_CONTEXT_H_ */
