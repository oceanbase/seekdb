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
#ifndef OCEANBASE_LOG_FETCHER_PROGRESS_INFO_H_
#define OCEANBASE_LOG_FETCHER_PROGRESS_INFO_H_

#include "logservice/common_util/ob_log_ls_define.h"  // logservice::TenantLSID
#include "ob_log_ls_fetch_ctx.h"  // LSFetchCtx

namespace oceanbase
{
namespace logfetcher
{
struct LSProgressInfo
{
  LSProgressInfo() : tls_id_(), progress_(0) {}
  LSProgressInfo(const logservice::TenantLSID &tls_id, const int64_t progress) : tls_id_(tls_id), progress_(progress) {}

  logservice::TenantLSID tls_id_;
  int64_t progress_;
  TO_STRING_KV(K_(tls_id), K_(progress));
};
// Used to diagnosis and monitoring
typedef common::ObSEArray<LSProgressInfo, 16> LSProgressInfoArray;

struct FetchCtxMapHBFunc
{
  FetchCtxMapHBFunc(const bool print_ls_heartbeat_info);
  bool operator()(const logservice::TenantLSID &tls_id, LSFetchCtx *&ctx);

  bool                    print_ls_heartbeat_info_;
  int64_t                 data_progress_;
  int64_t                 ddl_progress_;
  palf::LSN               ddl_last_dispatch_log_lsn_;
  int64_t                 min_progress_;
  int64_t                 max_progress_;
  logservice::TenantLSID              min_progress_ls_;
  logservice::TenantLSID              max_progress_ls_;
  int64_t                 ls_count_;
  LSProgressInfoArray     ls_progress_infos_;

  TO_STRING_KV(K_(data_progress),
      K_(ddl_last_dispatch_log_lsn),
      K_(min_progress),
      K_(max_progress),
      K_(min_progress_ls),
      K_(max_progress_ls),
      K_(ls_count));
};

typedef FetchCtxMapHBFunc ProgressInfo;

} // namespace logfetcher
} // namespace oceanbase

#endif
